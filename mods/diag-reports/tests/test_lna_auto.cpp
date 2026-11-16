#include <helpers/diag_reports/DiagLnaAuto.h>

#include "check.h"

#define DAY 20000
#define T0  (DAY * 86400UL)

static AutoTest pair(bool off_reply, bool on_reply, float off_back, float on_back, float out_off = 5, float out_on = 5) {
  AutoTest t;
  t.reply[0] = off_reply;
  t.reply[1] = on_reply;
  t.back_db[0] = off_back;
  t.back_db[1] = on_back;
  t.out_db[0] = out_off;
  t.out_db[1] = out_on;
  t.floor_dbm[0] = -117;
  t.floor_dbm[1] = -95;
  return t;
}

int main() {
  static LnaAutoState a;
  char text[48], row[AUTO_BLOCKS + 1];

  // Blocks follow local even hours.
  CHECK(autoBlockOf(0) == 0 && autoBlockOf(13) == 6 && autoBlockOf(23) == 11);

  // Learning starts at the default, tests every 10 minutes, under a daily cap.
  autoInit(a, true, DAY);
  CHECK(autoApplied(a, 6) && autoLearning(a, DAY, 6));
  CHECK(autoTestDue(a, T0 + 100, DAY, 6));
  autoAddTest(a, 6, pair(true, true, 3, 3), T0 + 100, DAY);
  CHECK(!autoTestDue(a, T0 + 400, DAY, 6) && autoTestDue(a, T0 + 700, DAY, 6));
  CHECK(!autoTestDue(a, 0, DAY, 6));

  // A block leaves the default only on a significant score: one-sided losses for on in 6 (12-14h) turn it off.
  for (int i = 0; i < 7; i++) autoAddTest(a, 6, pair(true, true, 6, 3), T0 + 1000 + i * 700, DAY);
  CHECK(autoApplied(a, 6));   // 7 decided: not yet
  for (int i = 0; i < 6; i++) autoAddTest(a, 6, pair(true, true, 6, 3), T0 + 6000 + i * 700, DAY);
  CHECK(!autoApplied(a, 6) && autoApplied(a, 5));
  autoStatusText(a, -6, text, sizeof(text));
  CHECK_STR(text, "auto (on, off 12-14)");
  autoBlockRow(a, row);
  CHECK(row[6] == '-' && row[5] == '.');

  // A mixed block stays at the default.
  for (int i = 0; i < 20; i++) autoAddTest(a, 9, pair(true, true, i % 2 ? 6 : 3, i % 2 ? 3 : 6), T0 + 20000 + i * 700, DAY);
  CHECK(autoApplied(a, 9));

  // Fading between pings: the outbound SNRs disagree, so the pair is discarded.
  uint16_t before = a.b[3].on_w + a.b[3].off_w;
  CHECK(!autoAddTest(a, 3, pair(true, true, 9, 1, 2, 9), T0 + 40000, DAY));
  CHECK(a.b[3].on_w + a.b[3].off_w == before);
  CHECK(!autoAddTest(a, 3, pair(false, false, 0, 0), T0 + 41000, DAY));

  // Revert fast: two losses in a row by the applied state return the block to the default for the day.
  for (int i = 0; i < 2; i++) autoAddTest(a, 6, pair(true, true, 3, 6), T0 + 50000 + i * 700, DAY);
  CHECK(a.b[6].reverted && autoApplied(a, 6) && a.b[6].restart);
  autoDayTurn(a, DAY + 1, 4);
  CHECK(!a.b[6].reverted && !autoApplied(a, 6));
  // A neighbour heard only with the LNA on vetoes off at once, and for two weeks.
  autoAddTest(a, 6, pair(false, true, 0, 4), T0 + 90000, DAY + 1);
  CHECK(a.b[6].veto_off == 14 && autoApplied(a, 6));

  // Auto against the fixed default: replies won or lost while it ran the other state.
  CHECK(a.gain_tests >= 1);

  // Freezes in a block while on count against on.
  uint16_t off_w = a.b[2].off_w;
  autoAddFreeze(a, 2);
  CHECK(a.b[2].off_w == off_w + 10);

  // Scores fade: 0.95 a day.
  autoInit(a, true, DAY);
  for (int i = 0; i < 20; i++) autoAddTest(a, 1, pair(true, true, 6, 3), T0 + i * 700, DAY);
  uint16_t w = a.b[1].off_w;
  autoDayTurn(a, DAY + 1, 4);
  CHECK(a.b[1].off_w == w * 95 / 100);

  // Retire: after the learning week, a block within 3% over 30+ tests stops testing.
  autoInit(a, false, DAY);
  for (int i = 0; i < 40; i++) autoAddTest(a, 4, pair(true, true, i % 2 ? 6 : 3, i % 2 ? 3 : 6), T0 + i * 700, DAY + 7);
  autoDayTurn(a, DAY + 7, 4);
  CHECK(a.b[4].retired && !autoTestDue(a, T0 + 7 * 86400 + 99999, DAY + 7, 4));

  // Park: dead LNA (floor barely moves), too few neighbours, nothing left to learn.
  autoInit(a, true, DAY);
  for (int i = 0; i < 6; i++) {
    AutoTest t = pair(true, true, 3, 3);
    t.floor_dbm[1] = -114;
    autoAddTest(a, 0, t, T0 + i * 700, DAY);
  }
  autoDayTurn(a, DAY + 1, 4);
  CHECK(a.park == AUTO_PARK_DEAD && autoApplied(a, 0) && !autoTestDue(a, T0 + 99999, DAY + 1, 0));
  autoStatusText(a, 0, text, sizeof(text));
  CHECK_STR(text, "auto (parked: LNA not working)");
  // A parked auto rechecks after 90 days by learning again.
  autoDayTurn(a, DAY + 1 + 90, 4);
  CHECK(a.park == AUTO_ACTIVE && a.start_day == DAY + 91 && a.enabled);
  autoInit(a, true, DAY);
  for (int d = 1; d <= 14; d++) autoDayTurn(a, DAY + d, 1);
  CHECK(a.park == AUTO_PARK_FEW_NB);
  autoStatusText(a, 0, text, sizeof(text));
  CHECK_STR(text, "auto (parked: too few neighbours)");
  autoInit(a, true, DAY);
  for (int b = 0; b < AUTO_BLOCKS; b++) a.b[b].retired = 1;
  autoDayTurn(a, DAY + 8, 4);
  CHECK(a.park == AUTO_PARK_NO_BENEFIT);
  autoStatusText(a, 0, text, sizeof(text));
  CHECK_STR(text, "auto (parked: no benefit, on)");

  // Disabled or default-off: the default rules, and on wins only significantly.
  autoInit(a, false, DAY);
  for (int i = 0; i < 14; i++) autoAddTest(a, 10, pair(false, true, 0, 5), T0 + i * 700, DAY);
  CHECK(autoApplied(a, 10) && !autoApplied(a, 0));
  autoStatusText(a, 0, text, sizeof(text));
  CHECK_STR(text, "auto (off, on 20-22)");
  a.enabled = 0;
  CHECK(!autoApplied(a, 10) && !autoTestDue(a, T0 + 99999, DAY, 10));

  // A test over a busy channel is skipped and counted.
  autoInit(a, false, DAY);
  CHECK(autoChannelOk(a, 25) && !autoChannelOk(a, 26) && a.busy == 1);

  // Noise that changed between the halves: once the usual shift is known, a pair far off it is thrown out.
  for (int i = 0; i < 5; i++) autoAddTest(a, 3, pair(true, true, 3, 3), T0 + i * 700, DAY);
  CHECK(a.floor_n == 5 && a.noisy == 0);
  AutoTest jumped = pair(false, true, 0, 5);
  jumped.floor_dbm[1] = -80;          // 37 dB against the usual 22: noise arrived mid-test
  CHECK(!autoAddTest(a, 3, jumped, T0 + 9000, DAY) && a.noisy == 1 && a.b[3].on_w == 0);

  // The audit: block 10 learned on, but its on hours decode worse than its off hours, so on is banned there.
  autoInit(a, false, DAY);
  for (int i = 0; i < 14; i++) autoAddTest(a, 10, pair(false, true, 0, 5), T0 + i * 700, DAY);
  CHECK(autoApplied(a, 10));
  for (int h = 0; h < 6; h++) autoAddHour(a, 10, false, 100, 2, 0, 3, -117);
  for (int h = 0; h < 5; h++) autoAddHour(a, 10, true, 100, 12, 0, 3, -95);
  CHECK(autoAudit(a, 10) == AUTO_R_NONE);   // 5 hours on: not enough yet
  autoAddHour(a, 10, true, 100, 12, 0, 3, -95);
  CHECK(autoAudit(a, 10) == AUTO_R_DECODES && !autoApplied(a, 10));
  CHECK(autoBanText(a, text, sizeof(text)));
  CHECK_STR(text, "on 20-22 banned 7d: decodes worse");
  autoStatusText(a, 0, text, sizeof(text));
  CHECK_STR(text, "auto (off)");
  // The ban runs out after 7 day turns; on must then re-earn the block from new tests.
  for (int d = 1; d <= 7; d++) autoDayTurn(a, DAY + d, 4);
  CHECK(a.b[10].ban_days == 0 && !autoApplied(a, 10));

  // Each other observation: fewer packets, neighbours lost, freezes, and overload against the usual shift.
  struct { uint32_t recv, errors; uint8_t freezes, heard; int8_t floor; uint8_t why; } cases[] = {
    {60, 1, 0, 3, -95, AUTO_R_PACKETS},
    {100, 2, 0, 1, -95, AUTO_R_NEIGHBOURS},
    {100, 2, 1, 3, -95, AUTO_R_FREEZES},
    {100, 2, 0, 3, -85, AUTO_R_OVERLOAD},
    {100, 2, 0, 3, -95, AUTO_R_NONE},
  };
  for (auto& k : cases) {
    autoInit(a, false, DAY);
    for (int i = 0; i < 14; i++) autoAddTest(a, 10, pair(false, true, 0, 5), T0 + i * 700, DAY);
    for (int h = 0; h < 6; h++) autoAddHour(a, 10, false, 100, 2, 0, 3, -117);
    for (int h = 0; h < 6; h++) autoAddHour(a, 10, true, k.recv, k.errors, k.freezes, k.heard, k.floor);
    CHECK(autoAudit(a, 10) == k.why);
  }

  // A default-on block the engine finds overloaded runs off while the ban lasts, then on again.
  autoInit(a, true, DAY);
  autoBan(a, 1UL << 7, true, AUTO_R_OVERLOAD);   // 7am: block 3
  CHECK(!autoApplied(a, 3) && autoApplied(a, 2));
  CHECK(autoBanText(a, text, sizeof(text)));
  CHECK_STR(text, "on 06-08 banned 7d: overload");

  // A block away from its default runs the default one day in 14, so both records stay current.
  autoInit(a, false, DAY);
  for (int i = 0; i < 14; i++) autoAddTest(a, 10, pair(false, true, 0, 5), T0 + i * 700, DAY);
  int refreshed = 0;
  for (int d = 1; d <= 14; d++) {
    for (int i = 0; i < 2; i++) autoAddTest(a, 10, pair(false, true, 0, 5), T0 + d * 86400UL + i * 700, DAY + d);
    autoDayTurn(a, DAY + d, 4);
    refreshed += !autoApplied(a, 10);
  }
  CHECK(refreshed == 1);
  DONE();
}
