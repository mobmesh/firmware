#include <helpers/diag_reports/DiagReportsIntegration.h>

#include <Arduino.h>
#include <Packet.h>
#include <math.h>
#include <stdlib.h>
#include <helpers/CoreIntegration.h>
#include <helpers/ModFS.h>
#include <helpers/ModLateReply.h>
#include <helpers/ModPlatform.h>
#include <helpers/ModProbe.h>
#include <helpers/ModTemperature.h>
#include <helpers/ModTz.h>
#include <helpers/diag_reports/DiagBatt.h>
#include <helpers/diag_reports/DiagEngine.h>
#include <helpers/diag_reports/DiagEvidence.h>
#include <helpers/diag_reports/DiagRelate.h>
#include <helpers/diag_reports/DiagLinks.h>
#ifdef WITH_DIRECT_RETRY
#include <helpers/direct_retry/DirectRetryIntegration.h>
#endif
#include <helpers/diag_reports/DiagLna.h>
#include <helpers/diag_reports/DiagNoise.h>
#include <helpers/diag_reports/DiagOut.h>
#include <helpers/diag_reports/DiagScan.h>
#include <helpers/diag_reports/DiagSched.h>
#include <helpers/diag_reports/DiagStore.h>

#define STORE_PATH         "/diag2"
#define STUDY_PATH         "/diag2_lna"
#define NOISE_EVERY_MS     15000UL
#define NOISE_MIN_SAMPLES  20
#define BATT_EVERY_MS      300000UL
#define BATT_ABSENT_READS  3
#define TICK_EVERY_MS      1000UL
#define CLOCK_JUMP_S       120
#define SCAN_STALE_MS      86400000UL
#define SCAN_BREAK_MS      1500UL
#define SCAN_START_WAIT_MS 10000UL
#define BURST_MS           2000UL
#define BURST_EVERY_MS     20UL
#define RESET_WINDOW_MS    60000UL
#define FREEZE_T1_COUNT    2
#define LOUD_T2_DB         6
#define DECODES_T3_COUNT   3
#define AFTER_SCAN_SKIP    2           // noise samples dropped after a scan's AGC reset
#define BLOCK_MIN_MS       240000UL
#define BLOCK_SPAN_MS      120000UL
#define START_SPAN_MS      60000UL
#define FLOOR_EVERY_MS     2000UL
#define FLOOR_SAMPLES      160
#define STUDY_SAVE_S       3600UL
#define PAUSE_RETRY_MS     900000UL
#define PAUSE_LIMIT        4
#define SEEN_SLOTS         64
#define HOURS_MIN          6
#define HOURS_MAX          168
#define HOURS_DEFAULT      48
// Upstream boots with its clock at a fixed date in 2024, so only a time at or after this build counts as set.
#define CLOCK_VALID_EPOCH  ((uint32_t)MOBMESH_BUILD_EPOCH)
#define ADVERT_CLOCK_FLOOR 1700000000UL

static const char* const LEGACY_FILES[] = {"/rdiag.json", "/rdiag_lna", "/rdiag_noise", "/rdiag_batt", "/rdiag_hist", "/rdiag_today"};

static DiagStore store;
static LinkTable links;
static DiagOut out;
static bool booted, mounted;

// Hour in progress
static uint32_t cur_hour;
static DbmHistogram histo;
static uint8_t histo_lna;
static uint8_t skip_samples;
static uint32_t recv0, err0, air0;
static bool counters_start;
static uint32_t last_noise_ms, last_batt_ms, last_tick_ms;
static uint8_t batt_zeros;
static uint32_t last_clock, last_clock_ms;
static uint32_t cur_day;

// Triggers
static uint32_t minute_ms, minute_err0;
static bool block_loud[6];
static uint8_t freezes_hour;
static bool t4_pending;
static uint32_t lock_check_ms;          // a reset saw the busy state: look again after it should have cleared
static uint8_t t4_before;
static uint32_t pause_until_ms;
static bool paused;

// Scan
static ScanData scan;
static bool have_scan, in_scan;
static uint32_t scan_ms;
enum ScanState : uint8_t { SCAN_IDLE, SCAN_WAIT_A, SCAN_BREAK, SCAN_WAIT_B };
enum JobFor : uint8_t { FOR_NONE, FOR_RFSCAN, FOR_DIAG, FOR_PROBE, FOR_T1 };
static ScanState scan_state;
static uint8_t scan_for;
static uint32_t scan_job_ms;
static ModRadioValues scan_ch;
static ModReplyTarget scan_target;
static uint32_t scan_recv0, scan_err0;
static uint32_t burst_recv0, burst_err0;
static TrafficCheck burst_traffic;

// RSSI burst
static bool burst_active;
static uint8_t burst_for;               // FOR_PROBE, or the trigger that asked
static uint32_t burst_start_ms, burst_next_ms;
static DbmHistogram burst_histo;
static int8_t burst_min, burst_max;
static uint16_t burst_packets;          // reads that found a packet arriving
static ModReplyTarget burst_target;

// LNA study, allocated only while one runs
struct StudyRun {
  LnaStudy study;
  LnaBlock blocks[2];
  int8_t floor_buf[FLOOR_SAMPLES];
  uint8_t floor_count;
  uint8_t cur_state, first_state, pair_pos, pause_count;
  bool paused;
  uint32_t block_started_ms, block_len_ms, last_floor_ms, paused_at_ms, tick_ms, tick_rem_ms, last_save_s;
  uint32_t seen[SEEN_SLOTS];
  uint8_t seen_pos;
};
static StudyRun* run;
static uint8_t self_key[PUB_KEY_SIZE];
static bool have_key;
static uint32_t reset_armed_ms;
static bool reboot_unstamped;           // a restart recorded at boot before the clock was set
static uint32_t block_recv0, block_err0;
static char probe_ping_id[8];           // diag probe <id> waiting on core's quiet ping
static uint32_t probe_ping_started;
static ModReplyTarget probe_target;
static DiagNote note;

// LNA auto: the paired ping test in progress, and the state auto last set.
enum AutoStep : uint8_t { AUTO_IDLE, AUTO_PING_A, AUTO_PING_B };
static AutoStep auto_step;
static uint8_t auto_block;
static bool auto_first_on;              // the state the first ping ran in
static uint32_t auto_ping_ms;
static char auto_target[8];
static AutoTest auto_test;
static uint32_t auto_last_check_ms;
static int8_t auto_set = -1;            // the state auto applied last; -1 = none
static uint32_t auto_hold_until;        // another feature moved the LNA: leave it until then
static uint8_t auto_rotate;
static DiagEvidence evidence;

// ---------------------------------------------------------------- storage

static bool ensureMounted() {
  if (!mounted) mounted = ModFS.begin(true);
  return mounted;
}

static bool readFile(const char* path, uint32_t magic, void* data, size_t len) {
  if (!ensureMounted() || !ModFS.exists(path)) return false;
  File f = ModFS.open(path, "r");
  if (!f) return false;
  uint32_t got = 0, size = 0;
  bool ok = f.read((uint8_t*)&got, 4) == 4 && got == magic && f.read((uint8_t*)&size, 4) == 4 && size == len
            && f.read((uint8_t*)data, len) == (int)len;
  f.close();
  return ok;
}

static void writeFile(const char* path, uint32_t magic, const void* data, size_t len) {
  if (!ensureMounted()) return;
  File f = ModFS.open(path, "w", true);
  if (!f) return;
  uint32_t size = len;
  f.write((const uint8_t*)&magic, 4);
  f.write((const uint8_t*)&size, 4);
  f.write((const uint8_t*)data, len);
  f.close();
}

static void saveStore() {
  if (modClockGet() >= CLOCK_VALID_EPOCH) store.last_seen = modClockGet();
  writeFile(STORE_PATH, STORE_MAGIC, &store, sizeof(store));
}

// diag-report's files are not read: the new store starts fresh, and their space is returned.
static void purgeLegacy() {
  if (!ensureMounted()) return;
  modTzHours();                         // core copies diag-report's offset before its file goes
  for (const char* path : LEGACY_FILES) {
    if (ModFS.exists(path)) ModFS.remove(path);
  }
}

// ---------------------------------------------------------------- readings

static uint32_t clockHour() {
  uint32_t now = modClockGet();
  return now >= CLOCK_VALID_EPOCH ? now / 3600 : 0;
}

static uint8_t currentSf() {
  ModRadioValues rv;
  return modRadioPrefsGet(&rv) ? rv.sf : 7;
}

static uint8_t lnaNow() {
  if (!modFemLnaAvailable()) return STORE_LNA_NA;
  return modFemLnaGet() ? STORE_LNA_ON : STORE_LNA_OFF;
}

// The state the node runs in outside a study; noise hours are compared only within one state.
static uint8_t lnaSettled() {
  if (!modFemLnaAvailable()) return STORE_LNA_NA;
  return run ? run->study.start_lna : lnaNow();
}

static bool readFloor(int8_t* out_dbm) {
  float rssi;
  if (!modRadioRssiNow(&rssi)) return false;
  if (rssi < -127) rssi = -127;
  if (rssi > 0) rssi = 0;
  *out_dbm = (int8_t)lroundf(rssi);
  return true;
}

static uint8_t agcInterval() {
  ModPolicyValues p;
  return modPolicyRead(&p) ? p.agc_reset_interval : 1;
}

static bool liveChannel(ModRadioValues* ch) {
  if (modTempRadioGet(&ch->freq, &ch->bw, &ch->sf, &ch->cr)) return true;
  return modRadioPrefsGet(ch);
}

static bool calibrated() {
#ifdef MOBMESH_HAS_BATTERY_MEASUREMENT
  return true;
#else
  return false;
#endif
}

static uint8_t lastHourLoad() {
  const StoreHour* h = storeHourAt(store, clockHour() - 1);
  return h ? h->load_pct : 0;
}

static float weakestMargin(char* id, size_t len) {
  float margin = NOISE_NO_MARGIN;
  if (!linkWeakest(links, currentSf(), millis(), &margin, id, len)) margin = NOISE_NO_MARGIN;
  return margin;
}

static NoiseView noiseView(char* weakest, size_t len) {
  float margin = weakestMargin(weakest, len);
  NoiseView v = noiseAnalyse(store, clockHour(), modTzHours(), lnaSettled(), margin);
  // A capture in the loud hours that saw packets, or twice the packets of quiet hours, makes them busy rather than noisy.
  const StoreCapture& c = store.noise_capture;
  bool caught_packets = c.at && modClockGet() - c.at < 86400UL && c.kind == 0;
  bool caught_carrier = c.at && modClockGet() - c.at < 86400UL && c.kind == 1;
  if (caught_packets || (v.mesh_known && v.mesh_busy && !caught_carrier)) noiseMarkBusy(v);
  return v;
}

static bool readStudy(LnaStudy* s) {
  return readFile(STUDY_PATH, LNA_STUDY_MAGIC, s, sizeof(LnaStudy));
}

// The shared evidence every report and the engine reason from.
static void buildEvidence(const NoiseView& nv) {
  static LnaFacts facts;
  bool have_facts = false;
  if (modFemLnaAvailable() && !run) {
    LnaStudy* s = (LnaStudy*)malloc(sizeof(LnaStudy));
    if (s && readStudy(s)) {
      facts = lnaFacts(*s, currentSf());
      have_facts = facts.known;
    }
    free(s);
  }
  EvidenceSources src;
  memset(&src, 0, sizeof(src));
  src.store = &store;
  src.links = &links;
  src.noise = &nv;
  src.now_hour = clockHour();
  src.now_ms = millis();
  src.tz = modTzHours();
  src.sf = currentSf();
  src.temp_now = modMcuTemperature();
  src.lna_available = modFemLnaAvailable();
  src.lna_on = src.lna_available && modFemLnaGet();
  src.lna = have_facts ? &facts : nullptr;
  uint32_t age = 0;
  const ProbeResult* ping = coreProbeLast(&age);
  if (ping && ping->received && ping->hops == 1 && age < 86400000UL) {
    src.ping_ok = true;
    src.ping_id = ping->width >= 2 ? (uint16_t)((ping->hash[0][0] << 8) | ping->hash[0][1]) : ping->hash[0][0];
    src.ping_out_db = ping->out_q[0] / 4.0f;
    src.ping_back_db = ping->back_q[0] / 4.0f;
  }
  evidenceBuild(evidence, src);
}

// ---------------------------------------------------------------- output

// One row goes back as the reply; more go out through the shim's extra-reply queue.
static void emit(const ModCliContext& context, char* reply) {
  char row[DIAG_ROW_MAX + 1];
  uint16_t pos = 0;
  int rows = outRowCount(out);
  if (rows <= 1) {
    if (!outRow(out, &pos, row)) row[0] = 0;
    strcpy(reply, row);
    return;
  }
  for (int i = 0; i < rows && i < DIAG_ROWS_MAX && outRow(out, &pos, row); i++) modCliAddRow(context.sender_timestamp, row);
  reply[0] = 0;
}

// The same rows, sent after the command has returned.
static void emitLate(const ModReplyTarget& target) {
  static char rows[DIAG_ROWS_MAX][DIAG_ROW_MAX + 1];
  uint16_t pos = 0;
  int n = 0;
  while (n < DIAG_ROWS_MAX && outRow(out, &pos, rows[n])) n++;
  modReplyLate(target, &rows[0][0], DIAG_ROW_MAX + 1, n);
}

static void lateLine(const ModReplyTarget& target, const char* text) {
  outReset(out);
  outLine(out, "%s", text);
  emitLate(target);
}

// ---------------------------------------------------------------- the engine's view

static const ProbeResult* recentPing() {
  uint32_t age = 0;
  const ProbeResult* r = coreProbeLast(&age);
  return r && r->received && r->hops == 1 && age < 86400000UL ? r : nullptr;
}

static bool lockedNow(const uint8_t state[2]);

// direct-retry's counters when the build carries it; without it, or with it off, nothing shows.
static bool retryCounts(uint32_t* retried, uint32_t* won) {
#ifdef WITH_DIRECT_RETRY
  return directRetryTotals(retried, won);
#else
  (void)retried;
  (void)won;
  return false;
#endif
}

static bool retryOn() {
  uint32_t r, w;
  return retryCounts(&r, &w);
}

static int retryHops(LinkRetryHop* hops) {
#ifdef WITH_DIRECT_RETRY
  DrHop raw[LINK_RETRY_HOPS];
  int n = directRetryHopRecords(raw, LINK_RETRY_HOPS);
  for (int i = 0; i < n; i++) {
    hops[i].id = raw[i].len == 2 ? (uint16_t)((raw[i].id[0] << 8) | raw[i].id[1]) : raw[i].id[0];
    hops[i].id_len = raw[i].len;
    hops[i].retried = raw[i].retried;
    hops[i].won = raw[i].won;
  }
  return n;
#else
  (void)hops;
  return 0;
#endif
}

// The counters restart at boot: the first sample after it counts everything since boot.
static uint32_t retry_last_r, retry_last_w;
static bool retry_synced;

static void retrySample(uint32_t day) {
  uint32_t r, w;
  if (!retryCounts(&r, &w)) return;
  uint32_t dr = retry_synced && r >= retry_last_r ? r - retry_last_r : r;
  uint32_t dw = retry_synced && w >= retry_last_w ? w - retry_last_w : w;
  storeRetryAdd(store, day, dr, dw);
  retry_last_r = r;
  retry_last_w = w;
  retry_synced = true;
}

static void buildInput(EngineInput& in, NoiseView& nv, BattView& bv, StoreDay* hist_unused) {
  (void)hist_unused;
  memset(&in, 0, sizeof(in));
  LinkRetryHop hops[LINK_RETRY_HOPS];
  int hop_n = retryHops(hops), hop = -1;
  in.retry_one_way = (uint8_t)(linkRetryOneWay(links, hops, hop_n, millis(), &hop) + 1);
  if (hop >= 0) {
    in.retry_tried = hops[hop].retried;
    in.retry_won = hops[hop].won;
  }
  char weakest[8];
  nv = noiseView(weakest, sizeof(weakest));
  uint32_t hour = clockHour();
  int8_t tz = modTzHours();
  bv = battAnalyse(store, hour, tz, calibrated());
  in.store = &store;
  in.links = &links;
  in.noise = &nv;
  in.batt = &bv;
  in.now_hour = hour;
  in.now_ms = millis();
  in.uptime_s = millis() / 1000;
  in.tz = tz;
  in.sf = currentSf();
  in.clock_set = hour != 0;
  in.batt_present = batt_zeros < BATT_ABSENT_READS && modBoardBattMilliVolts() != 0;
  in.lna_available = modFemLnaAvailable();
  in.lna_on = in.lna_available && modFemLnaGet();
  in.radio_sx126x = modRadioScanAvailable();
  in.have_scan = have_scan;
  if (have_scan) {
    in.scan = scanAnalyse(scan);
    in.scan_age_min = (millis() - scan_ms) / 60000UL;
  }
  in.hist_kind = histAnalyse(store, calibrated()).kind;
  float now_c = modMcuTemperature();
  in.has_temp = !isnan(now_c);
  in.temp_now = in.has_temp ? (int8_t)lroundf(now_c) : 0;
  in.temp_kind = tempAnalyse(store, hour != 0, now_c).kind;

  // A study in progress lives in RAM; a finished one is read from its file.
  LnaStudy* finished = nullptr;
  if (run) {
    in.study_active = true;
    in.study_h = (uint16_t)(run->study.elapsed_s / 3600);
    in.study_of_h = (uint16_t)(run->study.requested_s / 3600);
  } else if (in.lna_available) {
    finished = (LnaStudy*)malloc(sizeof(LnaStudy));
    if (finished && readFile(STUDY_PATH, LNA_STUDY_MAGIC, finished, sizeof(LnaStudy))) {
      in.study_result = finished->result;
      in.study_percent_on = (int16_t)lnaPercentMore(*finished, LNA_ON);
      if (lnaFloorPairs(*finished) >= 3) in.study_floor_shift = (int8_t)lroundf(lnaFloorShift(*finished));
    }
    free(finished);
  }

  uint32_t recv, errors;
  modRadioCounters(&recv, &errors);
  uint32_t day_recv, day_errors;
  if (hour && noiseCounts(store, hour, &day_recv, &day_errors) && counters_start && recv >= recv0 && errors >= err0) {
    in.recv = day_recv + recv - recv0;
    in.errors = day_errors + errors - err0;
  } else {
    in.recv = recv;
    in.errors = errors;
  }
  in.air_s = modAirtimeTotalSecs();
  in.agc_interval = agcInterval();
  uint8_t guard, state[2];
  if (modRadioGuardRead(&guard, state)) {
    in.guard_known = true;
    in.guard_on = guard & 1;
    in.frozen_now = lockedNow(state);
  }
  in.auto_state = store.auto_off ? ENGINE_AUTO_OFF : paused ? ENGINE_AUTO_PAUSED : ENGINE_AUTO_ON;
  if (paused) in.pause_min = (uint16_t)((pause_until_ms - millis()) / 60000UL + 1);
  const ProbeResult* ping = recentPing();
  if (ping) in.cleared_one_way = ping->width >= 2 ? (uint16_t)((ping->hash[0][0] << 8) | ping->hash[0][1]) : ping->hash[0][0];
  buildEvidence(nv);
  in.ev = &evidence;
  static char auto_text[48];
  if (in.lna_available && store.lna_auto.enabled) {
    autoStatusText(store.lna_auto, tz, auto_text, sizeof(auto_text));
    in.lna_auto = auto_text;
    in.lna_auto_dead = store.lna_auto.park == AUTO_PARK_DEAD;
  }
}

// ---------------------------------------------------------------- the hour and the day

// Local hours of the last 24 the LNA was on, one bit per hour.
static uint32_t lnaOnHours() {
  uint32_t mask = 0, now = clockHour();
  for (uint32_t h = now - 24; now && h < now; h++) {
    const StoreHour* s = storeHourAt(store, h);
    if (s && s->lna == STORE_LNA_ON) mask |= 1UL << storeLocalHour(h, modTzHours());
  }
  return mask;
}

// Yesterday's leading cause goes into the recurrence ring, so a verdict can say how many days it held.
static void recordDay(uint32_t day) {
  EngineInput in;
  NoiseView nv;
  BattView bv;
  buildInput(in, nv, bv, nullptr);
  // The finished day's loud hours and each neighbour's level go into the long history.
  storeLoudDay(store, day, nv.loud_mask);
  storeNbTurn(store, day);
  for (int i = 0; i < links.count; i++) {
    const LinkNeighbour& nb = links.nb[i];
    if (nb.packets >= LINKS_MIN_PKTS && nb.heard && millis() - nb.last_ms < LINKS_STALE_MS) storeNbDay(store, day, nb.id, nb.id_len, (int8_t)lroundf(nb.snr_db));
  }
  EngineResult* r = (EngineResult*)malloc(sizeof(EngineResult));
  if (r == nullptr) return;
  engineRun(in, *r);
  storeRecurSet(store, day, r->cause, r->cause_hours);
  // The overload verdict bans the LNA in the hours it was on with failed decodes or freezes; else every hour it was on, else all day.
  if (r->cause == CAUSE_LNA_OVERLOAD && store.lna_auto.enabled) {
    uint32_t on = lnaOnHours(), hours = r->cause_hours & on;
    autoBan(store.lna_auto, hours ? hours : on ? on : 0xFFFFFFUL, true, AUTO_R_OVERLOAD);
  }
  free(r);
  // LNA auto's daily turn: fade, retire, park; pingable neighbours are the judged ones.
  if (store.lna_auto.enabled) {
    uint8_t pingable = 0;
    for (int i = 0; i < links.count; i++) {
      if (links.nb[i].packets >= LINKS_MIN_PKTS && millis() - links.nb[i].last_ms < LINKS_STALE_MS) pingable++;
    }
    autoDayTurn(store.lna_auto, day + 1, pingable);
  }
}

static void startHour(uint32_t hour) {
  cur_hour = hour;
  histoReset(histo);
  histo_lna = lnaSettled();
  modRadioCounters(&recv0, &err0);
  air0 = modAirtimeTotalSecs();
  counters_start = true;
  freezes_hour = 0;
  if (hour) linkRollHour(links, (uint8_t)(hour % 24));
  else linkRollHour(links, 0xFF);
}

static void closeHour() {
  uint32_t recv, errors;
  modRadioCounters(&recv, &errors);
  bool valid = counters_start && recv >= recv0 && errors >= err0;
  int8_t floor_dbm = histo.count >= NOISE_MIN_SAMPLES ? histoMedian(histo) : STORE_NO_FLOOR;
  storeNoiseHour(store, cur_hour, floor_dbm, histo_lna, valid, valid ? recv - recv0 : 0, valid ? errors - err0 : 0);
  uint32_t air = modAirtimeTotalSecs();
  uint8_t load = (uint8_t)(air >= air0 ? (air - air0) * 100 / 3600 : 0);
  storeLoadHour(store, cur_hour, modTzHours(), load);
  // The site's usual for this hour learns from settled hours only: not frozen, not in the other LNA state.
  const StoreHour* h = storeHourAt(store, cur_hour);
  if (h && !h->freezes && histo_lna == lnaSettled()) {
    storeBaseHour(store, storeLocalHour(cur_hour, modTzHours()), floor_dbm, valid, valid ? recv - recv0 : 0, valid ? errors - err0 : 0, load);
  }
  if (clockHour()) retrySample(storeLocalDay(cur_hour, modTzHours()));
  // LNA auto's record: the closed hour counts for its block in the state it ran, then the audit checks that state.
  if (store.lna_auto.enabled && modFemLnaAvailable() && valid && !run && histo_lna != STORE_LNA_NA && lnaNow() == histo_lna) {
    uint8_t heard = 0, bit = (uint8_t)(cur_hour % 24);
    for (int i = 0; i < links.count; i++) heard += (links.nb[i].heard >> bit) & 1;
    uint8_t block = autoBlockOf(storeLocalHour(cur_hour, modTzHours()));
    autoAddHour(store.lna_auto, block, histo_lna == STORE_LNA_ON, recv - recv0, errors - err0, h ? h->freezes : 0, heard, floor_dbm);
    autoAudit(store.lna_auto, block);
  }
}

// A clock set or jump closes nothing: that hour's epoch is not the one it was sampled in.
static void hourTick() {
  uint32_t hour = clockHour();
  if (reboot_unstamped && hour) {
    store.reboot[0].at = modClockGet() - millis() / 1000;
    reboot_unstamped = false;
    saveStore();
  }
  if (hour == cur_hour) return;
  bool next = cur_hour && hour == cur_hour + 1;
  if (next) closeHour();
  uint32_t day = hour ? storeLocalDay(hour, modTzHours()) : 0;
  if (next && cur_day && day == cur_day + 1) recordDay(cur_day);
  cur_day = day;
  for (int b = 0; b < 6; b++) block_loud[b] = block_loud[b] && hour && (hour % 24) / 4 == (uint32_t)b;
  startHour(hour);
  if (next) saveStore();
}

// A set or stepped clock moves the neighbours' advert offsets with it.
static void clockTick(uint32_t now_ms) {
  uint32_t clock = modClockGet();
  if (last_clock_ms) {
    int64_t expected = (int64_t)last_clock + (now_ms - last_clock_ms) / 1000;
    int64_t delta = (int64_t)clock - expected;
    if (delta > CLOCK_JUMP_S || delta < -CLOCK_JUMP_S) linkClockShift(links, (int32_t)delta);
  }
  last_clock = clock;
  last_clock_ms = now_ms - (now_ms - last_clock_ms) % 1000;
  if (!last_clock_ms) last_clock_ms = now_ms;
}

// ---------------------------------------------------------------- automatic captures

static SchedConditions conditions(bool for_scan) {
  SchedConditions c;
  memset(&c, 0, sizeof(c));
  c.epoch = clockHour() ? modClockGet() : 0;
  c.local_day = c.epoch ? storeLocalDay(c.epoch / 3600, modTzHours()) : 0;
  c.auto_off = store.auto_off;
  c.paused = paused;
  float rssi;
  c.packet_arriving = !modRadioRssiNow(&rssi);
  c.batt_mv = batt_zeros < BATT_ABSENT_READS ? modBoardBattMilliVolts() : 0;
  c.load_pct = lastHourLoad();
  c.scan_age_s = have_scan ? (millis() - scan_ms) / 1000 : 0xFFFFFFFF;
  (void)for_scan;
  return c;
}

static void startBurst(uint8_t for_what, const ModReplyTarget* target) {
  burst_active = true;
  burst_for = for_what;
  burst_start_ms = burst_next_ms = millis();
  histoReset(burst_histo);
  burst_min = 0;
  burst_max = -128;
  burst_packets = 0;
  memset(&burst_traffic, 0, sizeof(burst_traffic));
  modRadioCounters(&burst_recv0, &burst_err0);
  if (target) burst_target = *target;
}

static bool startScan(uint8_t for_what, const ModReplyTarget* target);

static void tryTrigger(uint8_t trigger) {
  if (burst_active || scan_state != SCAN_IDLE) return;
  SchedConditions c = conditions(trigger == TRIG_FREEZES);
  if (!schedAllow(store, trigger, c, nullptr)) return;
  if (trigger == TRIG_FREEZES) {
    if (!startScan(FOR_T1, nullptr)) return;
  } else {
    startBurst(trigger == TRIG_LOUD ? 0x80 | TRIG_LOUD : 0x80 | TRIG_DECODES, nullptr);
  }
  schedUse(store, trigger, c);
  // Captures are rare: save at once so the day's caps and findings survive a restart.
  saveStore();
}

// T2: the first loud sample of a 4-hour block, against the quietest finished hour in the same LNA state.
static void loudCheck(int8_t dbm) {
  uint32_t hour = clockHour();
  if (!hour) return;
  int block = (hour + 24 + modTzHours()) % 24 / 4;
  if (block_loud[block]) return;
  int8_t floor_dbm = 0;
  bool any = false;
  for (int i = 0; i < STORE_HOURS; i++) {
    const StoreHour& h = store.h[i];
    if (h.hour == 0 || h.hour >= hour || hour - h.hour > 24 || h.floor_dbm == STORE_NO_FLOOR || h.lna != histo_lna || h.freezes) continue;
    if (!any || h.floor_dbm < floor_dbm) floor_dbm = h.floor_dbm;
    any = true;
  }
  if (!any || dbm < floor_dbm + LOUD_T2_DB) return;
  block_loud[block] = true;
  tryTrigger(TRIG_LOUD);
}

// T3: three or more failed decodes inside a minute.
static void decodeCheck(uint32_t now_ms) {
  if (now_ms - minute_ms < 60000UL) return;
  uint32_t recv, errors;
  modRadioCounters(&recv, &errors);
  if (minute_ms && schedDecodeSpike(minute_err0, errors)) tryTrigger(TRIG_DECODES);
  minute_ms = now_ms;
  minute_err0 = errors;
}

// ---------------------------------------------------------------- guard registers

static int8_t quietFloor();

// The busy state with nothing on air: a locked receiver. With a strong signal present it is only busy.
static bool lockedNow(const uint8_t state[2]) {
  if (!(state[0] == 0x08 && state[1] == 0x12)) return false;
  int8_t dbm = 0, floor_dbm = quietFloor();
  if (!readFloor(&dbm)) return false;
  return floor_dbm == STORE_NO_FLOOR ? false : dbm < floor_dbm + LOUD_T2_DB;
}

static void freezeSeen() {
  uint32_t epoch = clockHour() ? modClockGet() : 0;
  if (store.lna_auto.enabled && modFemLnaAvailable() && modFemLnaGet() && clockHour()) autoAddFreeze(store.lna_auto, autoBlockOf(storeLocalHour(clockHour(), modTzHours())));
  storeFreeze(store, epoch, modTzHours());
  if (run) {
    uint16_t& n = run->study.freezes[run->cur_state];
    if (n < 0xFFFF) n++;
  }
  if (++freezes_hour >= FREEZE_T1_COUNT) tryTrigger(TRIG_FREEZES);
}

// Called by the radio wrapper just before each AGC reset; a scan's own resets are not the receiver's doing.
void diagReportsAgcReset() {
  if (in_scan) return;
  uint8_t guard, state[2];
  if (!modRadioGuardRead(&guard, state)) return;
  // The busy state also shows while a strong signal is simply on air; only one that outlasts it is a lock.
  if (state[0] == 0x08 && state[1] == 0x12 && !lock_check_ms) lock_check_ms = (millis() + 2000) | 1;
  if (!(guard & 1)) {
    t4_pending = true;
    t4_before = guard;
  }
}

static int8_t quietFloor();

// Two seconds after a reset found the busy state: still busy with the channel at its floor is a lock; otherwise a strong signal.
static void lockCheck(uint32_t now_ms) {
  if (!lock_check_ms || (int32_t)(now_ms - lock_check_ms) < 0) return;
  lock_check_ms = 0;
  uint8_t guard, state[2];
  if (!modRadioGuardRead(&guard, state)) return;
  bool busy = state[0] == 0x08 && state[1] == 0x12;
  int8_t dbm = 0, floor_dbm = quietFloor();
  bool heard = readFloor(&dbm);
  bool raised = !heard || (floor_dbm != STORE_NO_FLOOR && dbm >= floor_dbm + LOUD_T2_DB);
  if (busy && !raised) freezeSeen();
  else storeStrong(store, clockHour() ? modClockGet() : 0);
}

// T4: read the guard again after the reset that should have restored it.
static void guardAfterReset() {
  if (!t4_pending) return;
  t4_pending = false;
  uint8_t guard = 0, state[2];
  if (!modRadioGuardRead(&guard, state)) return;
  storeGuardOff(store, clockHour() ? modClockGet() : 0, t4_before, guard);
}

// ---------------------------------------------------------------- samplers

static void noiseTick(uint32_t now_ms) {
  if (now_ms - last_noise_ms < NOISE_EVERY_MS) return;
  last_noise_ms = now_ms;
  hourTick();
  // With the AGC reset off nothing else reads the guard, so the noise sample does.
  if (agcInterval() == 0) {
    uint8_t guard, state[2];
    if (modRadioGuardRead(&guard, state) && state[0] == 0x08 && state[1] == 0x12 && !lock_check_ms) lock_check_ms = (millis() + 2000) | 1;
  }
  if (!cur_hour || in_scan) return;
  if (skip_samples) {
    skip_samples--;
    return;
  }
  int8_t dbm;
  if (lnaNow() != histo_lna || !readFloor(&dbm)) return;
  histoAdd(histo, dbm);
  loudCheck(dbm);
}

static void battTick(uint32_t now_ms) {
  uint32_t hour = clockHour();
  if (!hour) return;
  if (last_batt_ms && now_ms - last_batt_ms < BATT_EVERY_MS) return;
  last_batt_ms = now_ms | 1;
  uint16_t mv = batt_zeros >= BATT_ABSENT_READS ? 0 : modBoardBattMilliVolts();
  // Only timed readings count, so asking for a report repeatedly cannot switch the watch off.
  if (batt_zeros < BATT_ABSENT_READS) batt_zeros = mv ? 0 : batt_zeros + 1;
  storeSample(store, hour, modTzHours(), mv, modMcuTemperature());
}

// ---------------------------------------------------------------- scan job

static void scanChunk(int first_sweep) {
  in_scan = true;
  modRadioScanEnter(scan.bw_khz);
  if (first_sweep == 0) {
    modRadioScanCalibrate(scanBandFreq(scan_ch.freq, SCAN_BAND_STEPS / 2));
    for (int b = 0; b < SCAN_BAND_STEPS; b++) scan.band[b] = scanPack(modRadioScanRead(scanBandFreq(scan_ch.freq, b)));
  }
  modRadioScanCalibrate(scan_ch.freq);
  for (int sw = first_sweep; sw < first_sweep + SCAN_SWEEPS / 2; sw++) {
    for (int b = 0; b < scan.steps; b++) scan.raw[sw][b] = scanPack(modRadioScanRead(scanStepFreq(scan, b)));
    scan.sweeps = (uint8_t)(sw + 1);
  }
  modRadioScanLeave(scan_ch.freq, scan_ch.bw, scan_ch.sf, scan_ch.cr);
  in_scan = false;
  skip_samples = AFTER_SCAN_SKIP;
}

static bool startScan(uint8_t for_what, const ModReplyTarget* target) {
  if (!modRadioScanAvailable() || scan_state != SCAN_IDLE) return false;
  ModRadioValues ch;
  if (!liveChannel(&ch)) return false;
  scan_ch = ch;
  scan_for = for_what;
  if (target) scan_target = *target;
  memset(&scan, 0, sizeof(scan));
  // The manual scan shows the wide chart; automatic ones only need the channel and the steps beside it.
  if (for_what == FOR_RFSCAN) scanSetWide(scan, ch.freq);
  else scanSetNarrow(scan, ch.freq, ch.bw);
  modRadioCounters(&scan_recv0, &scan_err0);
  scan.lna = modFemLnaAvailable() ? (modFemLnaGet() ? 1 : 0) : SCAN_LNA_NA;
  scan_state = SCAN_WAIT_A;
  scan_job_ms = millis();
  return true;
}

static void runSummary(DiagOut& o);

static void scanDone() {
  have_scan = true;
  scan_ms = millis();
  uint32_t recv, errors;
  modRadioCounters(&recv, &errors);
  scan.traffic.decoded = (uint16_t)(recv >= scan_recv0 ? recv - scan_recv0 : 0);
  scan.traffic.errors = (uint16_t)(errors >= scan_err0 ? errors - scan_err0 : 0);
  ScanFinding f = scanAnalyse(scan);
  storeLoraSeen(store, clockHour() ? modClockGet() : 0, f.traffic == TRAFFIC_FOREIGN, f.traffic == TRAFFIC_OURS);
  store.scan = {clockHour() ? modClockGet() : 0, f.kind, f.level, f.offset_khz, f.db, f.floor_dbm, (uint8_t)f.blocker, f.blocker_khz, f.blocker_db, f.traffic};
  if (scan_for == FOR_T1 || f.blocker) {
    // A finding is only kept as a capture when it could explain a freeze.
    if (f.blocker) store.scan_capture = {modClockGet(), f.blocker_khz, f.blocker_db, (int8_t)(f.floor_dbm + f.blocker_db), 0, 1};
    else if (f.kind == SCAN_K_MESH_STEADY) store.scan_capture = {modClockGet(), 0, f.db, (int8_t)(f.floor_dbm + f.db), 0, 2};
    else if (scan_for == FOR_T1) store.scan_capture = {modClockGet(), 0, 0, 0, 0, 0};
  }
  outReset(out);
  if (scan_for == FOR_RFSCAN) {
    char weakest[8];
    NoiseView nv = noiseView(weakest, sizeof(weakest));
    buildEvidence(nv);
    relateScan(evidence, note);
    scanFormat(scan, &note, out);
  }
  else if (scan_for == FOR_DIAG) runSummary(out);
  else if (scan_for == FOR_PROBE) {
    char text[40];
    int level = scanSummary(f, text, sizeof(text));
    outLine(out, "%s %s", diagOrb((uint8_t)level), text);
    if (f.traffic != TRAFFIC_UNKNOWN) outLine(out, "Channel: %s", trafficText(f.traffic));
    outSep(out);
    outLine(out, level == DIAG_GOOD ? "Nothing near the channel now." : "Confirmed: live now.");
    outLine(out, "See: diag radio.rfscan");
  }
  if (scan_for != FOR_T1) emitLate(scan_target);
  scan_for = FOR_NONE;
  saveStore();
}

// Two chunks of about 1.3 s off channel, with the mesh heard in between.
static void scanTick(uint32_t now_ms) {
  float rssi;
  switch (scan_state) {
    case SCAN_WAIT_A:
    case SCAN_WAIT_B:
      // Never leave the channel mid-packet; give up when the channel never goes quiet.
      if (!modRadioRssiNow(&rssi)) {
        if (now_ms - scan_job_ms > SCAN_START_WAIT_MS) {
          if (scan_for != FOR_T1) lateLine(scan_target, "ERR: radio busy, try again");
          scan_state = SCAN_IDLE;
        }
        return;
      }
      if (scan_state == SCAN_WAIT_A) {
        scanChunk(0);
        scan_state = SCAN_BREAK;
        scan_job_ms = millis();
      } else {
        scanChunk(SCAN_SWEEPS / 2);
        scan_state = SCAN_IDLE;
        scanDone();
      }
      return;
    case SCAN_BREAK:
      // Back on the channel between the halves: watch for preambles while the decoder listens.
      trafficPeek(scan.traffic, modRadioIrqPeek());
      if (now_ms - scan_job_ms < SCAN_BREAK_MS) return;
      scan_state = SCAN_WAIT_B;
      scan_job_ms = now_ms;
      return;
    default:
      return;
  }
}

// ---------------------------------------------------------------- burst job

// The quietest finished hour in the current LNA state, else this hour so far, else the last scan's floor.
static int8_t quietFloor() {
  uint32_t hour = clockHour();
  int8_t best = STORE_NO_FLOOR;
  for (int i = 0; hour && i < STORE_HOURS; i++) {
    const StoreHour& h = store.h[i];
    if (h.hour == 0 || h.hour >= hour || hour - h.hour > 24 || h.floor_dbm == STORE_NO_FLOOR || h.lna != histo_lna || h.freezes) continue;
    if (best == STORE_NO_FLOOR || h.floor_dbm < best) best = h.floor_dbm;
  }
  if (best == STORE_NO_FLOOR && histo.count >= 4) best = histoMedian(histo);
  if (best == STORE_NO_FLOOR && have_scan) best = scanAnalyse(scan).floor_dbm;
  return best;
}

static void burstTick(uint32_t now_ms) {
  if (!burst_active || now_ms - burst_next_ms < BURST_EVERY_MS) return;
  burst_next_ms = now_ms;
  int8_t dbm;
  trafficPeek(burst_traffic, modRadioIrqPeek());
  if (readFloor(&dbm)) {
    histoAdd(burst_histo, dbm);
    if (dbm < burst_min || burst_histo.count == 1) burst_min = dbm;
    if (dbm > burst_max) burst_max = dbm;
  } else {
    // The radio is taking a packet: that moment is traffic, not a gap in the reading.
    burst_packets++;
  }
  if (now_ms - burst_start_ms < BURST_MS) return;
  burst_active = false;
  if (burst_histo.count + burst_packets < 10) return;
  // A carrier holds its level well above the floor; packets come and go; a quiet channel sits at the floor.
  int8_t floor_dbm = quietFloor();
  int8_t median = burst_histo.count ? histoMedian(burst_histo) : floor_dbm;
  if (!burst_histo.count) burst_min = burst_max = median;
  bool known = floor_dbm != STORE_NO_FLOOR;
  bool raised = known && median >= floor_dbm + LOUD_T2_DB;
  // Steady means most readings sit within 3 dB of the median; packets spend most of the time at the floor.
  uint16_t near = 0;
  for (int d = -3; d <= 3; d++) {
    int bin = -(median + d);
    if (bin >= 0 && bin < 128) near += burst_histo.bin[bin];
  }
  uint8_t steady = raised && burst_packets * 5 < burst_histo.count && near * 5 >= burst_histo.count * 4 ? 1 : 0;
  uint32_t recv_now, err_now;
  modRadioCounters(&recv_now, &err_now);
  burst_traffic.decoded = (uint16_t)(recv_now >= burst_recv0 ? recv_now - burst_recv0 : 0);
  burst_traffic.errors = (uint16_t)(err_now >= burst_err0 ? err_now - burst_err0 : 0);
  uint8_t lora = trafficClassify(burst_traffic, raised || burst_packets > 0);
  // Another network's LoRa is neither this mesh's traffic nor a carrier: kept as its own kind.
  storeLoraSeen(store, clockHour() ? modClockGet() : 0, lora == TRAFFIC_FOREIGN, lora == TRAFFIC_OURS);
  if (lora == TRAFFIC_FOREIGN) steady = 3;
  else if (lora == TRAFFIC_NOT_LORA && raised) steady = 1;
  bool traffic = burst_packets >= 3 || lora == TRAFFIC_OURS;
  StoreCapture c = {modClockGet(), 0, burst_min, burst_max, median, steady};
  if (burst_for == (0x80 | TRIG_LOUD)) store.noise_capture = c;
  if (burst_for == (0x80 | TRIG_DECODES)) store.decode_capture = c;
  // A probe's burst is evidence too: in a loud hour it speaks for the noise, otherwise for the failures.
  if (burst_for == FOR_PROBE && (raised || traffic)) {
    char weakest[8];
    NoiseView nv = noiseView(weakest, sizeof(weakest));
    uint8_t local = storeLocalHour(clockHour(), modTzHours());
    if (nv.loud_mask & (1UL << local)) store.noise_capture = c;
    else store.decode_capture = c;
  }
  saveStore();
  if (burst_for != FOR_PROBE) return;
  outReset(out);
  outLine(out, "%d to %d dBm over 2 s, median %d", burst_min, burst_max, median);
  if (burst_packets) outLine(out, "Packets on air %u%% of it", (unsigned)(burst_packets * 100 / (burst_histo.count + burst_packets)));
  if (burst_traffic.peeks) outLine(out, "LoRa %u preambles, %u no header, %u decoded", burst_traffic.preambles, burst_traffic.orphans, burst_traffic.decoded);
  if (floor_dbm != STORE_NO_FLOOR) outLine(out, "Floor %d dBm", floor_dbm);
  outSep(out);
  if (lora == TRAFFIC_FOREIGN) {
    outLine(out, "LoRa that never decodes:");
    outLine(out, "another network on our frequency.");
  } else if (traffic && steady != 1) {
    outLine(out, "Comes and goes: our packets.");
    outLine(out, "See: diag radio.noise");
  } else if (!known) outLine(out, "No floor known yet: try in an hour.");
  else if (!raised && burst_max < floor_dbm + LOUD_T2_DB) outLine(out, "Quiet now: at the floor.");
  else if (steady == 1) {
    outLine(out, "Steady carrier, not LoRa: interference.");
    outLine(out, "See: diag radio.rfscan");
  } else {
    outLine(out, "Comes and goes: packets.");
    outLine(out, "See: diag radio.noise");
  }
  emitLate(burst_target);
}

// ---------------------------------------------------------------- probe ping

// Core's quiet ping has finished when its result is younger than the request.
static void pingTick(uint32_t now_ms) {
  uint32_t age = 0;
  const ProbeResult* r = coreProbeLast(&age);
  bool done = r && age < now_ms - probe_ping_started;
  if (!done && now_ms - probe_ping_started < 20000UL) return;
  outReset(out);
  char id[8];
  snprintf(id, sizeof(id), "%s", probe_ping_id);
  for (char* c = id; *c; c++) if (*c >= 'a' && *c <= 'f') *c -= 32;
  probe_ping_id[0] = 0;
  if (!done || !r->received) {
    outLine(out, "No answer from %s.", id);
    outSep(out);
    outLine(out, "It may not hear us: check");
    outLine(out, "our TX power, or its antenna.");
    emitLate(probe_target);
    return;
  }
  float out_db = r->out_q[0] / 4.0f, back_db = r->back_q[0] / 4.0f;
  outLine(out, "Ping %s: %lu ms", id, (unsigned long)r->rtt_ms);
  outLine(out, "out %+.1f / back %+.1f", out_db, back_db);
  outSep(out);
  if (out_db + 3.0f < back_db) {
    outLine(out, "Their side hears us weakly:");
    outLine(out, "check %s's antenna.", id);
  } else if (back_db + 3.0f < out_db) {
    outLine(out, "We hear it back weakly:");
    outLine(out, "check our antenna and cable.");
  } else {
    outLine(out, "Confirmed: two-way, both ends even.");
  }
  emitLate(probe_target);
}

// ---------------------------------------------------------------- LNA study

static void saveStudy() {
  if (run) writeFile(STUDY_PATH, LNA_STUDY_MAGIC, &run->study, sizeof(LnaStudy));
}

static int studyHour() {
  if (!run->study.clock_valid) return -1;
  return (int)((modClockGet() / 3600UL) % 24);
}

static void beginBlock(uint8_t state, uint32_t extra_ms) {
  run->cur_state = state;
  modRadioCounters(&block_recv0, &block_err0);
  modFemLnaSet(state == LNA_ON);
  run->block_started_ms = millis();
  run->block_len_ms = BLOCK_MIN_MS + modRandom() % BLOCK_SPAN_MS + extra_ms;
  run->floor_count = 0;
}

static void beginPair(uint32_t extra_ms) {
  memset(run->blocks, 0, sizeof(run->blocks));
  run->first_state = modRandom() & 1;
  run->pair_pos = 0;
  beginBlock(run->first_state, extra_ms);
}

static int8_t median8(int8_t* buf, int count) {
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

// A restore is skipped when the operator changed the setting: their value stands.
static void finishStudy(bool restore) {
  if (!run) return;
  run->study.result = lnaVerdict(run->study, currentSf(), true);
  run->study.active = 0;
  if (restore) modFemLnaSet(run->study.start_lna == LNA_ON);
  saveStudy();
  free(run);
  run = nullptr;
}

static void endBlock() {
  uint32_t recv, errors;
  modRadioCounters(&recv, &errors);
  if (recv >= block_recv0 && errors >= block_err0) {
    run->blocks[run->cur_state].recv = (uint16_t)(recv - block_recv0 > 0xFFFF ? 0xFFFF : recv - block_recv0);
    run->blocks[run->cur_state].errors = (uint16_t)(errors - block_err0 > 0xFFFF ? 0xFFFF : errors - block_err0);
  }
  if (run->floor_count) {
    run->blocks[run->cur_state].floor_dbm = median8(run->floor_buf, run->floor_count);
    run->blocks[run->cur_state].floor_valid = true;
  }
  if (run->pair_pos == 0) {
    run->pair_pos = 1;
    beginBlock(run->first_state ^ 1, 0);
    return;
  }
  int day = (int)(run->study.elapsed_s / 86400UL);
  lnaAddPair(run->study, run->blocks[LNA_ON], run->blocks[LNA_OFF], studyHour(), day);
  run->pause_count = 0;
  if (run->study.elapsed_s >= run->study.requested_s || lnaShouldStop(run->study, currentSf())) {
    finishStudy(true);
    return;
  }
  beginPair(0);
}

static void studyTick(uint32_t now_ms) {
  if (!run) return;
  if (run->paused) {
    if (now_ms - run->paused_at_ms < PAUSE_RETRY_MS) return;
    run->paused = false;
    run->tick_ms = now_ms;
    beginPair(0);
    return;
  }
  // Another feature (power-guard's low-battery drop) moved the LNA: the pair is discarded, not mixed.
  if (modFemLnaGet() != (run->cur_state == LNA_ON)) {
    if (++run->pause_count >= PAUSE_LIMIT) {
      finishStudy(false);
      return;
    }
    run->paused = true;
    run->paused_at_ms = now_ms;
    memset(run->blocks, 0, sizeof(run->blocks));
    return;
  }
  run->tick_rem_ms += now_ms - run->tick_ms;
  run->tick_ms = now_ms;
  run->study.elapsed_s += run->tick_rem_ms / 1000;
  run->tick_rem_ms %= 1000;
  if (now_ms - run->last_floor_ms >= FLOOR_EVERY_MS && run->floor_count < FLOOR_SAMPLES) {
    run->last_floor_ms = now_ms;
    if (readFloor(&run->floor_buf[run->floor_count])) run->floor_count++;
  }
  if (now_ms - run->block_started_ms >= run->block_len_ms) endBlock();
  if (run && run->study.elapsed_s - run->last_save_s >= STUDY_SAVE_S) {
    run->last_save_s = run->study.elapsed_s;
    saveStudy();
  }
}

static bool seenBefore(const mesh::Packet* packet) {
  uint8_t hash[MAX_HASH_SIZE];
  packet->calculatePacketHash(hash);
  uint32_t key;
  memcpy(&key, hash, sizeof(key));
  for (int i = 0; i < SEEN_SLOTS; i++) {
    if (run->seen[i] == key) return true;
  }
  run->seen[run->seen_pos] = key;
  run->seen_pos = (run->seen_pos + 1) % SEEN_SLOTS;
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

// ---------------------------------------------------------------- LNA auto

static void autoSet(bool on) {
  if (modFemLnaGet() != on) modFemLnaSet(on);
  auto_set = on ? 1 : 0;
}

// The weakest judged neighbours, rotating among the weakest few so one node does not carry every test.
static bool autoPickTarget(char* out, size_t len) {
  int order[LINKS_MAX], k = 0;
  uint8_t sf = currentSf();
  for (int i = 0; i < links.count; i++) {
    if (links.nb[i].packets >= LINKS_MIN_PKTS && millis() - links.nb[i].last_ms < LINKS_STALE_MS) order[k++] = i;
  }
  if (k == 0) return false;
  for (int i = 1; i < k; i++) {
    int v = order[i], j = i - 1;
    while (j >= 0 && linkMargin(links, order[j], sf) > linkMargin(links, v, sf)) {
      order[j + 1] = order[j];
      j--;
    }
    order[j + 1] = v;
  }
  linkLabel(links, order[auto_rotate++ % (k < AUTO_TARGETS ? k : AUTO_TARGETS)], out, len);
  for (char* c = out; *c; c++) if (*c >= 'A' && *c <= 'F') *c += 32;
  return true;
}

static void autoFloor(int idx) {
  int8_t dbm;
  auto_test.floor_dbm[idx] = readFloor(&dbm) ? dbm : -128;
}

// Core's ping takes the mesh's own hash width: a 2-byte id is cut to 1 byte when the mesh runs 1-byte hashes.
static bool pingQuiet(char* target, char* reply) {
  if (coreProbeQuiet(target, reply)) return true;
  if (strlen(target) != 4 || strstr(reply, "not a hash") == nullptr) return false;
  target[2] = 0;
  return coreProbeQuiet(target, reply);
}

static bool autoPingStart() {
  char reply[160];
  return pingQuiet(auto_target, reply);
}

// Fills one side of the test from core's quiet ping; false while it is still out.
static bool autoPingDone(uint32_t now_ms, int idx) {
  uint32_t age = 0;
  const ProbeResult* r = coreProbeLast(&age);
  bool done = r && age < now_ms - auto_ping_ms;
  if (!done && now_ms - auto_ping_ms < 15000UL) return false;
  auto_test.reply[idx] = done && r->received;
  auto_test.back_db[idx] = done ? r->back_q[0] / 4.0f : 0;
  auto_test.out_db[idx] = done ? r->out_q[0] / 4.0f : 0;
  return true;
}

// Once a minute: run each block in its learned state; a paired ping test when one is due.
static void autoTick(uint32_t now_ms) {
  if (!modFemLnaAvailable() || run) return;
  uint32_t hour = clockHour();
  if (!hour) return;
  uint8_t local_hour = storeLocalHour(hour, modTzHours());
  uint8_t block = autoBlockOf(local_hour);
  uint32_t day = storeLocalDay(hour, modTzHours());
  if (auto_step == AUTO_PING_A && autoPingDone(now_ms, auto_first_on ? 1 : 0)) {
    // Second half: the other state, after its floor reading.
    autoSet(!auto_first_on);
    delay(20);
    autoFloor(auto_first_on ? 0 : 1);
    auto_ping_ms = now_ms;
    auto_step = autoPingStart() ? AUTO_PING_B : AUTO_IDLE;
    if (auto_step == AUTO_IDLE) autoSet(autoApplied(store.lna_auto, auto_block, local_hour));
    return;
  }
  if (auto_step == AUTO_PING_B && autoPingDone(now_ms, auto_first_on ? 0 : 1)) {
    autoAddTest(store.lna_auto, auto_block, auto_test, modClockGet(), day);
    auto_step = AUTO_IDLE;
    autoSet(autoApplied(store.lna_auto, block, local_hour));
    saveStore();
    return;
  }
  if (auto_step != AUTO_IDLE || now_ms - auto_last_check_ms < 60000UL) return;
  auto_last_check_ms = now_ms;
  // Another feature (power-guard's low-battery drop) moved the LNA: it wins, and auto waits.
  if (auto_set >= 0 && modFemLnaGet() != (auto_set == 1)) {
    auto_hold_until = now_ms + 900000UL;
    auto_set = -1;
  }
  if (auto_hold_until && (int32_t)(now_ms - auto_hold_until) < 0) return;
  auto_hold_until = 0;
  bool applied = autoApplied(store.lna_auto, block, local_hour);
  if (modFemLnaGet() != applied) autoSet(applied);
  else auto_set = applied ? 1 : 0;
  // Tests transmit, so they stop with the automatic captures and on a low battery.
  SchedConditions c = conditions(false);
  if (c.auto_off || c.paused || probe_ping_id[0] || schedTestBatteryLow(c.batt_mv)) return;
  if (!autoTestDue(store.lna_auto, modClockGet(), day, block) || !autoChannelOk(store.lna_auto, c.load_pct)) return;
  if (!autoPickTarget(auto_target, sizeof(auto_target))) return;
  memset(&auto_test, 0, sizeof(auto_test));
  auto_block = block;
  auto_first_on = applied;
  autoFloor(applied ? 1 : 0);
  auto_ping_ms = now_ms;
  if (autoPingStart()) auto_step = AUTO_PING_A;
}

// ---------------------------------------------------------------- boot and loop

static void boot() {
  booted = true;
  purgeLegacy();
  if (readFile(STORE_PATH, STORE_MAGIC, &store, sizeof(store))) {
    uint32_t now = modClockGet() >= CLOCK_VALID_EPOCH ? modClockGet() : 0;
    storeRebootSeen(store, now);
    reboot_unstamped = now == 0 && store.reboot[0].last_seen != 0;
  } else {
    storeInit(store);
  }
  linkInit(links);
  have_key = modPublisherKey(self_key);
  startHour(clockHour());
  cur_day = cur_hour ? storeLocalDay(cur_hour, modTzHours()) : 0;
  LnaStudy* saved = (LnaStudy*)malloc(sizeof(LnaStudy));
  bool resume = saved && readFile(STUDY_PATH, LNA_STUDY_MAGIC, saved, sizeof(LnaStudy)) && saved->active;
  // The saved setting is applied at boot; a different value means the operator changed it meanwhile.
  if (resume && modFemLnaAvailable() && modFemLnaGet() == (saved->start_lna == LNA_ON)) {
    run = (StudyRun*)calloc(1, sizeof(StudyRun));
    if (run) {
      run->study = *saved;
      run->tick_ms = millis();
      run->last_save_s = run->study.elapsed_s;
      beginPair(0);
    }
  } else if (resume) {
    saved->result = lnaVerdict(*saved, currentSf(), true);
    saved->active = 0;
    writeFile(STUDY_PATH, LNA_STUDY_MAGIC, saved, sizeof(LnaStudy));
  }
  free(saved);
}

void diagReportsLoop() {
  if (!booted) boot();
  uint32_t now = millis();
  // Short jobs need every pass while they run; everything else is gated to its own interval.
  if (scan_state != SCAN_IDLE) scanTick(now);
  if (burst_active) burstTick(now);
  if (probe_ping_id[0]) pingTick(now);
  if (lock_check_ms) lockCheck(now);
  if (store.lna_auto.enabled) autoTick(now);
  if (now - last_tick_ms < TICK_EVERY_MS) return;
  last_tick_ms = now;
  if (paused && (int32_t)(now - pause_until_ms) >= 0) paused = false;
  guardAfterReset();
  clockTick(now);
  noiseTick(now);
  battTick(now);
  decodeCheck(now);
  studyTick(now);
}

// Every reception counts toward its transmitter, duplicates included: each relay is a separate transmission.
void diagReportsRecv(const mesh::Packet* packet, bool accepted, const uint8_t scope_key[16]) {
  (void)accepted;
  (void)scope_key;
  if (!booted) boot();
  uint8_t size = packet->getPathHashSize();
  uint8_t id_len = size < 2 ? size : 2;
  uint8_t hops = packet->getPathHashCount();
  bool advert = packet->getPayloadType() == PAYLOAD_TYPE_ADVERT && packet->payload_len >= PUB_KEY_SIZE + 4;
  bool own_advert = advert && have_key && memcmp(packet->payload, self_key, PUB_KEY_SIZE) == 0;
  const uint8_t* sender = nullptr;
  if (packet->isRouteFlood() && hops > 0) sender = &packet->path[(hops - 1) * size];
  else if (hops == 0 && advert && !own_advert) sender = packet->payload;
  // An advert names its sender by full key, so it gives the 2-byte id whatever the packet's path width.
  if (sender == packet->payload) id_len = 2;
  if (sender && !(have_key && memcmp(sender, self_key, id_len) == 0)) {
    uint32_t now = millis();
    uint16_t id = id_len == 2 ? (uint16_t)((sender[0] << 8) | sender[1]) : sender[0];
    int index = linkIndex(links, id, id_len, now);
    uint8_t lna = lnaNow();
    linkAddPacket(links, index, packet->getSNR(), currentSf(), lna == STORE_LNA_NA ? 2 : lna, now);
    // It relayed straight after this node, or relayed its advert: it hears this node.
    bool after_us = hops >= 2 && have_key && memcmp(&packet->path[(hops - 2) * size], self_key, size) == 0;
    if (after_us || (own_advert && hops == 1)) linkAddEcho(links, index);
    if (hops == 0 && advert && !own_advert) {
      uint32_t sent, here = modClockGet();
      memcpy(&sent, &packet->payload[PUB_KEY_SIZE], 4);
      if (here > ADVERT_CLOCK_FLOOR && sent > ADVERT_CLOCK_FLOOR) linkAddClock(links, index, (int32_t)((int64_t)sent - here));
    }
  }
  if (!run || run->paused || seenBefore(packet) || isEcho(packet)) return;
  LnaBlock& block = run->blocks[run->cur_state];
  block.unique++;
  if (!sender) return;
  uint16_t id = id_len == 2 ? (uint16_t)((sender[0] << 8) | sender[1]) : sender[0];
  int index = lnaNeighbourIndex(run->study, id, id_len);
  if (index < 0) return;
  block.heard[index]++;
  block.snr_q[index] += packet->_snr;
  block.snr_n[index]++;
}

// ---------------------------------------------------------------- reports

static void runSummary(DiagOut& o) {
  EngineInput in;
  NoiseView nv;
  BattView bv;
  buildInput(in, nv, bv, nullptr);
  EngineResult* r = (EngineResult*)malloc(sizeof(EngineResult));
  if (r == nullptr) {
    outLine(o, "ERR: out of memory");
    return;
  }
  engineRun(in, *r);
  engineFormat(in, *r, o);
  free(r);
}

static void runLinks() {
  char weakest[8];
  NoiseView nv = noiseView(weakest, sizeof(weakest));
  char loud[24] = "";
  if (nv.loud_mask) noiseLoudText(nv, loud, sizeof(loud));
  LinkContext context = {engineRotate(nv.loud_mask, -modTzHours()), engineRotate(nv.have_mask, -modTzHours()), loud, modFemLnaAvailable() != 0};
  buildEvidence(nv);
  relateLinks(evidence, links, currentSf(), millis(), note);
  linkFormat(links, currentSf(), millis(), &context, &note, out);
  if (!retryOn()) return;
  // The week's record, today's unsampled hour included, and whether the last 3 days fell behind the 4 before.
  uint32_t day = clockHour() ? storeLocalDay(clockHour(), modTzHours()) : 0;
  retrySample(day);
  uint32_t r = 0, w = 0, r3 = 0, w3 = 0, r4 = 0, w4 = 0;
  storeRetrySum(store, day, 0, 6, &r, &w);
  storeRetrySum(store, day, 0, 2, &r3, &w3);
  storeRetrySum(store, day, 3, 6, &r4, &w4);
  if (r == 0) {
    outLine(out, "Retry on: none needed this week");
    return;
  }
  char trend[24] = "";
  if (r3 >= LINK_RETRY_MIN && r4 >= LINK_RETRY_MIN && w3 * 100 / r3 + 20 <= w4 * 100 / r4) snprintf(trend, sizeof(trend), ", was %lu%%", (unsigned long)(w4 * 100 / r4));
  outLine(out, "Retry 7d: %lu of %lu won (%lu%%%s)", (unsigned long)w, (unsigned long)r, (unsigned long)(w * 100 / r), trend);
  LinkRetryHop hops[LINK_RETRY_HOPS];
  int n = retryHops(hops);
  char line[80];
  int used = snprintf(line, sizeof(line), "By hop:");
  for (int i = 0; i < n && used < (int)sizeof(line) - 16; i++) {
    used += hops[i].id_len == 2 ? snprintf(line + used, sizeof(line) - used, " %04X %u/%u", hops[i].id, hops[i].won, hops[i].retried)
                                : snprintf(line + used, sizeof(line) - used, " %02X %u/%u", hops[i].id, hops[i].won, hops[i].retried);
  }
  if (n) outLine(out, "%s", line);
}

static void runNoise() {
  if (!clockHour()) {
    outLine(out, "ERR: clock not set (time <epoch>)");
    return;
  }
  char weakest[8] = "";
  NoiseView v = noiseView(weakest, sizeof(weakest));
  buildEvidence(v);
  relateNoise(evidence, links, v, note);
  noiseFormat(v, weakest, store.noise_capture.at ? &store.noise_capture : nullptr, modTzHours(), &note, out);
}

static void evidenceNow() {
  char weakest[8];
  NoiseView nv = noiseView(weakest, sizeof(weakest));
  buildEvidence(nv);
}

static void runBatt() {
  if (batt_zeros >= BATT_ABSENT_READS || modBoardBattMilliVolts() == 0) {
    outLine(out, "🔋 Battery: ⚪ Not Available");
    return;
  }
  if (!clockHour()) {
    outLine(out, "ERR: clock not set (time <epoch>)");
    return;
  }
  BattView v = battAnalyse(store, clockHour(), modTzHours(), calibrated());
  evidenceNow();
  relateBatt(evidence, store, v, note);
  battFormat(v, &note, out);
}

static void runHistory() {
  if (batt_zeros >= BATT_ABSENT_READS || modBoardBattMilliVolts() == 0) {
    outLine(out, "🔋 30 Days: ⚪ Not Available");
    return;
  }
  if (!clockHour()) {
    outLine(out, "ERR: clock not set (time <epoch>)");
    return;
  }
  HistView v = histAnalyse(store, calibrated());
  relateHist(store, v, note);
  histFormat(v, &note, out);
}

static void runTemp() {
  TempView v = tempAnalyse(store, clockHour() != 0, modMcuTemperature());
  evidenceNow();
  relateTemp(evidence, store, v, note);
  tempFormat(v, &note, out);
}

static void runLnaReport() {
  if (run) {
    lnaFormatProgress(run->study, currentSf(), out);
    return;
  }
  LnaStudy* s = (LnaStudy*)malloc(sizeof(LnaStudy));
  bool have = s && readStudy(s);
  evidenceNow();
  relateLna(evidence, note);
  lnaFormatResult(have ? s : nullptr, lnaNow() == STORE_LNA_ON ? LNA_ON : LNA_OFF, currentSf(), modTzHours(), &note, out);
  free(s);
  if (store.lna_auto.enabled) {
    const LnaAutoState& au = store.lna_auto;
    char text[48], row[AUTO_BLOCKS + 1];
    autoStatusText(au, modTzHours(), text, sizeof(text));
    autoBlockRow(au, row);
    outSep(out);
    outLine(out, "%s", text);
    outLine(out, "Blocks 0-24h: %s", row);
    char banned[48];
    if (autoBanText(au, banned, sizeof(banned))) outLine(out, "%s", banned);
    if (au.gain_tests) outLine(out, "Auto vs fixed off: %+d of %u tests", au.gain_replies, au.gain_tests);
    if (au.floor_n) outLine(out, "Floor shift %+d dB over %u tests", (int)(au.floor_shift_sum / au.floor_n), au.floor_n);
    unsigned on = 0, off = 0, ties = 0;
    for (int i = 0; i < AUTO_BLOCKS; i++) {
      on += au.b[i].on_w / 10;
      off += au.b[i].off_w / 10;
      ties += au.b[i].ties;
    }
    outLine(out, "Tests %u: on won %u, off %u, even %u", au.tests_total, on, off, ties);
    if (au.faded || au.silent || au.noisy) outLine(out, "Discarded: %u faded, %u noisy, %u unanswered", au.faded, au.noisy, au.silent);
    if (au.busy) outLine(out, "Skipped %u busy-channel checks", au.busy);
  }
}

static void startStudy(const char* arg) {
  if (!modFemLnaAvailable()) {
    outLine(out, "ERR: no switchable LNA on this board");
    return;
  }
  if (run) {
    outLine(out, "ERR: study already running (diag radio.lna stop)");
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
    outLine(out, "ERR: hours 6-168");
    return;
  }
  run = (StudyRun*)calloc(1, sizeof(StudyRun));
  if (!run) {
    outLine(out, "ERR: out of memory");
    return;
  }
  run->study.active = 1;
  run->study.start_lna = modFemLnaGet() ? LNA_ON : LNA_OFF;
  run->study.requested_s = (uint32_t)hours * 3600UL;
  run->study.clock_valid = modClockGet() > CLOCK_VALID_EPOCH;
  run->tick_ms = millis();
  beginPair(modRandom() % START_SPAN_MS);
  saveStudy();
  outLine(out, "📶 LNA study started, up to %ldh", hours);
  outLine(out, "LNA now %s. It switches on and", run->study.start_lna == LNA_ON ? "on" : "off");
  outLine(out, "off every ~5 min, then goes back.");
  outSep(out);
  outLine(out, "Progress: diag radio.lna");
}

// Every history diag keeps; the time zone is a setting and stays.
static void resetHistory() {
  if (run) finishStudy(true);
  bool auto_on = store.lna_auto.enabled;
  storeInit(store);
  if (auto_on) autoInit(store.lna_auto, clockHour() ? storeLocalDay(clockHour(), modTzHours()) : 0);
  linkInit(links);
  have_scan = false;
  scan_ms = 0;
  batt_zeros = 0;
  last_batt_ms = 0;
  startHour(clockHour());
  if (!ensureMounted()) return;
  if (ModFS.exists(STORE_PATH)) ModFS.remove(STORE_PATH);
  if (ModFS.exists(STUDY_PATH)) ModFS.remove(STUDY_PATH);
}

static void runReset(const char* arg) {
  if (*arg == 0) {
    reset_armed_ms = millis() | 1;
    outLine(out, "Clears: battery log, 30-day history,");
    outLine(out, "noise watch, LNA study, links, scan,");
    outLine(out, "guard counters, freeze hours,");
    outLine(out, "captures. Keeps tz.");
    outLine(out, "Send clear diag confirm within 60 s.");
    return;
  }
  if (strcmp(arg, "confirm") != 0) {
    outLine(out, "ERR: clear diag [confirm]");
    return;
  }
  if (reset_armed_ms == 0 || millis() - reset_armed_ms > RESET_WINDOW_MS) {
    reset_armed_ms = 0;
    outLine(out, "ERR: send clear diag first, then confirm within 60 s");
    return;
  }
  reset_armed_ms = 0;
  resetHistory();
  outLine(out, "OK - diag history cleared");
}

static void runAuto(const char* arg) {
  uint32_t day = clockHour() ? storeLocalDay(clockHour(), modTzHours()) : 0;
  if (*arg == 0) {
    const char* state = store.auto_off ? "off" : paused ? "paused" : "on";
    outLine(out, "Auto: %s, %u of %u scans today", state, schedUsed(store, TRIG_FREEZES, day), SCHED_SCAN_CAP);
    outLine(out, "Bursts today: %u loud, %u decodes", schedUsed(store, TRIG_LOUD, day), schedUsed(store, TRIG_DECODES, day));
    if (paused) outLine(out, "Back on in %lum.", (unsigned long)((pause_until_ms - millis()) / 60000UL + 1));
    // The captures on record, so what the engine is reasoning from can be seen.
    const StoreCapture* caps[3] = {&store.scan_capture, &store.noise_capture, &store.decode_capture};
    const char* names[3] = {"Scan", "Loud", "Decodes"};
    int8_t tz = modTzHours();
    for (int i = 0; i < 3; i++) {
      const StoreCapture& c = *caps[i];
      if (!c.at) continue;
      uint32_t local = c.at + (int32_t)tz * 3600;
      const char* what = i == 0 ? (c.kind == 1 ? "blocker" : c.kind == 2 ? "on channel" : "nothing on air") : c.kind == 3 ? "other LoRa" : c.kind ? "carrier" : "packets";
      outLine(out, "%s %lu:%02lu: %s", names[i], (unsigned long)(local / 3600 % 24), (unsigned long)(local / 60 % 60), what);
    }
    return;
  }
  if (strcmp(arg, "on") == 0 || strcmp(arg, "off") == 0) {
    store.auto_off = arg[1] == 'f';
    paused = false;
    saveStore();
    outLine(out, "OK - auto captures %s", store.auto_off ? "off" : "on");
    return;
  }
  if (strncmp(arg, "pause", 5) == 0 && (arg[5] == 0 || arg[5] == ' ')) {
    int minutes = schedPauseMinutes(arg + 5);
    if (minutes < 0) {
      outLine(out, "ERR: diag auto pause [1-1440]");
      return;
    }
    paused = true;
    pause_until_ms = millis() + (uint32_t)minutes * 60000UL;
    outLine(out, "OK - auto captures paused %dm", minutes);
    return;
  }
  outLine(out, "ERR: diag auto [on|off|pause [min]]");
}

// ---------------------------------------------------------------- slow commands and probes

static bool sameSender(const ModReplyTarget& a, const ModReplyTarget& b) {
  return a.remote == b.remote && (!a.remote || memcmp(a.admin_key, b.admin_key, sizeof(a.admin_key)) == 0);
}

// One slow job at a time: a repeat from the same sender sees progress and starts nothing; anyone else is told to wait.
static bool busyReply(const ModCliContext& context, char* reply) {
  bool scanning = scan_state != SCAN_IDLE;
  if (!scanning && !burst_active) return false;
  ModReplyTarget asker;
  bool known = modReplyCapture(context.sender_timestamp, &asker);
  bool mine = scanning ? scan_for != FOR_T1 && sameSender(asker, scan_target) : burst_for == FOR_PROBE && sameSender(asker, burst_target);
  if (known && mine && scanning) sprintf(reply, "working, scan %u of %u sweeps", scan.sweeps, SCAN_SWEEPS);
  else if (known && mine) strcpy(reply, "working, listening");
  else strcpy(reply, "busy, try again in a few seconds");
  return true;
}

static void slowScan(const ModCliContext& context, char* reply, uint8_t for_what) {
  if (!modRadioScanAvailable()) {
    strcpy(reply, "ERR: rfscan needs an SX126x radio");
    return;
  }
  if (busyReply(context, reply)) return;
  ModReplyTarget target;
  if (!modReplyCapture(context.sender_timestamp, &target)) {
    strcpy(reply, "ERR: sender unknown");
    return;
  }
  ModRadioValues ch;
  if (!liveChannel(&ch)) {
    strcpy(reply, "ERR: radio settings unavailable");
    return;
  }
  if (!startScan(for_what, &target)) {
    strcpy(reply, "ERR: radio busy, try again");
    return;
  }
  strcpy(reply, for_what == FOR_RFSCAN ? "ok, scanning ~5s" : "ok, scanning ~2s");
}

static void probeGuard() {
  uint8_t guard, state[2];
  if (!modRadioGuardRead(&guard, state)) {
    outLine(out, "ERR: guard needs an SX126x radio");
    return;
  }
  bool busy = state[0] == 0x08 && state[1] == 0x12, frozen = lockedNow(state);
  outLine(out, "Guard %s (0x%02X), %s.", guard & 1 ? "on" : "OFF", guard, frozen ? "FROZEN now" : busy ? "busy with a strong signal" : "not frozen now");
  uint32_t now = modClockGet();
  if (store.guard.last_freeze && now > store.guard.last_freeze) {
    uint32_t ago = (now - store.guard.last_freeze) / 60;
    if (ago < 120) outLine(out, "Last freeze %lum ago.", (unsigned long)ago);
    else outLine(out, "Last freeze %luh ago.", (unsigned long)(ago / 60));
  }
  if (store.guard.off && (store.guard.off_after & 1)) outLine(out, "Found off %ux; the reset restored it.", store.guard.off);
  else if (store.guard.off) outLine(out, "Found off %ux; the reset left it off (%02X).", store.guard.off, store.guard.off_after);
  if (store.guard.strong) outLine(out, "Strong signal at %u resets, not locks.", store.guard.strong);
  if (agcInterval() == 0) outLine(out, "AGC reset off: nothing clears a freeze.");
}

static void probeBatt() {
  if (batt_zeros >= BATT_ABSENT_READS || modBoardBattMilliVolts() == 0 || !clockHour()) {
    outLine(out, "ERR: no battery reading");
    return;
  }
  BattView v = battAnalyse(store, clockHour(), modTzHours(), calibrated());
  outLine(out, "Last hour %+d mV, day %+d mV:", v.hour_mv, v.net_mv);
  outLine(out, v.hour_mv < 0 ? "still falling now." : v.hour_mv > 0 ? "rising now." : "level now.");
}

// The floor with the LNA on and off, a few hundred milliseconds each; never transmits.
static void probeLna() {
  if (!modFemLnaAvailable()) {
    outLine(out, "ERR: no switchable LNA on this board");
    return;
  }
  if (run) {
    outLine(out, "ERR: study running (diag radio.lna)");
    return;
  }
  bool was = modFemLnaGet();
  int8_t floor_dbm[2] = {0, 0};
  for (int state = 0; state < 2; state++) {
    modFemLnaSet(state == LNA_ON);
    delay(20);
    DbmHistogram g;
    histoReset(g);
    for (int i = 0; i < 12; i++) {
      int8_t dbm;
      if (readFloor(&dbm)) histoAdd(g, dbm);
      delay(15);
    }
    floor_dbm[state] = histoMedian(g);
  }
  modFemLnaSet(was);
  int shift = floor_dbm[LNA_ON] - floor_dbm[LNA_OFF];
  if (clockHour()) store.lna_check = {modClockGet(), floor_dbm[LNA_ON], floor_dbm[LNA_OFF], {0, 0}};
  outLine(out, "Floor now: on %d, off %d dBm", floor_dbm[LNA_ON], floor_dbm[LNA_OFF]);
  outLine(out, shift >= 10 ? "LNA working (%d dB). No transmit." : "Barely moves (%d dB): check the FEM.", shift);
}

// A neighbour id typed as hex: "c5fb" or "c5".
static bool parseId(const char* text, uint16_t* id, uint8_t* len) {
  size_t n = strlen(text);
  if (n != 2 && n != 4) return false;
  char* end;
  unsigned long v = strtoul(text, &end, 16);
  if (*end != 0) return false;
  *id = (uint16_t)v;
  *len = (uint8_t)(n / 2);
  return true;
}

// Starts the one ping a probe may send: to the neighbour the leading cause is about.
static void probePing(const ModCliContext& context, const char* id, char* reply) {
  if (probe_ping_id[0]) {
    strcpy(reply, "working, pinging");
    return;
  }
  if (!modReplyCapture(context.sender_timestamp, &probe_target)) {
    strcpy(reply, "ERR: sender unknown");
    return;
  }
  char target[8];
  snprintf(target, sizeof(target), "%s", id);
  for (char* c = target; *c; c++) if (*c >= 'A' && *c <= 'F') *c += 32;
  if (!pingQuiet(target, reply)) return;
  snprintf(probe_ping_id, sizeof(probe_ping_id), "%s", target);
  probe_ping_started = millis();
  sprintf(reply, "ok, pinging %s ~2s", target);
}

// One command: the engine names the leading cause and the probe runs the live check that confirms it.
static void runProbe(const ModCliContext& context, const char* arg, char* reply) {
  if (*arg) {
    strcpy(reply, "ERR: diag probe takes no arguments");
    return;
  }
  EngineInput in;
  NoiseView nv;
  BattView bv;
  buildInput(in, nv, bv, nullptr);
  EngineResult* r = (EngineResult*)malloc(sizeof(EngineResult));
  if (!r) {
    strcpy(reply, "ERR: out of memory");
    return;
  }
  engineRun(in, *r);
  uint8_t cause = r->cause;
  free(r);
  const char* area = engineProbeFor(cause);
  if (*area == 0) {
    strcpy(reply, "Nothing to check: all green.");
    return;
  }
  if (strcmp(area, "ping") == 0) {
    char who[8] = "";
    float margin = 0;
    int index = cause == CAUSE_FAR_SIDE ? linkOneWay(links, in.sf, in.now_ms) : -1;
    if (index >= 0) linkLabel(links, index, who, sizeof(who));
    else linkWeakest(links, in.sf, in.now_ms, &margin, who, sizeof(who));
    if (!who[0]) {
      strcpy(reply, "Nothing to check: no neighbour to ping.");
      return;
    }
    probePing(context, who, reply);
    return;
  }
  outReset(out);
  if (strcmp(area, "guard") == 0) probeGuard();
  else if (strcmp(area, "batt") == 0) probeBatt();
  else if (strcmp(area, "lna") == 0) probeLna();
  else if (strcmp(area, "noise") == 0) {
    if (busyReply(context, reply)) return;
    ModReplyTarget target;
    if (!modReplyCapture(context.sender_timestamp, &target)) {
      strcpy(reply, "ERR: sender unknown");
      return;
    }
    startBurst(FOR_PROBE, &target);
    strcpy(reply, "ok, listening ~2s");
    return;
  } else {
    slowScan(context, reply, FOR_PROBE);
    return;
  }
  emit(context, reply);
}

static bool isWord(const char* command, const char* word) {
  size_t len = strlen(word);
  return strncmp(command, word, len) == 0 && (command[len] == 0 || command[len] == ' ');
}

static const char* argAfter(const char* command, const char* word) {
  const char* arg = command + strlen(word);
  while (*arg == ' ') arg++;
  return arg;
}

bool diagReportsHandleCli(const ModCliContext& context, char* command, char* reply) {
  if (!booted) boot();

  // A restart the operator asked for is not a fault; the command itself still runs.
  if (strcmp(command, "reboot") == 0) {
    store.reboot_planned = 1;
    saveStore();
    return false;
  }
  // LNA auto lives behind the existing setting's name.
  if (strcmp(command, "set radio.fem.rxgain auto") == 0) {
    if (!modFemLnaAvailable()) {
      strcpy(reply, "ERR: no switchable LNA on this board");
      return true;
    }
    uint32_t day = clockHour() ? storeLocalDay(clockHour(), modTzHours()) : 0;
    LnaAutoState& a = store.lna_auto;
    // Back to auto keeps its scores; a parked auto, or the first time, starts a fresh learning week with every block off.
    if (a.start_day && a.park == AUTO_ACTIVE) a.enabled = 1;
    else autoInit(a, day);
    saveStore();
    strcpy(reply, "OK - auto (learning, starts off)");
    return true;
  }
  if (strcmp(command, "get radio.fem.rxgain") == 0 && store.lna_auto.enabled && modFemLnaAvailable()) {
    char text[48], banned[48];
    autoStatusText(store.lna_auto, modTzHours(), text, sizeof(text));
    if (autoBanText(store.lna_auto, banned, sizeof(banned))) sprintf(reply, "> %s; %s", text, banned);
    else sprintf(reply, "> %s", text);
    return true;
  }
  if (store.lna_auto.enabled && (isWord(command, "set radio.fem.rxgain") || (isWord(command, "tryset") && strstr(command, "radio.fem.rxgain")))) {
    // The operator takes the LNA back; the scores stay for a later `auto`.
    store.lna_auto.enabled = 0;
    auto_step = AUTO_IDLE;
    auto_set = -1;
    saveStore();
  }
  // An operator change to the LNA ends the study without a restore; the command itself still runs.
  if (run && (isWord(command, "set radio.fem.rxgain") || (isWord(command, "tryset") && strstr(command, "radio.fem.rxgain")))) {
    finishStudy(false);
    return false;
  }
  outReset(out);
  if (strcmp(command, "diag") == 0 || strcmp(command, "diag radio") == 0) {
    // A missing or day-old scan is refreshed first, so the report comes back late, after it.
    if (modRadioScanAvailable() && (!have_scan || millis() - scan_ms >= SCAN_STALE_MS)) {
      slowScan(context, reply, FOR_DIAG);
      if (strncmp(reply, "ok", 2) == 0 || strncmp(reply, "working", 7) == 0 || strncmp(reply, "busy", 4) == 0) return true;
      outReset(out);
    }
    runSummary(out);
  } else if (strcmp(command, "diag radio.links") == 0) runLinks();
  else if (strcmp(command, "diag radio.noise") == 0) runNoise();
  else if (strcmp(command, "diag batt") == 0) runBatt();
  else if (strcmp(command, "diag batt.history") == 0) runHistory();
  else if (strcmp(command, "diag temp") == 0) runTemp();
  else if (strcmp(command, "diag radio.rfscan") == 0) {
    slowScan(context, reply, FOR_RFSCAN);
    return true;
  } else if (isWord(command, "clear diag")) runReset(argAfter(command, "clear diag"));
  else if (isWord(command, "diag auto")) runAuto(argAfter(command, "diag auto"));
  else if (isWord(command, "diag probe")) {
    runProbe(context, argAfter(command, "diag probe"), reply);
    return true;
  } else if (isWord(command, "diag radio.lna")) {
    const char* arg = argAfter(command, "diag radio.lna");
    if (*arg == 0) runLnaReport();
    else if (isWord(arg, "start")) startStudy(argAfter(arg, "start"));
    else if (strcmp(arg, "stop") == 0) {
      if (!run) outLine(out, "ERR: no study running");
      else {
        finishStudy(true);
        runLnaReport();
      }
    } else {
      outLine(out, "ERR: diag radio.lna [start [hours]|stop]");
    }
  } else {
    return false;
  }
  emit(context, reply);
  return true;
}
