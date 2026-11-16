#include <helpers/diag_reports/DiagEngine.h>
#include <helpers/diag_reports/DiagLna.h>

#include "check.h"

#define NOW 500016U                    // local midnight at tz 0
#define MS  90000000UL
#define SF  7

// A whole node's evidence, built by hand and run through the engine.
struct Rig {
  DiagStore store;
  LinkTable links;
  NoiseView noise;
  BattView batt;
  EngineInput in;
  EngineResult r;
  DiagOut out;
  DiagEvidence ev;
  float margin;
  bool use_ev = true;

  // A healthy node: quiet channel, three solid links that hear this node, steady battery, light load.
  void healthy() {
    storeInit(store);
    linkInit(links);
    memset(&in, 0, sizeof(in));
    for (uint32_t k = 0; k <= 30; k++) storeSample(store, NOW - 30 + k, 0, 3980, 21);
    for (uint32_t k = 0; k < 24; k++) {
      storeNoiseHour(store, NOW - 24 + k, -112, STORE_LNA_ON, true, 100, 0);
      storeLoadHour(store, NOW - 24 + k, 0, 6);
    }
    for (uint8_t h = 0; h < 24; h++) {
      linkRollHour(links, h);
      neighbour(0xC5FB, 4.0f, 3, 2);
      neighbour(0x4EA1, 9.0f, 3, 2);
      neighbour(0xA6B2, 12.0f, 3, 2);
    }
    in.store = &store;
    in.links = &links;
    in.noise = &noise;
    in.batt = &batt;
    in.now_hour = NOW;
    in.now_ms = MS;
    in.uptime_s = 86400;
    in.sf = SF;
    in.clock_set = in.batt_present = in.lna_available = in.lna_on = in.radio_sx126x = in.have_scan = true;
    in.scan.level = DIAG_GOOD;
    in.hist_kind = HIST_K_HOLDING;
    in.temp_kind = TEMP_K_NORMAL;
    in.has_temp = true;
    in.temp_now = 21;
    in.study_result = LNA_V_HELPS;
    in.study_percent_on = 18;
    in.recv = 2400;
    in.errors = 10;
    in.air_s = 5184;
    in.agc_interval = 16;
    in.guard_known = in.guard_on = true;
    margin = NOISE_NO_MARGIN;
  }

  // A fresh store with the healthy channel and a battery falling `step` mV an hour from `start`.
  void battRamp(uint16_t start, int step, float temp) {
    storeInit(store);
    for (uint32_t k = 0; k <= 24; k++) storeSample(store, NOW - 24 + k, 0, (uint16_t)(start - step * (int)k), temp);
    for (uint32_t k = 0; k < 24; k++) {
      storeNoiseHour(store, NOW - 24 + k, -112, STORE_LNA_ON, true, 100, 0);
      storeLoadHour(store, NOW - 24 + k, 0, 6);
    }
  }

  void neighbour(uint16_t id, float snr, int packets, int echoes) {
    int index = linkIndex(links, id, 2, MS);
    for (int p = 0; p < packets; p++) linkAddPacket(links, index, snr, SF, 1, MS);
    for (int e = 0; e < echoes; e++) linkAddEcho(links, index);
  }

  // Raises the floor in local hours [from, to) and sets the hour's decode counts.
  void loud(int from, int to, int8_t floor, uint16_t recv, uint16_t errors) {
    for (int l = from; l < to; l++) storeNoiseHour(store, NOW - 24 + l, floor, STORE_LNA_ON, true, recv, errors);
  }

  void freezes(int local_hour, int count) {
    for (int i = 0; i < count; i++) storeFreeze(store, (NOW - 24 + local_hour) * 3600 + 60 * i, 0);
  }

  void run() {
    char weakest[8];
    if (!linkWeakest(links, SF, MS, &margin, weakest, sizeof(weakest))) margin = NOISE_NO_MARGIN;
    noise = noiseAnalyse(store, NOW, in.tz, in.lna_available ? (in.lna_on ? STORE_LNA_ON : STORE_LNA_OFF) : STORE_LNA_NA, margin);
    batt = battAnalyse(store, NOW, in.tz, true);
    EvidenceSources src = {&store, &links, &noise, NOW, MS, in.tz, SF, (float)in.temp_now, in.lna_available, in.lna_on, nullptr, false, 0, 0, 0};
    evidenceBuild(ev, src);
    in.ev = use_ev ? &ev : nullptr;
    engineRun(in, r);
    outReset(out);
    engineFormat(in, r, out);
  }

  bool has(const char* text) const { return strstr(out.buf, text) != nullptr; }
};

static Rig rig;

// Every line fits a phone and the reply limits; a See: line carries a command and is as long as the command.
static void checkShape(const char* label) {
  char line[200];
  if (rig.out.truncated || outRowCount(rig.out) > DIAG_ROWS_MAX) {
    printf("FAIL %s: output truncated or too many rows\n", label);
    failures++;
  }
  for (int i = 0; outGetLine(rig.out, i, line, sizeof(line)); i++) {
    int chars = 0;
    for (const char* c = line; *c; c++) chars += (*c & 0xC0) != 0x80;
    if (chars > 37 && strncmp(line, "See: ", 5) != 0) {
      printf("FAIL %s line too wide (%d): %s\n", label, chars, line);
      failures++;
    }
  }
}

#define CAUSE_IS(c) do { if (rig.r.cause != (c)) { printf("FAIL %s:%d cause %s, want %s\n%s\n", __FILE__, __LINE__, engineCauseName(rig.r.cause), engineCauseName(c), rig.out.buf); failures++; } } while (0)
#define HAS(text) do { if (!rig.has(text)) { printf("FAIL %s:%d missing '%s' in\n%s\n", __FILE__, __LINE__, text, rig.out.buf); failures++; } } while (0)
#define HAS_NOT(text) do { if (rig.has(text)) { printf("FAIL %s:%d unexpected '%s' in\n%s\n", __FILE__, __LINE__, text, rig.out.buf); failures++; } } while (0)

// The noise site of the proposal: loud 8am-5pm, decodes failing in those hours, one link lost in them.
static void noiseSite() {
  rig.healthy();
  rig.loud(8, 17, -104, 100, 12);
  linkInit(rig.links);
  for (uint8_t h = 0; h < 24; h++) {
    linkRollHour(rig.links, h);
    if (h < 8 || h >= 17) rig.neighbour(0xC5FB, 4.0f, 3, 2);
    rig.neighbour(0x4EA1, 9.0f, 3, 2);
    rig.neighbour(0xA6B2, 12.0f, 3, 2);
  }
  rig.in.recv = 2300;
  rig.in.errors = 110;
}

int main() {
  // All clear.
  rig.healthy();
  rig.run();
  CAUSE_IS(CAUSE_NONE);
  HAS("\U0001F517 Links    \U0001F7E2 3 strong, all hear us");
  HAS("\U0001F4F6 LNA      \U0001F7E2 on helps (+18%)");
  HAS("\U0001F4E1 Nearby   \U0001F7E2 nothing close");
  HAS("\U0001F50A Noise    \U0001F7E2 Quiet Site");
  HAS("\U0001F6E1 Guard    \U0001F7E2 No Freezes");
  HAS("\U0001F4C9 Decodes  \U0001F7E2 0.4% failed");
  HAS("🔋 Battery  \U0001F7E2 holding, 3.98V");
  HAS("\U0001F4CA Load     \U0001F7E2 channel 6% busy");
  HAS("\U0001F9ED Paths    \U0001F7E2 spread over 3");
  HAS("~\n🩺 Diag     \U0001F7E2 all good\nNothing to fix.");
  checkShape("all clear");

  // One cause, one fix: noise explains the link and the decodes, and gives the only fix.
  noiseSite();
  rig.run();
  CAUSE_IS(CAUSE_NOISE);
  CHECK(rig.r.area[AREA_LINKS].cause == CAUSE_NOISE && rig.r.area[AREA_DECODES].cause == CAUSE_NOISE && rig.r.explained == 3);
  HAS("Links    \U0001F7E1 C5FB drops in noise");
  HAS("Noise    \U0001F534 Loud 8am-5pm");
  HAS("Decodes  \U0001F7E1 4.5%, 100% in noise");
  HAS("🩺 Diag     \U0001F534 noise 8am-5pm");
  HAS("Best fix: scan at 9am to find it.\nSee: diag radio.rfscan");
  HAS_NOT("Higher antenna");
  CHECK(rig.r.score[CAUSE_NOISE] > rig.r.score[CAUSE_COLLISIONS] && rig.r.score[CAUSE_WEAK_LINK] == 0);
  checkShape("noise site");

  // Recurrence turns a sighting into a measured confidence.
  noiseSite();
  rig.run();
  HAS_NOT("seen");
  uint32_t today = storeLocalDay(NOW, 0);
  for (uint32_t d = 4; d >= 1; d--) storeRecurSet(rig.store, today - d, CAUSE_NOISE, rig.noise.loud_mask);
  rig.run();
  HAS("(sure: seen 5 of 5 days)");
  storeRecurSet(rig.store, today - 1, CAUSE_NONE, 0);
  storeRecurSet(rig.store, today - 1, CAUSE_NONE, 0);
  rig.run();
  HAS("(sure: seen 4 of 5 days)");
  // The same cause at other hours is a different pattern.
  noiseSite();
  storeRecurSet(rig.store, today - 1, CAUSE_NOISE, 0x3UL);
  rig.run();
  HAS("(seen 1 of 2 days)");
  // A young watch says so.
  rig.healthy();
  storeInit(rig.store);
  for (uint32_t k = 14; k < 24; k++) storeNoiseHour(rig.store, NOW - 24 + k, (int8_t)(k >= 20 ? -104 : -112), STORE_LNA_ON, true, 100, 1);
  rig.run();
  CAUSE_IS(CAUSE_NOISE);
  HAS("(early: 10h seen)");

  // A captured carrier is quoted in the fix.
  noiseSite();
  rig.store.noise_capture = {(NOW - 24 + 8) * 3600 + 42 * 60, 0, -103, -101, -102, 1};
  rig.run();
  HAS("Best fix: a steady carrier,\ncaught 8:42; scan at 9am to find it.");
  checkShape("noise capture");

  // Loud hours that are simply the mesh being busy are congestion, not interference.
  rig.healthy();
  rig.loud(8, 17, -106, 400, 4);
  rig.run();
  CHECK(rig.r.area[AREA_NOISE].cause == CAUSE_CONGESTION && rig.r.score[CAUSE_NOISE] < rig.r.score[CAUSE_CONGESTION]);
  HAS("busy mesh");

  // Freezes from a blocker beside the channel.
  rig.healthy();
  rig.freezes(8, 3);
  rig.freezes(9, 1);
  rig.in.scan = {SCAN_K_EDGE_STEADY, DIAG_CAUTION, -250, 14, -118, 0, 0, true, -250, 14};
  rig.in.recv = 2340;
  rig.in.errors = 62;
  rig.run();
  CAUSE_IS(CAUSE_BLOCKER);
  HAS("Nearby   \U0001F7E1 strong 250k off ch");
  HAS("Guard    \U0001F534 4 Freezes, blocker");
  HAS("Decodes  \U0001F7E1 2.5%, in freezes");
  HAS("🩺 Diag     \U0001F534 radio freezing");
  HAS("Best fix: a transmitter beside the\nchannel freezes the receiver.\nSee: tryset 86400 radio.fem.rxgain off");
  CHECK(rig.r.area[AREA_DECODES].cause == CAUSE_BLOCKER && rig.r.area[AREA_NEARBY].cause == CAUSE_BLOCKER);
  checkShape("blocker");
  // With the capture taken at the freeze, the fix quotes it.
  rig.store.scan_capture = {(NOW - 24 + 9) * 3600 + 14 * 60, -250, 14, -62, 0, 1};
  rig.run();
  HAS("Best fix: caught 9:14, -62 dBm\n-250k off channel; freezes 8am-10am.");
  CHECK(rig.r.score[CAUSE_BLOCKER] == 95);
  checkShape("blocker capture");
  // A board that cannot switch its LNA gets another fix and no LNA word anywhere.
  rig.in.lna_available = rig.in.lna_on = false;
  rig.store.scan_capture.at = 0;
  rig.run();
  CAUSE_IS(CAUSE_BLOCKER);
  HAS("move the antenna away from it.\nSee: diag radio.rfscan");
  HAS_NOT("LNA");
  HAS_NOT("rxgain");

  // Freezes with nothing caught: reported as seen, never as a guessed blocker.
  rig.healthy();
  rig.freezes(8, 2);
  rig.freezes(9, 1);
  rig.run();
  CAUSE_IS(CAUSE_FREEZES);
  HAS("Guard    \U0001F7E1 3 Freezes, Cleared");
  HAS("🩺 Diag     \U0001F7E1 radio froze 3x");
  HAS("Best fix: cause not on air at\nscan; freezes cluster 8am-10am.\nSee: diag radio.rfscan at 8am");
  HAS_NOT("blocker");
  checkShape("freezes unknown");

  // Freezes while the AGC reset is off: nothing clears them.
  rig.healthy();
  rig.freezes(14, 3);
  rig.in.agc_interval = 0;
  rig.run();
  CAUSE_IS(CAUSE_GUARD_OFF);
  HAS("Guard    \U0001F534 3 Freezes, AGC Off");
  HAS("See: set agc.reset.interval 16");
  // Guard bit lost with the AGC reset off, no freeze yet.
  rig.healthy();
  rig.in.agc_interval = 0;
  rig.in.guard_on = false;
  rig.run();
  CAUSE_IS(CAUSE_GUARD_OFF);
  HAS("Guard    \U0001F534 Off, AGC Off");
  HAS("unprotected, AGC off");
  // Guard bit lost but restored by the reset: a caution with no cause to chase.
  rig.healthy();
  storeGuardOff(rig.store, (NOW - 3) * 3600, 0x04, 0x05);
  storeGuardOff(rig.store, (NOW - 2) * 3600, 0x04, 0x05);
  rig.run();
  HAS("Guard    \U0001F7E1 Off 2x, restored");
  CAUSE_IS(CAUSE_NONE);
  // Guard bit still off with the AGC reset running: the reset failed, and that leads.
  rig.healthy();
  storeGuardOff(rig.store, (NOW - 1) * 3600, 0x04, 0x04);
  rig.in.guard_on = false;
  rig.run();
  CAUSE_IS(CAUSE_GUARD_OFF);
  HAS("Guard    \U0001F534 Off, not restored");
  HAS("guard off, not restored");
  HAS("See: reboot");
  // Frozen at this moment.
  rig.healthy();
  rig.in.frozen_now = true;
  rig.run();
  HAS("Guard    \U0001F534 Frozen Now");
  // A neighbour whose direct-retry resends go unconfirmed: one-way, with the counts in the fix.
  rig.healthy();
  rig.in.retry_one_way = 1;
  rig.in.retry_tried = 12;
  rig.in.retry_won = 2;
  rig.run();
  CAUSE_IS(CAUSE_FAR_SIDE);
  HAS("resends fail");
  HAS("10 of 12 resends unconfirmed:");
  // Radios without the registers show the area as not applicable.
  rig.healthy();
  rig.in.radio_sx126x = rig.in.have_scan = false;
  rig.run();
  HAS("Guard    ⚪ not on this radio");
  HAS_NOT("Nearby");
  CAUSE_IS(CAUSE_NONE);

  // Front-end overload: the LNA lifts the floor and decodes fall; one cause for LNA, Noise and Decodes.
  rig.healthy();
  rig.in.study_result = LNA_V_NONE;
  rig.in.study_floor_shift = 22;
  rig.in.recv = 2330;
  rig.in.errors = 73;
  rig.run();
  CAUSE_IS(CAUSE_LNA_OVERLOAD);
  HAS("LNA      \U0001F7E1 on hurts here");
  HAS("Noise    \U0001F7E1 floor +22 dB, from LNA");
  HAS("Decodes  \U0001F7E1 3.0%, from LNA");
  HAS("🩺 Diag     \U0001F7E1 LNA overload\nBest fix: turn the LNA off.\nSee: tryset 86400 radio.fem.rxgain off");
  CHECK(rig.r.explained == 3);
  checkShape("overload");
  // The same floor shift with clean decodes is a working LNA, not overload.
  rig.in.errors = 8;
  rig.run();
  CAUSE_IS(CAUSE_NONE);

  // Failed decodes with no noise rise, no freezes and a quiet channel: collisions.
  rig.healthy();
  rig.in.recv = 2300;
  rig.in.errors = 110;
  rig.run();
  CAUSE_IS(CAUSE_COLLISIONS);
  HAS("Decodes  \U0001F7E1 4.5% failed");
  HAS("packets colliding");
  HAS("Best fix: repeats overlap: spread them,\nraise txdelay.");
  HAS("See: set txdelay 1");
  // On a busy channel the same failures follow the load.
  rig.in.air_s = 26784;
  rig.run();
  CAUSE_IS(CAUSE_CONGESTION);
  HAS("Load     \U0001F7E1 channel 31% busy");
  HAS("Decodes  \U0001F7E1 4.5%, from load");
  HAS("Best fix: a faster preset can help.");
  HAS_NOT("flood-gate");
  CHECK(rig.r.explained == 2);

  // Battery: falling with the CPU below freezing is the cold, not the panel.
  rig.healthy();
  rig.battRamp(3970, 5, -2);
  rig.in.temp_now = -2;
  rig.run();
  CAUSE_IS(CAUSE_COLD);
  HAS("Battery  \U0001F7E1 draining, 3.85V");
  HAS("🩺 Diag     \U0001F7E1 cold, not charging\nBest fix: CPU -2°C: chargers\npause; it resumes when it warms.");
  checkShape("cold");
  // The same fall in the warm is the panel.
  rig.healthy();
  rig.battRamp(3970, 5, 21);
  rig.run();
  CAUSE_IS(CAUSE_PANEL);
  HAS("battery not refilling");

  // Battery loss that tracks busy days over the month: drain follows load, not the panel.
  rig.healthy();
  storeInit(rig.store);
  uint16_t mv = 4100;
  for (int day = 20; day >= 1; day--) {
    bool busy = day % 2 == 0;
    uint32_t midnight = NOW - 24U * day;
    storeSample(rig.store, midnight + 6, 0, mv, 20);
    for (uint32_t h = 0; h < 24; h++) storeLoadHour(rig.store, midnight + h, 0, busy ? 35 : 6);
    mv = (uint16_t)(busy ? mv - 60 : mv + 40);
  }
  CHECK(engineLoadDrain(rig.store));
  for (uint32_t k = 0; k <= 24; k++) storeSample(rig.store, NOW - 24 + k, 0, (uint16_t)(3990 - 4 * k + (k % 6 == 0 ? 12 : 0)), 21);
  for (uint32_t k = 0; k < 24; k++) storeNoiseHour(rig.store, NOW - 24 + k, -112, STORE_LNA_ON, true, 100, 0);
  rig.run();
  CAUSE_IS(CAUSE_LOAD_DRAIN);
  HAS("🩺 Diag     \U0001F7E1 drain follows load");
  // Without the correlation the month says nothing about load.
  rig.healthy();
  CHECK(!engineLoadDrain(rig.store));

  // Battery low outranks everything.
  noiseSite();
  rig.battRamp(3340, 0, 21);
  rig.loud(8, 17, -104, 100, 12);
  rig.run();
  CAUSE_IS(CAUSE_BATT_LOW);
  HAS("Battery  \U0001F534 low, 3.34V");
  HAS("Decodes  \U0001F7E1 4.5%, 100% in noise");

  // One-way link, then cleared by a probe ping.
  rig.healthy();
  linkInit(rig.links);
  rig.neighbour(0xC5FB, 4.0f, 40, 0);
  rig.neighbour(0x4EA1, 9.0f, 30, 14);
  rig.neighbour(0xA6B2, 12.0f, 25, 9);
  rig.run();
  CAUSE_IS(CAUSE_FAR_SIDE);
  HAS("🩺 Diag     \U0001F7E1 C5FB one-way");
  HAS("See: diag probe");
  rig.in.cleared_one_way = 0xC5FB;
  rig.run();
  CAUSE_IS(CAUSE_NONE);
  HAS("Links    \U0001F7E2 two-way, confirmed");

  // Most links weak points at this node's antenna.
  rig.healthy();
  linkInit(rig.links);
  rig.neighbour(0xC5FB, -6.0f, 40, 6);
  rig.neighbour(0x4EA1, -5.5f, 30, 4);
  rig.neighbour(0xA6B2, 4.0f, 25, 3);
  rig.run();
  CAUSE_IS(CAUSE_ANTENNA);
  HAS("Links    \U0001F534 2 of 3 weak");

  // Paths that lean on a weak link are one problem, not two.
  rig.healthy();
  linkInit(rig.links);
  rig.neighbour(0xC5FB, -6.0f, 90, 6);
  rig.neighbour(0x4EA1, 9.0f, 10, 4);
  rig.neighbour(0xA6B2, 12.0f, 8, 3);
  rig.run();
  CAUSE_IS(CAUSE_WEAK_LINK);
  HAS("Paths    \U0001F7E1 leans on weak C5FB");
  HAS("🩺 Diag     \U0001F7E1 paths lean on weak C5FB");
  CHECK(rig.r.explained == 2);
  // A dominant but solid path is only a backup question.
  rig.healthy();
  linkInit(rig.links);
  rig.neighbour(0xC5FB, 9.0f, 90, 6);
  rig.neighbour(0x4EA1, 9.0f, 10, 4);
  rig.run();
  CAUSE_IS(CAUSE_SINGLE_PATH);
  HAS("Paths    \U0001F7E1 90% via C5FB");

  // LNA study results: fault, advice not followed, and a study in progress.
  rig.healthy();
  rig.in.study_result = LNA_V_NOT_WORKING;
  rig.run();
  CAUSE_IS(CAUSE_LNA_FAULT);
  HAS("LNA      \U0001F534 not working");
  rig.healthy();
  rig.in.lna_on = false;
  rig.run();
  CAUSE_IS(CAUSE_LNA_SETTING);
  HAS("LNA      \U0001F7E1 off, study says on");
  HAS("See: tryset 86400 radio.fem.rxgain on");
  rig.healthy();
  rig.in.study_active = true;
  rig.in.study_h = 5;
  rig.in.study_of_h = 48;
  rig.run();
  HAS("LNA      ⚪ studying, 5 of 48h");
  HAS("🩺 Diag     ⚪ still learning\nCheck again in a day.");

  // Heat has no area of its own, yet leads when nothing else is wrong.
  rig.healthy();
  rig.in.temp_kind = TEMP_K_HOT;
  rig.run();
  CAUSE_IS(CAUSE_HEAT);
  HAS("🩺 Diag     \U0001F7E1 running hot");
  HAS("See: diag temp");

  // A fresh node is learning, not healthy; the automatic-capture state is always visible.
  rig.healthy();
  storeInit(rig.store);
  linkInit(rig.links);
  rig.in.uptime_s = 120;
  rig.in.recv = 4;
  rig.in.errors = 0;
  rig.in.have_scan = false;
  rig.in.study_result = LNA_V_NONE;
  rig.in.auto_state = ENGINE_AUTO_PAUSED;
  rig.in.pause_min = 4;
  rig.run();
  CAUSE_IS(CAUSE_NONE);
  HAS("Noise    ⚪ watching, 0 of 24h");
  HAS("Load     ⚪ 2 of 10 min up");
  HAS("🩺 Diag     ⚪ still learning\nCheck again in a day.\nAuto captures paused 4m.");
  checkShape("fresh");
  rig.in.auto_state = ENGINE_AUTO_OFF;
  rig.in.clock_set = false;
  rig.run();
  HAS("Noise    ⚪ clock not set");
  HAS("Battery  ⚪ clock not set");
  HAS("Auto captures off.");

  // The LNA gate closed: no scenario may print an LNA word, fix or command.
  for (int scenario = 0; scenario < 4; scenario++) {
    if (scenario == 0) rig.healthy();
    if (scenario == 1) noiseSite();
    if (scenario == 2) {
      rig.healthy();
      rig.freezes(8, 4);
      rig.in.scan = {SCAN_K_EDGE_STEADY, DIAG_CAUTION, -250, 14, -118, 0, 0, true, -250, 14};
    }
    if (scenario == 3) {
      rig.healthy();
      rig.in.study_floor_shift = 22;
      rig.in.study_result = LNA_V_OVERLOADED;
      rig.in.errors = 200;
    }
    rig.in.lna_available = rig.in.lna_on = false;
    rig.run();
    HAS_NOT("LNA");
    HAS_NOT("rxgain");
    CHECK(!rig.r.area[AREA_LNA].shown && rig.r.score[CAUSE_LNA_OVERLOAD] == 0 && rig.r.score[CAUSE_LNA_FAULT] == 0 && rig.r.score[CAUSE_LNA_SETTING] == 0);
  }

  CHECK(engineRotate(0x000001, -6) == 0x040000 && engineRotate(0x040000, 6) == 0x000001 && engineRotate(0xFFFFFF, 5) == 0xFFFFFF);
  DONE();
}
