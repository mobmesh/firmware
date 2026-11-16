#include <helpers/diag_reports/DiagEngine.h>
#include <helpers/diag_reports/DiagEvidence.h>
#include <helpers/diag_reports/DiagLna.h>
#include <helpers/diag_reports/DiagRelate.h>
#include <helpers/diag_reports/DiagTrend.h>

#include <math.h>

#include "check.h"

// Each case shows a report reaching a different, better answer because of evidence from outside its own data.

#define NOW 500016U                    // local midnight at tz 0
#define MS  90000000UL
#define SF  7

static DiagStore store;
static LinkTable links;
static NoiseView noise;
static DiagEvidence ev;
static DiagNote note;
static DiagOut out;
static LnaFacts facts;

static bool noteHas(const char* text) {
  for (int i = 0; i < note.n; i++) if (strstr(note.line[i], text)) return true;
  return false;
}
static bool outHas(const char* text) { return strstr(out.buf, text) != nullptr; }

#define NOTE(text) do { if (!noteHas(text)) { printf("FAIL %s:%d note lacks '%s':\n", __FILE__, __LINE__, text); for (int i = 0; i < note.n; i++) printf("  %s\n", note.line[i]); failures++; } } while (0)
#define NO_NOTE(text) do { if (noteHas(text)) { printf("FAIL %s:%d note has '%s'\n", __FILE__, __LINE__, text); failures++; } } while (0)
#define OUT(text) do { if (!outHas(text)) { printf("FAIL %s:%d output lacks '%s':\n%s\n", __FILE__, __LINE__, text, out.buf); failures++; } } while (0)

static void neighbour(uint16_t id, float snr, int packets, int echoes) {
  int index = linkIndex(links, id, 2, MS);
  for (int p = 0; p < packets; p++) linkAddPacket(links, index, snr, SF, 1, MS);
  for (int e = 0; e < echoes; e++) linkAddEcho(links, index);
}

static void quietDay(int8_t floor = -112, uint16_t recv = 100, uint16_t errors = 0) {
  storeInit(store);
  linkInit(links);
  for (uint32_t k = 0; k < 24; k++) {
    storeNoiseHour(store, NOW - 24 + k, floor, STORE_LNA_ON, true, recv, errors);
    storeLoadHour(store, NOW - 24 + k, 0, 6);
  }
  for (uint8_t h = 0; h < 24; h++) {
    linkRollHour(links, h);
    neighbour(0xC5FB, 4.0f, 3, 2);
    neighbour(0x4EA1, 9.0f, 3, 2);
  }
}

static void loud(int from, int to, int8_t floor, uint16_t recv, uint16_t errors) {
  for (int l = from; l < to; l++) storeNoiseHour(store, NOW - 24 + l, floor, STORE_LNA_ON, true, recv, errors);
}

static void build(bool lna = true, bool lna_on = true, const LnaFacts* f = nullptr, float temp = 21) {
  char weakest[8];
  float margin = NOISE_NO_MARGIN;
  if (!linkWeakest(links, SF, MS, &margin, weakest, sizeof(weakest))) margin = NOISE_NO_MARGIN;
  noise = noiseAnalyse(store, NOW, 0, STORE_LNA_ON, margin);
  EvidenceSources src = {&store, &links, &noise, NOW, MS, 0, SF, temp, lna, lna_on, f, false, 0, 0, 0};
  evidenceBuild(ev, src);
}

static uint32_t at(int local_hour, int minute = 0) {
  return (NOW - 24 + local_hour) * 3600 + minute * 60;
}

static void links_cases() {
  // One-way: a ping names the weak side.
  quietDay();
  linkInit(links);
  neighbour(0xC5FB, 9.0f, 40, 0);
  neighbour(0x4EA1, 9.0f, 30, 14);
  neighbour(0xA6B2, 12.0f, 25, 9);
  build();
  CHECK(ev.one_way >= 0);
  relateLinks(ev, links, SF, MS, note);
  NOTE("See: diag probe");
  ev.ping_ok = true;
  ev.ping_id = 0xC5FB;
  ev.ping_out_db = 2.0f;
  ev.ping_back_db = 11.5f;
  relateLinks(ev, links, SF, MS, note);
  NOTE("Their side hears us weakly:");
  NOTE("check C5FB's antenna.");
  ev.ping_out_db = 11.5f;
  ev.ping_back_db = 2.0f;
  relateLinks(ev, links, SF, MS, note);
  NOTE("check our antenna and cable.");

  // Paths leaning on a weak link, in the links report itself.
  quietDay();
  linkInit(links);
  neighbour(0xC5FB, -6.0f, 90, 6);
  neighbour(0x4EA1, 9.0f, 10, 4);
  neighbour(0xA6B2, 12.0f, 8, 3);
  build();
  relateLinks(ev, links, SF, MS, note);
  NOTE("of paths go via weak C5FB");

  // Confidence: how many hours the weakest link was heard, and when it is weak.
  quietDay();
  linkInit(links);
  for (uint8_t h = 0; h < 24; h++) {
    linkRollHour(links, h);
    neighbour(0x4EA1, 9.0f, 3, 2);
    if (h >= 8 && h < 12) neighbour(0xC5FB, -6.0f, 2, 1);
    else if (h % 2 == 0) neighbour(0xC5FB, 4.0f, 2, 1);
  }
  build();
  relateLinks(ev, links, SF, MS, note);
  NOTE("C5FB weak 8am-12pm (heard 14 of 24h)");

  // A very strong neighbour with failing decodes and no noise rise.
  quietDay();
  for (int b = 3; b < 4; b++) loud(b * 4, b * 4 + 4, -112, 100, 15);
  linkInit(links);
  neighbour(0xA6B2, 28.0f, 30, 4);
  neighbour(0x4EA1, 9.0f, 30, 4);
  build();
  CHECK(ev.strong >= 0 && ev.err && !ev.loud);
  relateLinks(ev, links, SF, MS, note);
  NOTE("A6B2 at +36 dB margin can swamp");
}

static void noise_cases() {
  // A capture that saw packets turns loud hours into a busy mesh.
  quietDay();
  loud(8, 17, -105, 100, 1);
  store.noise_capture = {at(9, 10), 0, -106, -95, -101, 0};
  build();
  CHECK(ev.loud_source == SOURCE_PACKETS);
  noiseMarkBusy(noise);
  CHECK(noise.kind == NOISE_K_BUSY && noise.level == DIAG_CAUTION);
  relateNoise(ev, links, noise, note);
  outReset(out);
  noiseFormat(noise, "", nullptr, 0, &note, out);
  OUT("Noise: \U0001F7E1 Busy 8am-5pm, Not Noise");
  OUT("Caught bursts: mesh traffic,");

  // A carrier capture keeps them noise and says so; a link lost in those hours is named.
  quietDay();
  loud(8, 17, -104, 100, 12);
  linkInit(links);
  for (uint8_t h = 0; h < 24; h++) {
    linkRollHour(links, h);
    if (h < 8 || h >= 17) neighbour(0xC5FB, 4.0f, 3, 2);
    neighbour(0x4EA1, 9.0f, 3, 2);
  }
  store.noise_capture = {at(8, 42), 0, -103, -101, -102, 1};
  build();
  CHECK(ev.loud_source == SOURCE_CARRIER && ev.drops >= 0);
  relateNoise(ev, links, noise, note);
  NOTE("Caught a steady carrier, not packets.");
  NOTE("C5FB drops 8am-5pm.");
  NOTE("Try: diag radio.rfscan at 9am.");
  // A scan that ran in those hours and found energy on the channel closes the case.
  store.scan = {at(10), SCAN_K_MESH_STEADY, DIAG_PROBLEM, 0, 9, -118, 0, 0, 0, 0};
  build();
  CHECK(ev.scan_explains_loud);
  relateNoise(ev, links, noise, note);
  NOTE("Scan at 10am found it on channel.");

  // Failed decodes with no rise: a carrier caught at the failures is interference; a very strong neighbour is named.
  quietDay();
  loud(12, 16, -112, 100, 15);
  store.decode_capture = {at(13), 0, -100, -98, -99, 1};
  build();
  CHECK(noise.kind == NOISE_K_ERRORS && ev.decode_source == SOURCE_CARRIER);
  relateNoise(ev, links, noise, note);
  NOTE("interference, not collisions.");
  store.decode_capture.at = 0;
  linkInit(links);
  neighbour(0xA6B2, 28.0f, 30, 4);
  build();
  relateNoise(ev, links, noise, note);
  NOTE("A6B2 is");
  store.decode_capture = {at(13), 0, -110, -90, -104, 0};
  linkInit(links);
  neighbour(0x4EA1, 9.0f, 30, 4);
  build();
  relateNoise(ev, links, noise, note);
  NOTE("bursts at the failures: collisions.");

  // Freezes only in hours with the LNA on, after real hours with it off.
  quietDay();
  for (int l = 0; l < 6; l++) storeNoiseHour(store, NOW - 24 + l, -118, STORE_LNA_OFF, true, 100, 0);
  storeFreeze(store, at(14), 0);
  storeFreeze(store, at(15), 0);
  build();
  CHECK(ev.lna_freezes_only_on);
  relateNoise(ev, links, noise, note);
  NOTE("Only with the LNA on.");
  // The same freezes on a node whose LNA never went off say nothing about it.
  quietDay();
  storeFreeze(store, at(14), 0);
  storeFreeze(store, at(15), 0);
  build();
  CHECK(!ev.lna_freezes_only_on);
}

static void source_cases() {
  char hint[48];
  // Loud 10am-3pm while the battery charges: the node's own power gear.
  quietDay();
  loud(10, 15, -100, 100, 1);
  uint16_t mv = 3900;
  for (uint32_t k = 0; k < 24; k++) {
    bool charging = k >= 9 && k < 16;
    storeSample(store, NOW - 24 + k, 0, mv, 20);
    mv = (uint16_t)(charging ? mv + 20 : mv - 3);
    storeSample(store, NOW - 24 + k, 0, mv, 20);
  }
  build();
  CHECK(ev.noise_follows_charging);
  relateSourceHint(ev, noise.max_rise, hint, sizeof(hint));
  CHECK_STR(hint, "Strong, follows charging: own charger or panel.");
  relateNoise(ev, links, noise, note);
  NOTE("follows charging");
  // The same hours on a node that is not charging then: daytime, nearby.
  quietDay();
  loud(10, 15, -100, 100, 1);
  build();
  CHECK(!ev.noise_follows_charging);
  relateSourceHint(ev, noise.max_rise, hint, sizeof(hint));
  CHECK_STR(hint, "Strong, daytime: solar gear close by.");
  // Evenings, faint, a captured carrier.
  quietDay();
  loud(18, 23, -107, 100, 1);
  store.noise_capture = {at(19), 0, -106, -105, -105, 1};
  build();
  relateSourceHint(ev, noise.max_rise, hint, sizeof(hint));
  CHECK_STR(hint, "Weak, steady, evenings: lights or household.");
  // Overnight and very strong: close by.
  quietDay();
  loud(1, 5, -85, 100, 1);
  build();
  relateSourceHint(ev, noise.max_rise, hint, sizeof(hint));
  CHECK_STR(hint, "Very strong, close by.");
}

static void scan_cases() {
  // A blocker found beside the channel, with freezes logged: likely the cause.
  quietDay();
  storeFreeze(store, at(8, 5), 0);
  storeFreeze(store, at(8, 40), 0);
  store.scan = {at(14), SCAN_K_EDGE_STEADY, DIAG_CAUTION, -250, 14, -118, 1, -250, 14, 0};
  build();
  relateScan(ev, note);
  NOTE("Matches freezes 8am:");
  NOTE("likely the cause.");

  // Energy on the channel in the loud hours: likely the source.
  quietDay();
  loud(8, 17, -104, 100, 1);
  store.scan = {at(9), SCAN_K_MESH_STEADY, DIAG_PROBLEM, 0, 9, -118, 0, 0, 0, 0};
  build();
  relateScan(ev, note);
  NOTE("(8am-5pm): likely the source.");

  // A clear scan at the wrong hour names the right one.
  store.scan = {at(14 + 6), SCAN_K_CLEAR, DIAG_GOOD, 0, 0, -118, 0, 0, 0, 0};
  build();
  relateScan(ev, note);
  NOTE("Ran 8pm; loud hours are 8am-5pm:");
  NOTE("scan at 9am.");

  // Freezes with a clear scan outside their hours; and inside them: nothing on air now.
  quietDay();
  storeFreeze(store, at(8, 5), 0);
  storeFreeze(store, at(9, 5), 0);
  store.scan = {at(15), SCAN_K_CLEAR, DIAG_GOOD, 0, 0, -118, 0, 0, 0, 0};
  build();
  relateScan(ev, note);
  NOTE("Ran 3pm; freezes are 8am-10am:");
  store.scan.at = at(9, 30);
  build();
  relateScan(ev, note);
  NOTE("nothing on air now.");
}

static void lna_cases() {
  // Freezes only with the LNA on make a study overloaded, whatever the packet counts.
  static LnaStudy st;
  memset(&st, 0, sizeof(st));
  st.wins[LNA_ON] = 200;
  st.wins[LNA_OFF] = 60;
  st.unique[LNA_ON] = 1380;
  st.unique[LNA_OFF] = 1000;
  int a = lnaNeighbourIndex(st, 0xC5FB, 2), b = lnaNeighbourIndex(st, 0x4EA1, 2);
  st.nb[a].heard[LNA_ON] = 700;
  st.nb[a].heard[LNA_OFF] = 500;
  st.nb[b].heard[LNA_ON] = 680;
  st.nb[b].heard[LNA_OFF] = 500;
  CHECK(lnaVerdict(st, SF, true) == LNA_V_HELPS);
  st.freezes[LNA_ON] = 3;
  CHECK(lnaVerdict(st, SF, true) == LNA_V_OVERLOADED);
  st.freezes[LNA_OFF] = 1;
  CHECK(lnaVerdict(st, SF, true) == LNA_V_HELPS);

  // Facts: the neighbours that need it, decode rates per state, floor shift.
  memset(&st, 0, sizeof(st));
  st.wins[LNA_ON] = 120;
  st.wins[LNA_OFF] = 110;
  st.floor_sum[LNA_ON] = -95 * 10;
  st.floor_sum[LNA_OFF] = -117 * 10;
  st.floor_n[LNA_ON] = st.floor_n[LNA_OFF] = 10;
  st.recv[LNA_ON] = 900;
  st.errors[LNA_ON] = 60;
  st.recv[LNA_OFF] = 950;
  st.errors[LNA_OFF] = 10;
  a = lnaNeighbourIndex(st, 0xA6B2, 2);
  b = lnaNeighbourIndex(st, 0x4EA1, 2);
  st.nb[a].heard[LNA_ON] = 60;
  st.nb[a].heard[LNA_OFF] = 12;
  st.nb[b].heard[LNA_ON] = 500;
  st.nb[b].heard[LNA_OFF] = 490;
  st.result = LNA_V_NO_BENEFIT;
  facts = lnaFacts(st, SF);
  CHECK(facts.known && facts.need_n == 1 && facts.need_id[0] == 0xA6B2 && facts.floor_shift == 22);
  CHECK(facts.err_tenths[1] == 62 && facts.err_tenths[0] == 10);
  quietDay();
  build(true, true, &facts);
  relateLna(ev, note);
  NOTE("Floor +22 dB on; failed 6.2%");
  NOTE("on vs 1.0% off: overload.");
  NOTE("A6B2 needs it on.");
  NOTE("On costs ~7 mA, ~168 mAh a day.");

  // Off better in the loud hours: the noise source swamps the LNA.
  for (int d = 0; d < 3; d++) {
    for (int h = 0; h < 24; h++) {
      bool in = h >= 8 && h < 16;
      st.hour_wins[d][h][in ? LNA_OFF : LNA_ON] = 3;
      st.hour_wins[d][h][in ? LNA_ON : LNA_OFF] = 1;
    }
  }
  facts = lnaFacts(st, SF);
  quietDay();
  loud(8, 16, -104, 100, 1);
  build(true, true, &facts);
  relateLna(ev, note);
  NOTE("Off better 8am-4pm, the loud");
  NOTE("hours: the source swamps it.");

  // The gate closed: nothing about the LNA, whatever the study says.
  build(false, false, &facts);
  relateLna(ev, note);
  CHECK(note.n == 0 && !ev.lna_available && ev.lna_off_better == 0);
}

static void power_cases() {
  // Falling with the CPU below freezing and no charging hour: cold, not the panel.
  storeInit(store);
  linkInit(links);
  for (uint32_t k = 0; k <= 24; k++) storeSample(store, NOW - 24 + k, 0, (uint16_t)(3970 - 5 * k), -3);
  build(true, true, nullptr, -3);
  BattView v = battAnalyse(store, NOW, 0, true);
  CHECK(v.kind == BATT_K_DRAINING && ev.cold);
  relateBatt(ev, store, v, note);
  NOTE("CPU -3°C: charger pauses below 0.");
  // With the LNA on, the drain note gives its share.
  quietDay();
  for (uint32_t k = 0; k <= 24; k++) storeSample(store, NOW - 24 + k, 0, (uint16_t)(3970 - 5 * k), 21);
  for (uint32_t k = 0; k < 24; k++) storeNoiseHour(store, NOW - 24 + k, -112, STORE_LNA_ON, true, 100, 0);
  build(true, true, nullptr, 21);
  v = battAnalyse(store, NOW, 0, true);
  relateBatt(ev, store, v, note);
  NOTE("LNA on: ~168 mAh a day of it.");

  // Month of busy days costing more than quiet ones: drain follows load; history says busy days.
  storeInit(store);
  uint16_t mv = 4100;
  for (int day = 29; day >= 1; day--) {
    bool busy = day % 2 == 0;
    uint32_t midnight = NOW - 24U * day;
    storeSample(store, midnight + 6, 0, mv, 20);
    for (uint32_t h = 0; h < 24; h++) storeLoadHour(store, midnight + h, 0, busy ? 35 : 6);
    mv = (uint16_t)(busy ? mv - 50 : mv + 30);
  }
  for (uint32_t k = 0; k <= 24; k++) storeSample(store, NOW - 24 + k, 0, (uint16_t)(mv - 3 * k), 20);
  build();
  v = battAnalyse(store, NOW, 0, true);
  CHECK(relateDayDrain(store, false) >= 60);
  relateBatt(ev, store, v, note);
  NOTE("drain follows load, not panel.");
  HistView h = histAnalyse(store, true);
  relateHist(store, h, note);
  NOTE("Worst on busy days:");

  // Cold days costing more than warm ones: the history blames the cold.
  storeInit(store);
  mv = 4000;
  for (int day = 29; day >= 0; day--) {
    bool cold = day % 3 == 0;
    uint32_t midnight = NOW - 24U * day;
    storeSample(store, midnight + 3, 0, mv, cold ? -4 : 12);
    storeSample(store, midnight + 6, 0, mv, cold ? -4 : 12);
    mv = (uint16_t)(cold ? mv - 40 : mv + 5);
  }
  h = histAnalyse(store, true);
  relateHist(store, h, note);
  NOTE("Worst on cold days:");
  TempView t = tempAnalyse(store, true, -4);
  build(true, true, nullptr, -4);
  relateTemp(ev, store, t, note);
  NOTE("cold days with no charge.");

  // An unplanned restart after a hot hour: heat reboots, in the temp report and the engine.
  quietDay();
  storeSample(store, NOW - 3, 0, 3900, 67);
  store.last_seen = (NOW - 3) * 3600 + 1800;
  storeRebootSeen(store, (NOW - 2) * 3600);
  build();
  CHECK(ev.heat_reboot == 0);
  t = tempAnalyse(store, true, 40);
  relateTemp(ev, store, t, note);
  NOTE("after 67°C:");
  // A restart the operator asked for is not heat.
  store.reboot[0].planned = 1;
  build();
  CHECK(ev.heat_reboot < 0);
}

static void traffic_cases() {
  // Passive flag reads and the decoder name what was on the channel.
  TrafficCheck t = {};
  CHECK(trafficClassify(t, true) == TRAFFIC_UNKNOWN);
  // Our packet: preamble, then a header, then the flags clear on receipt.
  trafficPeek(t, 0x0004); trafficPeek(t, 0x0014); trafficPeek(t, 0);
  CHECK(t.preambles == 1 && t.orphans == 0 && t.pending == 0);
  // A header error still reached our header stage: not an orphan.
  trafficPeek(t, 0x0004); trafficPeek(t, 0x0024); trafficPeek(t, 0);
  CHECK(t.preambles == 2 && t.orphans == 0);
  // Another sync word: the preamble times out with no header, once per preamble however long it is held.
  trafficPeek(t, 0x0004); trafficPeek(t, 0x0004); trafficPeek(t, 0);
  CHECK(t.preambles == 3 && t.orphans == 1);
  CHECK(trafficClassify(t, true) == TRAFFIC_UNKNOWN);
  trafficPeek(t, 0x0004); trafficPeek(t, 0);
  CHECK(t.orphans == 2 && trafficClassify(t, true) == TRAFFIC_UNKNOWN);
  trafficPeek(t, 0x0004); trafficPeek(t, 0);
  CHECK(t.orphans == 3 && trafficClassify(t, true) == TRAFFIC_FOREIGN);
  t.decoded = 1;
  CHECK(trafficClassify(t, true) == TRAFFIC_OURS);
  t = {};
  trafficPeek(t, 0);
  CHECK(trafficClassify(t, true) == TRAFFIC_NOT_LORA && trafficClassify(t, false) == TRAFFIC_QUIET);
  CHECK_STR(trafficText(TRAFFIC_FOREIGN), "another LoRa network");
  // The wide report replaces the "(mesh traffic?)" guess with the measured answer.
  static ScanData d;
  memset(&d, 0, sizeof(d));
  scanSetWide(d, 910.525f);
  d.sweeps = SCAN_SWEEPS;
  for (int b = 0; b < SCAN_BAND_STEPS; b++) d.band[b] = scanPack(-118);
  for (int sw = 0; sw < SCAN_SWEEPS; sw++) for (int b = 0; b < SCAN_STEPS; b++) d.raw[sw][b] = scanPack(sw < 4 && b == SCAN_MESH_STEP ? -80 : -117);
  d.traffic = {};
  d.traffic.peeks = 60; d.traffic.preambles = 4; d.traffic.orphans = 4;
  outReset(out);
  scanFormat(d, nullptr, out);
  OUT("mesh ch bursts 4/6: another LoRa network");
  OUT("LoRa 4 preambles, 4 no header, 0 decoded");
  CHECK(!outHas("(mesh traffic?)"));
  d.traffic = {};
  d.traffic.peeks = 60;
  outReset(out);
  scanFormat(d, nullptr, out);
  OUT("mesh ch bursts 4/6: not LoRa: interference");
}

static void scan_spill_case() {
  // A strong carrier on the channel spills into the steps beside it; that spill is not a blocker.
  static ScanData d;
  memset(&d, 0, sizeof(d));
  scanSetWide(d, 907.525f);
  d.sweeps = SCAN_SWEEPS;
  for (int b = 0; b < SCAN_BAND_STEPS; b++) d.band[b] = scanPack(-118);
  for (int sw = 0; sw < SCAN_SWEEPS; sw++) {
    for (int b = 0; b < SCAN_STEPS; b++) {
      int away = b > SCAN_MESH_STEP ? b - SCAN_MESH_STEP : SCAN_MESH_STEP - b;
      d.raw[sw][b] = scanPack(away == 0 ? -65 : away <= 4 ? -75 : -117);
    }
  }
  ScanFinding f = scanAnalyse(d);
  CHECK(f.kind == SCAN_K_MESH_STEADY && !f.blocker);
  // A real blocker stands above the channel.
  for (int sw = 0; sw < SCAN_SWEEPS; sw++) d.raw[sw][SCAN_MESH_STEP - 1] = scanPack(-55);
  f = scanAnalyse(d);
  CHECK(f.blocker && f.blocker_khz == -250);

  // The narrow automatic scan: +/-2 bandwidths in half-bandwidth steps; a signal 62.5 kHz off is beside the channel.
  memset(&d, 0, sizeof(d));
  scanSetNarrow(d, 910.525f, 62.5f);
  d.sweeps = SCAN_SWEEPS;
  CHECK(d.steps == 9 && d.mesh == 4 && fabsf(scanStepFreq(d, 0) - 910.400f) < 0.001f && fabsf(scanStepFreq(d, 8) - 910.650f) < 0.001f);
  for (int b = 0; b < SCAN_BAND_STEPS; b++) d.band[b] = scanPack(-118);
  for (int sw = 0; sw < SCAN_SWEEPS; sw++) for (int b = 0; b < 9; b++) d.raw[sw][b] = scanPack(-117);
  f = scanAnalyse(d);
  CHECK(f.kind == SCAN_K_CLEAR && !f.blocker);
  for (int sw = 0; sw < SCAN_SWEEPS; sw++) d.raw[sw][2] = scanPack(-95);
  f = scanAnalyse(d);
  CHECK(f.kind == SCAN_K_EDGE_STEADY && f.blocker && f.blocker_khz == -63 && f.level == DIAG_CAUTION);
  // Energy on the channel's own steps is on channel, never a blocker.
  for (int sw = 0; sw < SCAN_SWEEPS; sw++) {
    d.raw[sw][2] = scanPack(-117);
    d.raw[sw][4] = scanPack(-100);
    d.raw[sw][3] = d.raw[sw][5] = scanPack(-104);
  }
  f = scanAnalyse(d);
  CHECK(f.kind == SCAN_K_MESH_STEADY && !f.blocker);
  // The peak says whether it sits exactly on our frequency or a measured step off it.
  CHECK(f.peak_khz == 0 && fabsf(f.peak_mhz - 910.525f) < 0.001f && f.db == 18);
  char text[40];
  scanSummary(f, text, sizeof(text));
  CHECK_STR(text, "on our freq, 18 dB");
  for (int sw = 0; sw < SCAN_SWEEPS; sw++) {
    d.raw[sw][4] = scanPack(-104);
    d.raw[sw][5] = scanPack(-98);
  }
  f = scanAnalyse(d);
  CHECK(f.kind == SCAN_K_MESH_STEADY && f.peak_khz == 31 && f.db == 20);
  scanSummary(f, text, sizeof(text));
  CHECK_STR(text, "+31k off ours, 20 dB");
}

static void engine_cases() {
  static EngineResult r;
  static BattView bv;
  EngineInput in;
  auto base = [&]() {
    memset(&in, 0, sizeof(in));
    in.store = &store;
    in.links = &links;
    in.noise = &noise;
    in.batt = &bv;
    in.now_hour = NOW;
    in.now_ms = MS;
    in.uptime_s = 86400;
    in.sf = SF;
    in.clock_set = in.batt_present = in.lna_available = in.lna_on = in.radio_sx126x = true;
    in.hist_kind = HIST_K_HOLDING;
    in.temp_kind = TEMP_K_NORMAL;
    in.has_temp = true;
    in.temp_now = 21;
    in.recv = 2300;
    in.errors = 10;
    in.air_s = 5184;
    in.agc_interval = 16;
    in.guard_known = in.guard_on = true;
    in.ev = &ev;
    bv = battAnalyse(store, NOW, 0, true);
  };
  auto runIt = [&]() {
    engineRun(in, r);
    outReset(out);
    engineFormat(in, r, out);
  };

  // R2: failing decodes, no noise, a very strong neighbour: it is named, not "collisions".
  quietDay();
  loud(12, 16, -112, 100, 15);
  linkInit(links);
  neighbour(0xA6B2, 28.0f, 30, 4);
  neighbour(0x4EA1, 9.0f, 30, 4);
  build();
  base();
  in.errors = 110;
  runIt();
  CHECK(r.cause == CAUSE_STRONG_NEIGHBOUR);
  OUT("A6B2 swamps the receiver");
  OUT("Decodes  \U0001F7E1 4.5%, strong nbr");

  // A carrier caught at the failures makes them interference.
  quietDay();
  loud(12, 16, -112, 100, 15);
  store.decode_capture = {at(13), 0, -100, -98, -99, 1};
  build();
  base();
  in.errors = 110;
  runIt();
  CHECK(r.cause == CAUSE_NOISE);
  OUT("4.5%, carrier");

  // R5 from the live floor check, with no study.
  quietDay();
  store.lna_check = {NOW * 3600 - 600, -95, -117, {0, 0}};
  build();
  base();
  in.errors = 73;
  in.recv = 2330;
  runIt();
  CHECK(r.cause == CAUSE_LNA_OVERLOAD);
  OUT("Noise    \U0001F7E1 floor +22 dB, from LNA");

  // R7: heat reboots lead when nothing else is wrong.
  quietDay();
  storeSample(store, NOW - 3, 0, 3900, 67);
  store.last_seen = (NOW - 3) * 3600 + 1800;
  storeRebootSeen(store, (NOW - 2) * 3600);
  for (uint32_t k = 0; k <= 24; k++) storeSample(store, NOW - 24 + k, 0, 3980, 21);
  build();
  base();
  runIt();
  CHECK(r.cause == CAUSE_HEAT);
  OUT("heat reboots");
  OUT("restarted after 67°C:");

  // The battery's last hour, a cheap live check, shows a turn before the day's trend does.
  quietDay();
  for (uint32_t k = 0; k <= 22; k++) storeSample(store, NOW - 24 + k, 0, 3990, 21);
  storeSample(store, NOW - 1, 0, 3990, 21);
  storeSample(store, NOW, 0, 3960, 21);
  storeSample(store, NOW, 0, 3960, 21);
  build();
  base();
  CHECK(bv.hour_mv == -30);
  runIt();
  OUT("Battery  \U0001F7E1 falling now, -30 mV/h");
  // One noisy reading a cent low is not a trend.
  quietDay();
  for (uint32_t k = 0; k <= 22; k++) storeSample(store, NOW - 24 + k, 0, 4230, 21);
  storeSample(store, NOW - 1, 0, 4230, 21);
  storeSample(store, NOW, 0, 4213, 21);
  storeSample(store, NOW, 0, 4230, 21);
  build();
  base();
  CHECK(bv.hour_mv > -15);
  runIt();
  CHECK(!outHas("falling now"));

  // U1: today's hours all look alike, but 2pm sits 8 dB above this site's usual for 2pm.
  quietDay();
  for (int d = 0; d < 7; d++) for (uint8_t h = 0; h < 24; h++) storeBaseHour(store, h, -112, true, 100, 0, 6);
  for (uint32_t k = 0; k < 24; k++) storeNoiseHour(store, NOW - 24 + k, -110, STORE_LNA_ON, true, 100, 0);
  storeNoiseHour(store, NOW - 24 + 14, -104, STORE_LNA_ON, true, 100, 0);
  storeNoiseHour(store, NOW - 24 + 13, -104, STORE_LNA_ON, true, 100, 0);
  storeNoiseHour(store, NOW - 24 + 15, -104, STORE_LNA_ON, true, 100, 0);
  build();
  base();
  runIt();
  CHECK(ev.usual.ready && ev.usual.above);
  // Today alone says loud 1pm-4pm; the usual sharpens it to how far off normal it is.
  relateNoise(ev, links, noise, note);
  NOTE("Try: diag radio.rfscan at 2pm.");
  // Uniformly raised: nothing loud against today, but every hour above the usual.
  quietDay();
  for (int d = 0; d < 7; d++) for (uint8_t h = 0; h < 24; h++) storeBaseHour(store, h, -112, true, 100, 0, 6);
  for (uint32_t k = 0; k < 24; k++) storeNoiseHour(store, NOW - 24 + k, -105, STORE_LNA_ON, true, 100, 0);
  build();
  base();
  runIt();
  CHECK(r.cause == CAUSE_NOISE);
  OUT("floor above usual at");
  relateNoise(ev, links, noise, note);
  NOTE("dB above its usual for");

  // U2: a neighbour 5 dB down over three days is named before its link drops.
  quietDay();
  uint32_t today = storeLocalDay(NOW, 0);
  for (int back = 10; back >= 1; back--) {
    storeNbDay(store, today - back, 0xC5FB, 2, back > 3 ? 12 : (int8_t)(7 - (3 - back)));
    storeNbDay(store, today - back, 0x4EA1, 2, 9);
  }
  build();
  base();
  runIt();
  CHECK(ev.fading.found && ev.fading.id == 0xC5FB);
  CHECK(r.cause == CAUSE_WEAK_LINK);
  OUT("Links    \U0001F7E1 C5FB fading, -");
  OUT("C5FB fading");
  relateLinks(ev, links, SF, MS, note);
  NOTE("C5FB down");
  NOTE("antenna or cable degrading.");

  // U4: a weekday schedule shapes the fix.
  quietDay();
  loud(8, 12, -104, 100, 1);
  today = storeLocalDay(NOW, 0);
  for (uint32_t back = 14; back >= 1; back--) storeLoudDay(store, today - back, trendWeekday(today - back) >= 5 ? 0 : 0xF00);
  build();
  base();
  runIt();
  CHECK(ev.schedule.found && ev.schedule.weekdays_only);
  OUT("loud weekdays every week:");
  OUT("on a weekday.");
  relateNoise(ev, links, noise, note);
  NOTE("Loud weekdays 8am-12pm, every week.");

  // U5: a green report ends with where things are heading.
  quietDay();
  for (int d = 0; d < 7; d++) for (uint8_t h = 0; h < 24; h++) storeBaseHour(store, h, -112, true, 100, 0, 6);
  for (int back = 10; back >= 1; back--) storeNbDay(store, storeLocalDay(NOW, 0) - back, 0x4EA1, 2, 9);
  for (uint32_t k = 0; k <= 30; k++) storeSample(store, NOW - 30 + k, 0, 3980, 21);
  build();
  base();
  runIt();
  CHECK(r.cause == CAUSE_NONE);
  OUT("Nothing to fix.\nTrend: floor usual, links steady");

  // Failed decodes with no noise rise, and a burst that caught LoRa that never decoded: another network.
  quietDay();
  loud(12, 16, -112, 100, 15);
  store.decode_capture = {at(13), 0, -100, -90, -97, 3};
  storeLoraSeen(store, at(13), true, false);
  build();
  base();
  in.errors = 110;
  runIt();
  // One hour of orphan preambles can be weak packets of our own: no verdict.
  CHECK(!ev.foreign_lora && r.cause != CAUSE_FOREIGN_LORA);
  CHECK(!outHas("other LoRa"));
  // A second hour of them with none of ours decoded names the other network.
  storeLoraSeen(store, at(15), true, false);
  build();
  base();
  in.errors = 110;
  runIt();
  CHECK(ev.foreign_lora && r.cause == CAUSE_FOREIGN_LORA);
  OUT("4.5%, other LoRa");
  OUT("another LoRa network here");
  // A scan that found foreign LoRa counts the same.
  quietDay();
  loud(8, 17, -104, 100, 1);
  store.noise_capture = {at(9), 0, -104, -90, -100, 3};
  storeLoraSeen(store, at(9), true, false);
  storeLoraSeen(store, at(11), true, false);
  // An hour that also decoded ours does not count.
  storeLoraSeen(store, at(14), true, true);
  build();
  base();
  runIt();
  CHECK(r.cause == CAUSE_FOREIGN_LORA);
  OUT("Noise    \U0001F7E1 Loud 8am-5pm, other LoRa");
  relateNoise(ev, links, noise, note);
  NOTE("network shares our frequency.");
  // Orphans in an hour that also decoded ours are our own weak packets: one clean hour is not enough.
  quietDay();
  loud(8, 17, -104, 100, 1);
  storeLoraSeen(store, at(9), true, false);
  storeLoraSeen(store, at(14), true, true);
  build();
  CHECK(!ev.foreign_lora);

  // The gate closed: the engine never names the LNA even with the floor check present.
  quietDay();
  store.lna_check = {NOW * 3600 - 600, -95, -117, {0, 0}};
  build(false, false);
  base();
  in.lna_available = in.lna_on = false;
  in.errors = 73;
  runIt();
  CHECK(!outHas("LNA") && !outHas("rxgain"));
}

static void probe_cases() {
  // Each leading cause is confirmed by the probe that measures it; loud noise gets a scan, not a burst.
  CHECK(strcmp(engineProbeFor(CAUSE_NOISE), "scan") == 0);
  CHECK(strcmp(engineProbeFor(CAUSE_COLLISIONS), "noise") == 0 && strcmp(engineProbeFor(CAUSE_STRONG_NEIGHBOUR), "noise") == 0);
  CHECK(strcmp(engineProbeFor(CAUSE_BLOCKER), "guard") == 0 && strcmp(engineProbeFor(CAUSE_GUARD_OFF), "guard") == 0);
  CHECK(strcmp(engineProbeFor(CAUSE_LNA_OVERLOAD), "lna") == 0 && strcmp(engineProbeFor(CAUSE_COLD), "batt") == 0);
  CHECK(strcmp(engineProbeFor(CAUSE_FAR_SIDE), "ping") == 0 && strcmp(engineProbeFor(CAUSE_WEAK_LINK), "ping") == 0);
  CHECK(strcmp(engineProbeFor(CAUSE_ANTENNA), "ping") == 0 && engineProbeFor(CAUSE_NONE)[0] == 0 && engineProbeFor(CAUSE_CLOCK)[0] == 0);
}

int main() {
  probe_cases();
  links_cases();
  noise_cases();
  source_cases();
  scan_cases();
  lna_cases();
  power_cases();
  traffic_cases();
  scan_spill_case();
  engine_cases();
  DONE();
}
