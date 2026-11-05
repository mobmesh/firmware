#include <helpers/diag_report/LnaStudy.h>

#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

static LnaStudy fresh() {
  LnaStudy s;
  memset(&s, 0, sizeof(s));
  s.active = 1;
  s.start_lna = LNA_ON;
  s.requested_s = 48 * 3600;
  s.clock_valid = 1;
  return s;
}

static void block(LnaBlock& b, int floor, int a, int bb, int snr_q_a = 0, int snr_q_b = 0) {
  memset(&b, 0, sizeof(b));
  b.floor_dbm = floor;
  b.floor_valid = true;
  b.heard[0] = a;
  b.heard[1] = bb;
  b.snr_q[0] = snr_q_a * a;
  b.snr_q[1] = snr_q_b * bb;
  b.snr_n[0] = a;
  b.snr_n[1] = bb;
  b.unique = a + bb;
}

// Two neighbours registered, then `pairs` pairs where the on block hears on_a/on_b and off hears off_a/off_b.
static void feed(LnaStudy& s, int pairs, int on_a, int on_b, int off_a, int off_b,
                 int on_floor = -70, int off_floor = -95, int hour = -1) {
  lnaNeighbourIndex(s, 0x11, 1);
  lnaNeighbourIndex(s, 0x22, 1);
  for (int i = 0; i < pairs; i++) {
    LnaBlock on, off;
    block(on, on_floor, on_a, on_b, 40, 40);
    block(off, off_floor, off_a, off_b, 40, 40);
    lnaAddPair(s, on, off, hour < 0 ? -1 : (hour + i) % 24, 0);
  }
}

static void testSignificance() {
  CHECK(!lnaSignificant(0, 0));
  CHECK(lnaSignificant(37, 60));
  CHECK(!lnaSignificant(36, 60));
}

static void testPairWinnerWeightsNeighbours() {
  LnaBlock on, off;
  block(on, -70, 100, 0);
  block(off, -95, 90, 2);
  CHECK(lnaPairWinner(on, off, 2) < 0);
}

static void testHelps() {
  LnaStudy s = fresh();
  feed(s, 40, 6, 6, 4, 4);
  s.elapsed_s = 30 * 3600;
  CHECK(lnaVerdict(s, 7, true) == LNA_V_HELPS);
  CHECK(lnaShouldStop(s, 7));
}

static void testOverloaded() {
  LnaStudy s = fresh();
  feed(s, 40, 4, 4, 6, 6);
  CHECK(lnaVerdict(s, 7, true) == LNA_V_OVERLOADED);
}

static void testNoBenefitNeedsSixtyPairs() {
  LnaStudy s = fresh();
  feed(s, 40, 5, 5, 5, 5);
  CHECK(lnaVerdict(s, 7, true) == LNA_V_NOT_ENOUGH);
  feed(s, 30, 5, 5, 5, 5);
  CHECK(lnaVerdict(s, 7, true) == LNA_V_NOT_ENOUGH);
}

static void testNoBenefitAlternating() {
  LnaStudy s = fresh();
  feed(s, 35, 6, 6, 5, 5);
  feed(s, 35, 5, 5, 6, 6);
  CHECK(lnaVerdict(s, 7, true) == LNA_V_NO_BENEFIT);
}

static void testNotWorking() {
  LnaStudy s = fresh();
  feed(s, 3, 5, 5, 5, 5, -94, -95);
  CHECK(lnaVerdict(s, 7, false) == LNA_V_NOT_WORKING);
  CHECK(lnaShouldStop(s, 7));
}

static void testLostNeighbourKeepsOn() {
  LnaStudy s = fresh();
  feed(s, 20, 3, 6, 0, 7);
  feed(s, 20, 3, 6, 0, 5);
  CHECK(lnaLostNeighbour(s) == 0);
  CHECK(lnaVerdict(s, 7, true) == LNA_V_KEEP_ON_LOST);
}

static void testNotEnoughNeighbours() {
  LnaStudy s = fresh();
  feed(s, 40, 6, 0, 4, 0);
  CHECK(lnaVerdict(s, 7, true) == LNA_V_NOT_ENOUGH);
  CHECK(lnaVerdict(s, 7, false) == LNA_V_NONE);
}

static void testEarlyStopWaitsADay() {
  LnaStudy s = fresh();
  feed(s, 40, 6, 6, 4, 4);
  s.elapsed_s = 10 * 3600;
  CHECK(!lnaShouldStop(s, 7));
}

static void testOppositeRun() {
  LnaStudy s = fresh();
  for (int h = 0; h < 24; h++) {
    bool off_hours = h >= 14 && h < 22;
    s.hour_wins[0][h][off_hours ? LNA_OFF : LNA_ON] = 8;
    s.hour_wins[1][h][off_hours ? LNA_OFF : LNA_ON] = 8;
  }
  LnaRun run = lnaOppositeRun(s, LNA_ON);
  CHECK(run.found);
  CHECK(run.start_hour == 14 && run.end_hour == 22);
  CHECK(run.state == LNA_OFF);
  // Day 1 disagrees inside the window while the two-day total still favours off: no note.
  for (int h = 14; h < 22; h++) {
    s.hour_wins[0][h][LNA_OFF] = 30;
    s.hour_wins[1][h][LNA_ON] = 9;
  }
  CHECK(!lnaOppositeRun(s, LNA_ON).found);
}

static void testFormatting() {
  LnaStudy s = fresh();
  feed(s, 40, 4, 4, 6, 6);
  s.elapsed_s = 48 * 3600;
  s.result = lnaVerdict(s, 7, true);
  char lines[12][48];
  int n = lnaFormatResult(s, LNA_ON, 7, 0, lines, 12);
  CHECK(n >= 5);
  CHECK(strstr(lines[0], "🔴 Overloaded") != nullptr);
  CHECK(strstr(lines[n - 3], "Now: on. To test off:") != nullptr);
  CHECK(strstr(lines[n - 2], "radio.fem.rxgain off") != nullptr);
  for (int i = 0; i < n; i++) CHECK(strlen(lines[i]) < 48);
}

// The header and last line follow the live LNA, whatever state the study began in.
static void testFollowsLiveState() {
  char lines[12][48];
  LnaStudy s = fresh();
  s.start_lna = LNA_ON;
  s.elapsed_s = 48 * 3600;

  s.result = LNA_V_OVERLOADED;
  int n = lnaFormatResult(s, LNA_OFF, 7, 0, lines, 12);
  CHECK(strstr(lines[0], "🟢 Overloaded (Study)") != nullptr);
  CHECK(strcmp(lines[n - 1], "Now: off, as advised. Nothing to change.") == 0);

  s.result = LNA_V_NO_BENEFIT;
  n = lnaFormatResult(s, LNA_OFF, 7, 0, lines, 12);
  CHECK(strstr(lines[0], "🟢 No Benefit Here (Study)") != nullptr);
  CHECK(strcmp(lines[n - 1], "Now: off, as advised. Nothing to change.") == 0);
  n = lnaFormatResult(s, LNA_ON, 7, 0, lines, 12);
  CHECK(strstr(lines[0], "🟡 No Benefit Here") != nullptr);
  CHECK(strstr(lines[0], "Study") == nullptr);
  CHECK(strcmp(lines[n - 1], "then: tryset keep") == 0);

  s.result = LNA_V_HELPS;
  n = lnaFormatResult(s, LNA_ON, 7, 0, lines, 12);
  CHECK(strstr(lines[0], "🟢 Keep It On") != nullptr);
  CHECK(strcmp(lines[n - 1], "Now: on, as advised. Nothing to change.") == 0);
  n = lnaFormatResult(s, LNA_OFF, 7, 0, lines, 12);
  CHECK(strstr(lines[0], "🟡 Keep It On") != nullptr);
  CHECK(strstr(lines[n - 2], "radio.fem.rxgain on") != nullptr);

  s.result = LNA_V_NOT_WORKING;
  n = lnaFormatResult(s, LNA_OFF, 7, 0, lines, 12);
  CHECK(strstr(lines[0], "🔴 Not Working") != nullptr);
  CHECK(strcmp(lines[n - 1], "Study stopped. Now: off.") == 0);

  s.result = LNA_V_NOT_ENOUGH;
  n = lnaFormatResult(s, LNA_OFF, 7, 0, lines, 12);
  CHECK(strcmp(lines[n - 1], "Now: off. Try again later.") == 0);
}

static bool noted(const char* text, int8_t tz) {
  LnaStudy s = fresh();
  feed(s, 40, 4, 4, 6, 6);
  s.elapsed_s = 48 * 3600;
  s.result = lnaVerdict(s, 7, true);
  for (int h = 0; h < 24; h++) {
    bool on_hours = h >= 14 && h < 22;
    s.hour_wins[0][h][on_hours ? LNA_ON : LNA_OFF] = 8;
    s.hour_wins[1][h][on_hours ? LNA_ON : LNA_OFF] = 8;
  }
  char lines[12][48];
  int n = lnaFormatResult(s, LNA_ON, 7, tz, lines, 12);
  for (int i = 0; i < n; i++) if (strstr(lines[i], text)) return true;
  return false;
}

// Hours are stored UTC, so the same study reads correctly under any offset.
static void testRunFollowsOffset() {
  CHECK(noted("On better 14-22 UTC", 0));
  CHECK(noted("On better 9am-5pm", -5));
  CHECK(noted("On better 11pm-7am", 9));
}

static void testParseTz() {
  int8_t h = 0;
  CHECK(lnaParseTz("-5h", &h) && h == -5);
  CHECK(lnaParseTz("+14", &h) && h == 14);
  CHECK(lnaParseTz("3", &h) && h == 3);
  CHECK(!lnaParseTz("-13", &h));
  CHECK(!lnaParseTz("5.5h", &h));
  CHECK(!lnaParseTz("abc", &h));
}

int main() {
  testSignificance();
  testPairWinnerWeightsNeighbours();
  testHelps();
  testOverloaded();
  testNoBenefitNeedsSixtyPairs();
  testNoBenefitAlternating();
  testNotWorking();
  testLostNeighbourKeepsOn();
  testNotEnoughNeighbours();
  testEarlyStopWaitsADay();
  testOppositeRun();
  testFormatting();
  testFollowsLiveState();
  testRunFollowsOffset();
  testParseTz();
  printf(failures ? "%d FAILED\n" : "OK\n", failures);
  return failures ? 1 : 0;
}
