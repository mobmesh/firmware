#include <helpers/radio_diag/RadioDiagIntegration.h>

#include <Arduino.h>
#include <Packet.h>
#include <SPIFFS.h>
#include <esp_system.h>
#include <math.h>
#include <helpers/ConfigSerializer.h>
#include <helpers/radio_diag/BattLog.h>
#include <helpers/radio_diag/DiagSummary.h>
#include <helpers/radio_diag/LinkReport.h>
#include <helpers/radio_diag/LnaStudy.h>
#include <helpers/radio_diag/NoiseLog.h>
#include <helpers/radio_diag/RfScan.h>

#define STUDY_PATH        "/rdiag_lna"
#define CONFIG_PATH       "/rdiag.json"
#define STUDY_MAGIC       0x524C4E31UL   // "RLN1"
#define NOISE_PATH        "/rdiag_noise"
#define NOISE_MAGIC       0x524E5331UL   // "RNS1"
#define NOISE_EVERY_MS    15000UL
#define NOISE_SAMPLES     240
#define NOISE_MIN_SAMPLES 20
#define NOISE_LINES_MAX   14
#define BATT_PATH         "/rdiag_batt"
#define BATT_MAGIC        0x52424131UL   // "RBA1"
#define BATT_EVERY_MS     300000UL
#define BATT_LINES_MAX    8
#define BATT_NO_FLOOR_MV  2500
#define BLOCK_MIN_MS      240000UL
#define BLOCK_SPAN_MS     120000UL
#define START_SPAN_MS     60000UL
#define FLOOR_EVERY_MS    2000UL
#define LOOP_EVERY_MS     1000UL
#define FLOOR_SAMPLES     160
#define SAVE_EVERY_S      3600UL
#define PAUSE_RETRY_MS    900000UL
#define PAUSE_LIMIT       4
#define SEEN_SLOTS        64
#define CLOCK_VALID_EPOCH 1700000000UL
#define HOURS_MIN         6
#define HOURS_MAX         168
#define HOURS_DEFAULT     48
#define ROW_MAX           155
#define LINES_MAX         12
#define RFSCAN_LINES_MAX  20
#define RFSCAN_BW_KHZ     250.0f
#define RFSCAN_BREAK_MS   1500
#define LINKS_LINES_MAX   16
#define DIAG_LINES_MAX    15
#define SCAN_STALE_MS     86400000UL
#define STATS_REPLY       200

// Stored exactly as upstream's companion stores tz_offset (int8_t whole hours, key tz_offset), so only
// the file changes if the repeater ever gains that setting.
class DiagConfig : public ConfigSerializer {
public:
  int8_t tz_offset = 0;
protected:
  void structure() override { def("tz_offset", tz_offset); }
};

static DiagConfig config;
static LnaStudy study;
static LnaBlock blocks[2];
static LinkTable links;
static RfScanData scan_data;
static bool have_scan;
static uint32_t scan_ms;
static int8_t floor_buf[FLOOR_SAMPLES];
static uint8_t floor_count;
static uint32_t seen[SEEN_SLOTS];
static uint8_t seen_pos;
static uint8_t self_key[PUB_KEY_SIZE];
static bool have_key;
static bool booted;
static bool mounted;

static NoiseLog noise_log;
static BattLog batt_log;
static uint32_t batt_hour;
static uint32_t last_batt_ms;
static int8_t noise_buf[NOISE_SAMPLES];
static uint8_t noise_count;
static uint32_t noise_hour;
static uint8_t noise_lna;
static uint32_t noise_recv0;
static uint32_t noise_err0;
static uint32_t last_noise_ms;

static uint8_t cur_state;
static uint8_t first_state;
static uint8_t pair_pos;
static uint32_t block_started_ms;
static uint32_t block_len_ms;
static uint32_t last_floor_ms;
static uint32_t last_tick_ms;
static uint32_t tick_rem_ms;
static uint32_t last_save_s;
static bool paused;
static uint32_t paused_at_ms;
static uint8_t pause_count;

// Mounted here rather than assumed, as try-settings does: nothing guarantees another mod mounted it.
static bool ensureMounted() {
  if (!mounted) mounted = SPIFFS.begin(true);
  return mounted;
}

static void saveStudy() {
  if (!ensureMounted()) return;
  File f = SPIFFS.open(STUDY_PATH, "w", true);
  if (!f) return;
  uint32_t magic = STUDY_MAGIC;
  f.write((const uint8_t*)&magic, sizeof(magic));
  f.write((const uint8_t*)&study, sizeof(study));
  f.close();
}

static void loadStudy() {
  memset(&study, 0, sizeof(study));
  if (!ensureMounted() || !SPIFFS.exists(STUDY_PATH)) return;
  File f = SPIFFS.open(STUDY_PATH, "r");
  if (!f) return;
  uint32_t magic = 0;
  bool ok = f.read((uint8_t*)&magic, sizeof(magic)) == sizeof(magic) && magic == STUDY_MAGIC
            && f.read((uint8_t*)&study, sizeof(study)) == sizeof(study);
  f.close();
  if (!ok || study.neighbour_count > LNA_MAX_NEIGHBOURS) memset(&study, 0, sizeof(study));
}

static void saveNoise() {
  if (!ensureMounted()) return;
  File f = SPIFFS.open(NOISE_PATH, "w", true);
  if (!f) return;
  uint32_t magic = NOISE_MAGIC;
  f.write((const uint8_t*)&magic, sizeof(magic));
  f.write((const uint8_t*)&noise_log, sizeof(noise_log));
  f.close();
}

static void loadNoise() {
  memset(&noise_log, 0, sizeof(noise_log));
  if (!ensureMounted() || !SPIFFS.exists(NOISE_PATH)) return;
  File f = SPIFFS.open(NOISE_PATH, "r");
  if (!f) return;
  uint32_t magic = 0;
  bool ok = f.read((uint8_t*)&magic, sizeof(magic)) == sizeof(magic) && magic == NOISE_MAGIC
            && f.read((uint8_t*)&noise_log, sizeof(noise_log)) == sizeof(noise_log);
  f.close();
  if (!ok) memset(&noise_log, 0, sizeof(noise_log));
}

static void saveBatt() {
  if (!ensureMounted()) return;
  File f = SPIFFS.open(BATT_PATH, "w", true);
  if (!f) return;
  uint32_t magic = BATT_MAGIC;
  f.write((const uint8_t*)&magic, sizeof(magic));
  f.write((const uint8_t*)&batt_log, sizeof(batt_log));
  f.close();
}

static void loadBatt() {
  memset(&batt_log, 0, sizeof(batt_log));
  if (!ensureMounted() || !SPIFFS.exists(BATT_PATH)) return;
  File f = SPIFFS.open(BATT_PATH, "r");
  if (!f) return;
  uint32_t magic = 0;
  bool ok = f.read((uint8_t*)&magic, sizeof(magic)) == sizeof(magic) && magic == BATT_MAGIC
            && f.read((uint8_t*)&batt_log, sizeof(batt_log)) == sizeof(batt_log);
  f.close();
  if (!ok) memset(&batt_log, 0, sizeof(batt_log));
}

static void loadConfig() {
  if (!ensureMounted() || !SPIFFS.exists(CONFIG_PATH)) return;
  File f = SPIFFS.open(CONFIG_PATH, "r");
  if (!f) return;
  config.loadSerial(f);
  f.close();
  if (config.tz_offset < -12 || config.tz_offset > 14) config.tz_offset = 0;
}

static bool saveConfig() {
  if (!ensureMounted()) return false;
  File f = SPIFFS.open(CONFIG_PATH, "w", true);
  if (!f) return false;
  bool ok = config.saveSerial(f);
  f.close();
  return ok;
}

static uint8_t currentSf() {
  ModRadioValues rv;
  return modRadioPrefsGet(&rv) ? rv.sf : 7;
}

// UTC, so a later diag.tz change moves the whole study; the report applies the offset.
static int currentHour() {
  if (!study.clock_valid) return -1;
  return (int)((modClockGet() / 3600UL) % 24);
}

static void beginBlock(uint8_t state, uint32_t extra_ms) {
  cur_state = state;
  modFemLnaSet(state == LNA_ON);
  block_started_ms = millis();
  block_len_ms = BLOCK_MIN_MS + esp_random() % BLOCK_SPAN_MS + extra_ms;
  floor_count = 0;
}

static void beginPair(uint32_t extra_ms) {
  memset(blocks, 0, sizeof(blocks));
  first_state = esp_random() & 1;
  pair_pos = 0;
  beginBlock(first_state, extra_ms);
}

static int8_t median(int8_t* buf, int count) {
  for (int i = 1; i < count; i++) {
    int8_t v = buf[i];
    int j = i - 1;
    while (j >= 0 && buf[j] > v) {
      buf[j + 1] = buf[j];
      j--;
    }
    buf[j + 1] = v;
  }
  return buf[count / 2];
}

static void closeFloor() {
  if (floor_count == 0) return;
  blocks[cur_state].floor_dbm = median(floor_buf, floor_count);
  blocks[cur_state].floor_valid = true;
}

// A restore is skipped when the operator changed the setting: their value stands.
static void finish(bool restore) {
  study.result = lnaVerdict(study, currentSf(), true);
  study.active = 0;
  paused = false;
  if (restore) modFemLnaSet(study.start_lna == LNA_ON);
  saveStudy();
}

static void endBlock() {
  closeFloor();
  if (pair_pos == 0) {
    pair_pos = 1;
    beginBlock(first_state ^ 1, 0);
    return;
  }
  int day = (int)(study.elapsed_s / 86400UL);
  lnaAddPair(study, blocks[LNA_ON], blocks[LNA_OFF], currentHour(), day);
  pause_count = 0;
  if (study.elapsed_s >= study.requested_s || lnaShouldStop(study, currentSf())) {
    finish(true);
    return;
  }
  beginPair(0);
}

// Idle RSSI as a whole dBm; false mid-packet.
static bool readFloor(int8_t* out) {
  float rssi;
  if (!modRadioRssiNow(&rssi)) return false;
  if (rssi < -127) rssi = -127;
  if (rssi > 0) rssi = 0;
  *out = (int8_t)lroundf(rssi);
  return true;
}

static void sampleFloor(uint32_t now) {
  if (now - last_floor_ms < FLOOR_EVERY_MS || floor_count >= FLOOR_SAMPLES) return;
  last_floor_ms = now;
  if (readFloor(&floor_buf[floor_count])) floor_count++;
}

// A counter from one of upstream's serial-only stats replies, which mods reach with timestamp 0.
static uint32_t statValue(const char* command, const char* key) {
  char cmd[24], rep[STATS_REPLY] = "";
  strncpy(cmd, command, sizeof(cmd) - 1);
  cmd[sizeof(cmd) - 1] = 0;
  modCliDispatch(0, cmd, rep);
  char pattern[32];
  snprintf(pattern, sizeof(pattern), "\"%s\":", key);
  const char* at = strstr(rep, pattern);
  return at ? strtoul(at + strlen(pattern), nullptr, 10) : 0;
}

static uint32_t clockHour() {
  uint32_t now = modClockGet();
  return now > CLOCK_VALID_EPOCH ? now / 3600 : 0;
}

static uint8_t lnaNow() {
  if (!modFemLnaAvailable()) return NOISE_LNA_NA;
  return modFemLnaGet() ? LNA_ON : LNA_OFF;
}

// The state the node runs in outside a study; noise hours are compared only within one state.
static uint8_t lnaSettled() {
  if (!modFemLnaAvailable()) return NOISE_LNA_NA;
  return study.active ? study.start_lna : lnaNow();
}

static uint16_t clamp16(uint32_t v) {
  return v > 0xFFFF ? 0xFFFF : (uint16_t)v;
}

static void noiseClose() {
  if (noise_hour == 0 || noise_count < NOISE_MIN_SAMPLES) return;
  uint32_t recv = statValue("stats-packets", "recv"), errors = statValue("stats-packets", "recv_errors");
  NoiseHour h;
  h.hour = noise_hour;
  h.floor_dbm = median(noise_buf, noise_count);
  h.lna = noise_lna;
  h.counts_valid = recv >= noise_recv0 && errors >= noise_err0;
  h.recv = h.counts_valid ? clamp16(recv - noise_recv0) : 0;
  h.errors = h.counts_valid ? clamp16(errors - noise_err0) : 0;
  noiseAddHour(noise_log, h);
  saveNoise();
}

// One hour per slot while the clock is set; samples in a study's opposite LNA state are skipped.
static void noiseTick(uint32_t now) {
  if (now - last_noise_ms < NOISE_EVERY_MS) return;
  last_noise_ms = now;
  uint32_t hour = clockHour();
  if (hour != noise_hour) {
    // A clock set or jump closes nothing: that hour's epoch is not the one it was sampled in.
    if (noise_hour && hour == noise_hour + 1) noiseClose();
    noise_hour = hour;
    noise_count = 0;
    noise_lna = lnaSettled();
    noise_recv0 = statValue("stats-packets", "recv");
    noise_err0 = statValue("stats-packets", "recv_errors");
  }
  if (hour == 0 || noise_count >= NOISE_SAMPLES) return;
  if (lnaNow() == noise_lna && readFloor(&noise_buf[noise_count])) noise_count++;
}

// One file write an hour; a clock jump only starts another slot.
static bool battSampleNow() {
  uint16_t mv = modBoardBattMilliVolts();
  uint32_t hour = clockHour();
  if (mv == 0 || hour == 0) return mv != 0;
  if (batt_hour != 0 && hour != batt_hour) saveBatt();
  batt_hour = hour;
  battSample(batt_log, hour, config.tz_offset, mv);
  return true;
}

static void battTick(uint32_t now) {
  if (last_batt_ms != 0 && now - last_batt_ms < BATT_EVERY_MS) return;
  last_batt_ms = now | 1;
  battSampleNow();
}

static void boot() {
  booted = true;
  loadConfig();
  have_key = modPublisherKey(self_key);
  loadNoise();
  loadBatt();
  loadStudy();
  if (!study.active) return;
  // The saved setting is applied at boot; a different value means the operator changed it meanwhile.
  if (!modFemLnaAvailable() || modFemLnaGet() != (study.start_lna == LNA_ON)) {
    finish(false);
    return;
  }
  last_tick_ms = millis();
  tick_rem_ms = 0;
  last_save_s = study.elapsed_s;
  beginPair(0);
}

void radioDiagLoop() {
  if (!booted) boot();
  // Every pass otherwise; nothing here needs better than 1 s, and each pass of work shows on the meter.
  static uint32_t last_run_ms;
  uint32_t now = millis();
  if (now - last_run_ms < LOOP_EVERY_MS) return;
  last_run_ms = now;
  noiseTick(now);
  battTick(now);
  if (!study.active) return;

  if (paused) {
    if (now - paused_at_ms < PAUSE_RETRY_MS) return;
    paused = false;
    last_tick_ms = now;
    beginPair(0);
    return;
  }
  // Another feature (power-guard's low-battery drop) moved the LNA: the pair is discarded, not mixed.
  if (modFemLnaGet() != (cur_state == LNA_ON)) {
    if (++pause_count >= PAUSE_LIMIT) {
      finish(false);
      return;
    }
    paused = true;
    paused_at_ms = now;
    memset(blocks, 0, sizeof(blocks));
    return;
  }

  tick_rem_ms += now - last_tick_ms;
  last_tick_ms = now;
  study.elapsed_s += tick_rem_ms / 1000;
  tick_rem_ms %= 1000;

  sampleFloor(now);
  if (now - block_started_ms >= block_len_ms) endBlock();
  if (study.active && study.elapsed_s - last_save_s >= SAVE_EVERY_S) {
    last_save_s = study.elapsed_s;
    saveStudy();
  }
}

static bool seenBefore(const mesh::Packet* packet) {
  uint8_t hash[MAX_HASH_SIZE];
  packet->calculatePacketHash(hash);
  uint32_t key;
  memcpy(&key, hash, sizeof(key));
  for (int i = 0; i < SEEN_SLOTS; i++) {
    if (seen[i] == key) return true;
  }
  seen[seen_pos] = key;
  seen_pos = (seen_pos + 1) % SEEN_SLOTS;
  return false;
}

// A path that carries this node's hash passed through it already: an echo, not new traffic.
static bool isEcho(const mesh::Packet* packet) {
  if (!have_key) return false;
  uint8_t size = packet->getPathHashSize();
  for (int i = 0; i < packet->getPathHashCount(); i++) {
    if (memcmp(&packet->path[i * size], self_key, size) == 0) return true;
  }
  return packet->getPayloadType() == PAYLOAD_TYPE_ADVERT && packet->payload_len >= PUB_KEY_SIZE
         && memcmp(packet->payload, self_key, PUB_KEY_SIZE) == 0;
}

static uint16_t hashId(const uint8_t* bytes, uint8_t id_len) {
  return id_len == 2 ? (uint16_t)((bytes[0] << 8) | bytes[1]) : bytes[0];
}

// Every reception counts, duplicates included: each relay of a packet is a separate transmission.
static void observeLinks(const mesh::Packet* packet) {
  uint8_t size = packet->getPathHashSize();
  uint8_t id_len = size < 2 ? size : 2;
  uint8_t hops = packet->getPathHashCount();
  bool advert = packet->getPayloadType() == PAYLOAD_TYPE_ADVERT && packet->payload_len >= PUB_KEY_SIZE + 4;
  bool own_advert = advert && have_key && memcmp(packet->payload, self_key, PUB_KEY_SIZE) == 0;
  const uint8_t* sender = nullptr;
  if (packet->isRouteFlood() && hops > 0) sender = &packet->path[(hops - 1) * size];
  else if (hops == 0 && advert && !own_advert) sender = packet->payload;
  if (!sender || (have_key && memcmp(sender, self_key, id_len) == 0)) return;
  uint32_t now = millis();
  int index = linkIndex(links, hashId(sender, id_len), id_len, now);
  linkAddPacket(links, index, packet->getSNR(), now);

  // It relayed straight after this node, or relayed its advert: it hears this node.
  bool after_us = hops >= 2 && have_key && memcmp(&packet->path[(hops - 2) * size], self_key, size) == 0;
  if (after_us || (own_advert && hops == 1)) linkAddEcho(links, index);

  if (hops == 0 && advert && !own_advert) {
    uint32_t sent, here = modClockGet();
    memcpy(&sent, &packet->payload[PUB_KEY_SIZE], 4);
    if (here > CLOCK_VALID_EPOCH && sent > CLOCK_VALID_EPOCH) linkAddClock(links, index, (int32_t)((int64_t)sent - here));
  }
}

void radioDiagRecv(const mesh::Packet* packet, bool accepted, const uint8_t scope_key[16]) {
  (void)accepted;
  (void)scope_key;
  if (!booted) boot();
  observeLinks(packet);
  if (!study.active || paused) return;
  if (seenBefore(packet) || isEcho(packet)) return;
  LnaBlock& block = blocks[cur_state];
  block.unique++;

  // The last path entry is the transmitter; a zero-hop advert names it in the payload.
  uint8_t size = packet->getPathHashSize();
  uint8_t id_len = size < 2 ? size : 2;
  const uint8_t* id_bytes = nullptr;
  if (packet->isRouteFlood() && packet->getPathHashCount() > 0) {
    id_bytes = &packet->path[(packet->getPathHashCount() - 1) * size];
  } else if (packet->getPathHashCount() == 0 && packet->getPayloadType() == PAYLOAD_TYPE_ADVERT
             && packet->payload_len >= PUB_KEY_SIZE) {
    id_bytes = packet->payload;
  }
  if (!id_bytes) return;
  uint16_t id = id_len == 2 ? (uint16_t)((id_bytes[0] << 8) | id_bytes[1]) : id_bytes[0];
  int index = lnaNeighbourIndex(study, id, id_len);
  if (index < 0) return;
  block.heard[index]++;
  block.snr_q[index] += packet->_snr;
  block.snr_n[index]++;
}

// One row fits the caller's reply buffer; more go out through the shim's extra-reply queue.
static void emitLines(const ModCliContext& context, char* reply, const char* lines, size_t stride, int count) {
  static char rows[6][ROW_MAX + 1];
  int row = 0;
  rows[0][0] = 0;
  for (int i = 0; i < count; i++) {
    const char* line = lines + i * stride;
    size_t cur = strlen(rows[row]), add = strlen(line) + (cur ? 1 : 0);
    if (cur + add > ROW_MAX) {
      if (row == 5) break;
      rows[++row][0] = 0;
      cur = 0;
    }
    if (cur) strcat(rows[row], "\n");
    strcat(rows[row], line);
  }
  if (row == 0) {
    strcpy(reply, rows[0]);
    return;
  }
  for (int i = 0; i <= row; i++) modCliAddRow(context.sender_timestamp, rows[i]);
  reply[0] = 0;
}

static void emit(const ModCliContext& context, char* reply, char lines[][48], int count) {
  emitLines(context, reply, &lines[0][0], 48, count);
}

// The mesh channel the node is on now: a live tempradio trial, else the saved radio settings.
static bool liveChannel(ModRadioValues* out) {
  if (modTempRadioGet(&out->freq, &out->bw, &out->sf, &out->cr)) return true;
  return modRadioPrefsGet(out);
}

// Blocks the loop for about 5 s; the node is off its channel for at most about 1.3 s at a time.
static const char* scanInto(RfScanData& data) {
  if (!modRadioScanAvailable()) return "ERR: rfscan needs an SX126x radio";
  float probe;
  ModRadioValues ch;
  if (!modRadioRssiNow(&probe)) return "ERR: radio busy, try again";
  if (!liveChannel(&ch)) return "ERR: radio settings unavailable";
  data.centre = ch.freq;
  data.lna = modFemLnaAvailable() ? (modFemLnaGet() ? 1 : 0) : RFSCAN_LNA_NA;
  modRadioScanEnter(RFSCAN_BW_KHZ);
  modRadioScanCalibrate(rfScanBandFreq(ch.freq, RFSCAN_BAND_STEPS / 2));
  for (int b = 0; b < RFSCAN_BAND_STEPS; b++) data.band[b] = modRadioScanRead(rfScanBandFreq(ch.freq, b));
  modRadioScanCalibrate(ch.freq);
  for (int sw = 0; sw < RFSCAN_SWEEPS; sw++) {
    if (sw == RFSCAN_SWEEPS / 2) {
      modRadioScanLeave(ch.freq, ch.bw, ch.sf, ch.cr);
      delay(RFSCAN_BREAK_MS);
      modRadioScanEnter(RFSCAN_BW_KHZ);
      modRadioScanCalibrate(ch.freq);
    }
    for (int b = 0; b < RFSCAN_STEPS; b++) data.raw[sw][b] = modRadioScanRead(rfScanStepFreq(ch.freq, b));
  }
  modRadioScanLeave(ch.freq, ch.bw, ch.sf, ch.cr);
  have_scan = true;
  scan_ms = millis();
  return nullptr;
}

static void runScan(const ModCliContext& context, char* reply) {
  const char* err = scanInto(scan_data);
  if (err) {
    strcpy(reply, err);
    return;
  }
  static char lines[RFSCAN_LINES_MAX][RFSCAN_LINE];
  int n = rfScanFormat(scan_data, lines, RFSCAN_LINES_MAX);
  emitLines(context, reply, &lines[0][0], RFSCAN_LINE, n);
}

static void ageText(uint32_t ms, char* out, size_t len) {
  uint32_t min = ms / 60000UL;
  if (min < 1) out[0] = 0;
  else if (min < 120) snprintf(out, len, " (%lum ago)", (unsigned long)min);
  else snprintf(out, len, " (%luh ago)", (unsigned long)(min / 60));
}

static void fix(DiagArea* a, uint8_t rank, const char* title, const char* line1, const char* line2, const char* see) {
  a->rank = rank;
  a->title = title;
  snprintf(a->fix[0], sizeof(a->fix[0]), "%s", line1);
  snprintf(a->fix[1], sizeof(a->fix[1]), "%s", line2 ? line2 : "");
  a->see = see;
}

static void lnaArea(DiagSummary& sum) {
  DiagArea* a = diagAdd(sum, "\U0001F4F6", "LNA");
  bool on = modFemLnaGet();
  if (study.active) {
    snprintf(a->text, sizeof(a->text), "studying, %lu of %luh", (unsigned long)(study.elapsed_s / 3600),
             (unsigned long)(study.requested_s / 3600));
    a->learning = true;
    return;
  }
  switch (study.result) {
    case LNA_V_NOT_WORKING:
      a->level = DIAG_PROBLEM;
      strcpy(a->text, "not working");
      fix(a, 3, "LNA fault", "Switching the LNA barely moves the", "floor: check the board's FEM.", "diag lna");
      return;
    case LNA_V_OVERLOADED:
    case LNA_V_NO_BENEFIT: {
      bool overload = study.result == LNA_V_OVERLOADED;
      if (!on) {
        a->level = DIAG_GOOD;
        strcpy(a->text, "off, as the study advised");
        return;
      }
      a->level = overload ? DIAG_PROBLEM : DIAG_CAUTION;
      strcpy(a->text, overload ? "overloaded" : "no benefit here");
      fix(a, overload ? 3 : 8, overload ? "LNA overloaded" : "LNA not needed", "tryset 86400 radio.fem.rxgain off",
          overload ? "then: tryset keep" : "saves about 9 mA; then: tryset keep", "diag lna");
      return;
    }
    case LNA_V_KEEP_ON_LOST:
    case LNA_V_HELPS:
      if (on) {
        a->level = DIAG_GOOD;
        strcpy(a->text, "keep on");
        return;
      }
      a->level = DIAG_CAUTION;
      strcpy(a->text, "off, but the study says on");
      fix(a, 3, "LNA off", "tryset 86400 radio.fem.rxgain on", "then: tryset keep", "diag lna");
      return;
    case LNA_V_NOT_ENOUGH:
      strcpy(a->text, "last study too quiet");
      return;
    default:
      strcpy(a->text, "no study (diag lna start)");
      return;
  }
}

// Runs a scan only when none is cached or the cache is a day old, so diag is usually instant.
static void nearbyArea(DiagSummary& sum) {
  DiagArea* a = diagAdd(sum, "\U0001F4E1", "Nearby");
  if (!have_scan || millis() - scan_ms >= SCAN_STALE_MS) {
    static RfScanData fresh_scan;
    if (!scanInto(fresh_scan)) scan_data = fresh_scan;
  }
  if (!have_scan) {
    strcpy(a->text, "radio busy, try again");
    return;
  }
  char finding[40], age[16];
  int level = rfScanSummary(scan_data, finding, sizeof(finding));
  ageText(millis() - scan_ms, age, sizeof(age));
  snprintf(a->text, sizeof(a->text), "%s%s", finding, age);
  a->level = level == 2 ? DIAG_PROBLEM : level == 1 ? DIAG_CAUTION : DIAG_GOOD;
  if (level == 2) fix(a, 1, "interference", "Find the source, or tempradio to", "a clear channel.", "diag rfscan");
  if (level == 1) fix(a, 1, "signal next to channel", "Find it, or tempradio a little", "further from it.", "diag rfscan");
}

static NoiseView noiseView(char* weakest, size_t len) {
  float margin = NOISE_NO_MARGIN;
  if (!linkWeakest(links, currentSf(), millis(), &margin, weakest, len)) margin = NOISE_NO_MARGIN;
  return noiseAnalyse(noise_log, clockHour(), config.tz_offset, lnaSettled(), margin);
}

static void runNoise(const ModCliContext& context, char* reply) {
  if (clockHour() == 0) {
    strcpy(reply, "ERR: clock not set (time <epoch>)");
    return;
  }
  char weakest[8] = "";
  NoiseView v = noiseView(weakest, sizeof(weakest));
  static char lines[NOISE_LINES_MAX][NOISE_LINE];
  int n = noiseFormat(v, weakest, lines, NOISE_LINES_MAX);
  emitLines(context, reply, &lines[0][0], NOISE_LINE, n);
}

// Only the board whose divider was bench-measured gets percent and time left; power-guard ships on the same one.
static BattView battView() {
#ifdef WITH_POWER_GUARD
  char command[] = "powersaving safe.mv";
  char reply[32] = "";
  modCliDispatch(0, command, reply);
  int floor_mv = reply[0] == '>' ? atoi(reply + 1) : 0;
  // Without a floor the node runs on towards brownout.
  return battAnalyse(batt_log, clockHour(), config.tz_offset, true, floor_mv > 0 ? (uint16_t)floor_mv : BATT_NO_FLOOR_MV);
#else
  return battAnalyse(batt_log, clockHour(), config.tz_offset, false, BATT_NO_FLOOR_MV);
#endif
}

static void runBatt(const ModCliContext& context, char* reply) {
  if (clockHour() == 0) {
    strcpy(reply, "ERR: clock not set (time <epoch>)");
    return;
  }
  if (!battSampleNow()) {
    strcpy(reply, "🔋 Battery: ⚪ no sensor on this board");
    return;
  }
  // Held only while the report is built; emitLines copies the text out.
  char (*lines)[BATT_LINE] = (char (*)[BATT_LINE])malloc(BATT_LINES_MAX * BATT_LINE);
  if (lines == nullptr) {
    strcpy(reply, "ERR: out of memory");
    return;
  }
  BattView v = battView();
  int n = battFormat(v, lines, BATT_LINES_MAX);
  emitLines(context, reply, &lines[0][0], BATT_LINE, n);
  free(lines);
}

static void battArea(DiagSummary& sum) {
  DiagArea* a = diagAdd(sum, "🔋", "Battery");
  if (clockHour() == 0) {
    strcpy(a->text, "clock not set");
    return;
  }
  if (!battSampleNow()) {
    strcpy(a->text, "no sensor");
    return;
  }
  BattView v = battView();
  a->level = battSummary(v, a->text, sizeof(a->text));
  a->learning = true;
  if (v.kind == BATT_K_LOW) fix(a, 0, "low battery", "Charge it now, or it may", "stop soon.", "diag batt");
  else fix(a, 3, "battery falling", "Check the panel or charger:", "it isn't holding its level.", "diag batt");
}

static void noiseArea(DiagSummary& sum) {
  DiagArea* a = diagAdd(sum, "\U0001F50A", "Noise");
  if (clockHour() == 0) {
    strcpy(a->text, "clock not set");
    return;
  }
  char weakest[8] = "";
  NoiseView v = noiseView(weakest, sizeof(weakest));
  a->level = noiseSummary(v, a->text, sizeof(a->text));
  a->learning = true;
  if (v.kind == NOISE_K_ERRORS) fix(a, 7, "failing packets", "Packets fail without a noise rise:", "diag noise shows when.", "diag noise");
  else fix(a, 1, "noise source", "Find what runs in the loud hours;", "diag rfscan then shows where.", "diag noise");
}

// The last 24 h when the noise log has counts, else since boot.
static void errorsArea(DiagSummary& sum) {
  DiagArea* a = diagAdd(sum, "📉", "Decodes");
  uint32_t recv = statValue("stats-packets", "recv"), errors = statValue("stats-packets", "recv_errors");
  uint32_t day_recv, day_errors, hour = clockHour();
  bool day = hour && noiseCounts(noise_log, hour, &day_recv, &day_errors);
  if (day && recv >= noise_recv0 && errors >= noise_err0) {
    day_recv += recv - noise_recv0;
    day_errors += errors - noise_err0;
  }
  a->level = day ? diagErrors(day_recv, day_errors, a->text, sizeof(a->text))
                 : diagErrors(recv, errors, a->text, sizeof(a->text));
  if (day && a->level != DIAG_UNKNOWN) strncat(a->text, ", 24h", sizeof(a->text) - strlen(a->text) - 1);
  a->learning = true;
  fix(a, 7, "failed decodes", "Many failed decodes: look for", "interference with diag rfscan.", "diag rfscan");
}

static void runSummary(const ModCliContext& context, char* reply) {
  static DiagSummary sum;
  memset(&sum, 0, sizeof(sum));
  uint8_t sf = currentSf();
  uint32_t now = millis();
  sum.uptime_s = statValue("stats-core", "uptime_secs");

  DiagArea* a = diagAdd(sum, "\U0001F517", "Links");
  a->level = linkSummary(links, sf, now, a->text, sizeof(a->text));
  a->learning = true;
  switch (linkVerdict(links, sf, now)) {
    case LINK_V_MOST_WEAK:
      fix(a, 2, "weak links", "Weak on all sides: check antenna,", "cable and connector.", "diag links");
      break;
    case LINK_V_ONE_WAY:
      fix(a, 4, "one-way link", "Check our TX power, antenna and", "cable.", "diag links");
      break;
    case LINK_V_CLOSE:
    case LINK_V_CAN_DROP:
      fix(a, 5, "weak link", "Higher antenna or a relay helps", "most.", "diag links");
      break;
    default:
      break;
  }
  if (modFemLnaAvailable()) lnaArea(sum);
  if (modRadioScanAvailable()) nearbyArea(sum);
  noiseArea(sum);
  battArea(sum);
  errorsArea(sum);

  a = diagAdd(sum, "\U0001F4CA", "Load");
  a->level = diagLoad(statValue("stats-radio", "tx_air_secs") + statValue("stats-radio", "rx_air_secs"),
                      sum.uptime_s, a->text, sizeof(a->text));
  a->learning = true;
  fix(a, 6, "busy channel", "Busy channel: flood-gate or a", "faster preset can help.", nullptr);

  a = diagAdd(sum, "\U0001F9ED", "Paths");
  a->level = linkPathsSummary(links, now, a->text, sizeof(a->text));
  a->learning = true;
  fix(a, 9, "one path", "Most traffic via one neighbour:", "a second in range adds backup.", "diag links");

  a = diagAdd(sum, "\U0001F550", "Clock");
  a->level = linkClockSummary(links, now, a->text, sizeof(a->text));
  fix(a, 10, "clock off", "Sync the clock from the app.", nullptr, nullptr);

  static char lines[DIAG_LINES_MAX][DIAG_LINE];
  int n = diagFormat(sum, lines, DIAG_LINES_MAX);
  emitLines(context, reply, &lines[0][0], DIAG_LINE, n);
}

static void startStudy(const ModCliContext& context, const char* arg, char* reply) {
  if (!modFemLnaAvailable()) {
    strcpy(reply, "ERR: no switchable LNA on this board");
    return;
  }
  if (study.active) {
    strcpy(reply, "ERR: study already running (diag lna stop)");
    return;
  }
  long hours = HOURS_DEFAULT;
  if (*arg) {
    char* end;
    hours = strtol(arg, &end, 10);
    while (*end == ' ') end++;
    if (*end != 0) hours = -1;
  }
  if (hours < HOURS_MIN || hours > HOURS_MAX) {
    strcpy(reply, "ERR: hours 6-168");
    return;
  }
  memset(&study, 0, sizeof(study));
  study.active = 1;
  study.start_lna = modFemLnaGet() ? LNA_ON : LNA_OFF;
  study.requested_s = (uint32_t)hours * 3600UL;
  study.clock_valid = modClockGet() > CLOCK_VALID_EPOCH;
  paused = false;
  pause_count = 0;
  last_tick_ms = millis();
  tick_rem_ms = 0;
  last_save_s = 0;
  beginPair(esp_random() % START_SPAN_MS);
  saveStudy();

  static char lines[LINES_MAX][48];
  int n = 0;
  snprintf(lines[n++], 48, "📶 LNA study started, up to %ldh", hours);
  snprintf(lines[n++], 48, "LNA now %s. Switching on/off", study.start_lna == LNA_ON ? "on" : "off");
  snprintf(lines[n++], 48, "every ~5 min; the setting is");
  snprintf(lines[n++], 48, "restored at the end.");
  snprintf(lines[n++], 48, "Check progress: diag lna");
  emit(context, reply, lines, n);
}

static bool isWord(const char* command, const char* word) {
  size_t len = strlen(word);
  return strncmp(command, word, len) == 0 && (command[len] == 0 || command[len] == ' ');
}

bool radioDiagHandleCli(const ModCliContext& context, char* command, char* reply) {
  if (!booted) boot();

  // An operator change to the LNA ends the study without a restore; the command itself still runs.
  if (study.active && (isWord(command, "set radio.fem.rxgain")
                       || (isWord(command, "tryset") && strstr(command, "radio.fem.rxgain")))) {
    finish(false);
    return false;
  }

  if (strcmp(command, "get diag.tz") == 0) {
    sprintf(reply, "> %d", config.tz_offset);
    return true;
  }
  if (strncmp(command, "set diag.tz ", 12) == 0) {
    int8_t hours;
    if (!lnaParseTz(command + 12, &hours)) {
      strcpy(reply, "Error, must be from -12 to +14");
      return true;
    }
    config.tz_offset = hours;
    strcpy(reply, saveConfig() ? "OK" : "ERR: could not save");
    return true;
  }

  if (strcmp(command, "diag") == 0) {
    runSummary(context, reply);
    return true;
  }
  if (strcmp(command, "diag links") == 0) {
    static char link_lines[LINKS_LINES_MAX][LINKS_LINE];
    int n = linkFormat(links, currentSf(), millis(), link_lines, LINKS_LINES_MAX);
    emitLines(context, reply, &link_lines[0][0], LINKS_LINE, n);
    return true;
  }
  if (strcmp(command, "diag noise") == 0) {
    runNoise(context, reply);
    return true;
  }
  if (strcmp(command, "diag batt") == 0) {
    runBatt(context, reply);
    return true;
  }
  if (strcmp(command, "diag rfscan") == 0) {
    runScan(context, reply);
    return true;
  }
  if (!isWord(command, "diag lna")) return false;
  const char* arg = command + 8;
  while (*arg == ' ') arg++;

  static char lines[LINES_MAX][48];
  if (*arg == 0) {
    int n = study.active ? lnaFormatProgress(study, currentSf(), lines, LINES_MAX)
                         : lnaFormatResult(study, currentSf(), config.tz_offset, lines, LINES_MAX);
    emit(context, reply, lines, n);
    return true;
  }
  if (isWord(arg, "start")) {
    const char* hours = arg + 5;
    while (*hours == ' ') hours++;
    startStudy(context, hours, reply);
    return true;
  }
  if (strcmp(arg, "stop") == 0) {
    if (!study.active) {
      strcpy(reply, "ERR: no study running");
      return true;
    }
    finish(true);
    emit(context, reply, lines, lnaFormatResult(study, currentSf(), config.tz_offset, lines, LINES_MAX));
    return true;
  }
  strcpy(reply, "ERR: diag lna [start [hours]|stop]");
  return true;
}
