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
  char text[80], row[AUTO_BLOCKS + 1];

  // Blocks follow local even hours.
  CHECK(autoBlockOf(0) == 0 && autoBlockOf(13) == 6 && autoBlockOf(23) == 11);

  // Learning starts undecided: odd local hours on, even off; tests every 10 minutes, under a daily cap.
  autoInit(a, DAY);
  CHECK(!autoApplied(a, 6, 12) && autoApplied(a, 6, 13) && autoLearning(a, DAY, 6));
  autoStatusText(a, 13, -1, text, sizeof(text));
  CHECK_STR(text, "mode:auto now:on (learning); off all day");
  autoStatusText(a, 12, -1, text, sizeof(text));
  CHECK_STR(text, "mode:auto now:off (learning); off all day");
  CHECK(autoTestDue(a, T0 + 100, DAY, 6));
  autoAddTest(a, 6, pair(true, true, 3, 3), T0 + 100, DAY);
  CHECK(!autoTestDue(a, T0 + 400, DAY, 6) && autoTestDue(a, T0 + 700, DAY, 6));
  CHECK(!autoTestDue(a, 0, DAY, 6));

  // A block takes on only on a significant score: one-sided wins for on in 6 (12-14h) turn it on, in every hour.
  for (int i = 0; i < 7; i++) autoAddTest(a, 6, pair(true, true, 3, 6), T0 + 1000 + i * 700, DAY);
  CHECK(!autoApplied(a, 6, 12));   // 7 decided: still alternating
  for (int i = 0; i < 6; i++) autoAddTest(a, 6, pair(true, true, 3, 6), T0 + 6000 + i * 700, DAY);
  CHECK(autoApplied(a, 6, 12) && autoApplied(a, 6, 13) && !autoApplied(a, 5, 10));
  autoStatusText(a, -1, -1, text, sizeof(text));
  CHECK_STR(text, "mode:auto; on 12:00-14:00");
  autoStatusText(a, 12, -1, text, sizeof(text));
  CHECK_STR(text, "mode:auto now:on; on 12:00-14:00");   // a decided block takes its state from the hour
  // With the real LNA state known, the status always ends with it.
  autoStatusText(a, 12, 1, text, sizeof(text));
  CHECK_STR(text, "mode:auto now:on; on 12:00-14:00");
  autoStatusText(a, 10, 1, text, sizeof(text));
  CHECK_STR(text, "mode:auto now:on (learning); on 12:00-14:00");
  autoStatusText(a, -1, 0, text, sizeof(text));
  CHECK_STR(text, "mode:auto now:off (clock not set); on 12:00-14:00");
  // A run that crosses midnight is joined, and separate runs are listed in order after an off block.
  LnaAutoState wrap = a;
  for (int b = 0; b < AUTO_BLOCKS; b++) {
    wrap.b[b].best = 0;
    wrap.b[b].retired = 1;
  }
  const int on_blocks[] = {11, 0, 1};
  for (int b : on_blocks) {
    wrap.b[b].best = 1;
    wrap.b[b].retired = 0;
  }
  autoStatusText(wrap, 9, 0, text, sizeof(text));
  CHECK_STR(text, "mode:auto now:off; on 22:00-04:00");
  wrap.b[6].best = 1;
  wrap.b[6].retired = 0;
  autoStatusText(wrap, 9, 0, text, sizeof(text));
  CHECK_STR(text, "mode:auto now:off; on 12:00-14:00, 22:00-04:00");
  autoBlockRow(a, row);
  CHECK(row[6] == '+' && row[5] == '.');

  // A mixed block, decided but with no significant winner, settles off and stops alternating.
  for (int i = 0; i < 20; i++) autoAddTest(a, 9, pair(true, true, i % 2 ? 6 : 3, i % 2 ? 3 : 6), T0 + 20000 + i * 700, DAY);
  CHECK(!autoApplied(a, 9, 18) && !autoApplied(a, 9, 19));
  autoBlockRow(a, row);
  CHECK(row[9] == '-');

  // Fading between pings: the outbound SNRs disagree, so the pair is discarded.
  uint16_t before = a.b[3].on_w + a.b[3].off_w;
  CHECK(!autoAddTest(a, 3, pair(true, true, 9, 1, 2, 9), T0 + 40000, DAY));
  CHECK(a.b[3].on_w + a.b[3].off_w == before);
  CHECK(!autoAddTest(a, 3, pair(false, false, 0, 0), T0 + 41000, DAY));

  // Auto against fixed off: replies won or lost while it ran on.
  CHECK(a.gain_tests >= 1);

  // Two losses in a row while on send the block to off, scores cleared, learning again; it stays there across days.
  for (int i = 0; i < 2; i++) autoAddTest(a, 6, pair(true, true, 6, 3), T0 + 50000 + i * 700, DAY);
  CHECK(!a.b[6].best && a.b[6].on_w == 0 && a.b[6].off_w == 0 && a.b[6].restart && !autoApplied(a, 6, 12));
  autoDayTurn(a, DAY + 1, 4);
  CHECK(!autoApplied(a, 6, 12) && autoApplied(a, 6, 13));   // undecided again: alternating
  // A neighbour heard only with the LNA on turns the block on at once, and vetoes off for two weeks.
  autoAddTest(a, 6, pair(false, true, 0, 4), T0 + 90000, DAY + 1);
  CHECK(a.b[6].veto_off == 14 && autoApplied(a, 6, 12) && autoApplied(a, 6, 13));
  for (int i = 0; i < 2; i++) autoAddTest(a, 6, pair(true, true, 6, 3), T0 + 91000 + i * 700, DAY + 1);
  CHECK(autoApplied(a, 6, 12));   // losses do not override the veto

  // Freezes in a block while on count against on.
  uint16_t off_w = a.b[2].off_w;
  autoAddFreeze(a, 2);
  CHECK(a.b[2].off_w == off_w + 10);

  // Scores fade: 0.95 a day.
  autoInit(a, DAY);
  for (int i = 0; i < 20; i++) autoAddTest(a, 1, pair(true, true, 6, 3), T0 + i * 700, DAY);
  uint16_t w = a.b[1].off_w;
  autoDayTurn(a, DAY + 1, 4);
  CHECK(a.b[1].off_w == w * 95 / 100);

  // Retire: after the learning week, a block within 3% over 30+ tests stops testing.
  autoInit(a, DAY);
  for (int i = 0; i < 40; i++) autoAddTest(a, 4, pair(true, true, i % 2 ? 6 : 3, i % 2 ? 3 : 6), T0 + i * 700, DAY + 7);
  autoDayTurn(a, DAY + 7, 4);
  CHECK(a.b[4].retired && !autoTestDue(a, T0 + 7 * 86400 + 99999, DAY + 7, 4));

  // Park: dead LNA (floor barely moves), too few neighbours, nothing left to learn.
  autoInit(a, DAY);
  for (int i = 0; i < 6; i++) {
    AutoTest t = pair(true, true, 3, 3);
    t.floor_dbm[1] = -114;
    autoAddTest(a, 0, t, T0 + i * 700, DAY);
  }
  autoDayTurn(a, DAY + 1, 4);
  CHECK(a.park == AUTO_PARK_DEAD && !autoApplied(a, 0, 1) && !autoTestDue(a, T0 + 99999, DAY + 1, 0));
  autoStatusText(a, -1, -1, text, sizeof(text));
  CHECK_STR(text, "mode:auto parked (LNA not working)");
  // A parked auto rechecks after 90 days by learning again.
  autoDayTurn(a, DAY + 1 + 90, 4);
  CHECK(a.park == AUTO_ACTIVE && a.start_day == DAY + 91 && a.enabled);
  autoInit(a, DAY);
  for (int d = 1; d <= 14; d++) autoDayTurn(a, DAY + d, 1);
  CHECK(a.park == AUTO_PARK_FEW_NB);
  autoStatusText(a, -1, -1, text, sizeof(text));
  CHECK_STR(text, "mode:auto parked (too few neighbours)");
  autoInit(a, DAY);
  for (int b = 0; b < AUTO_BLOCKS; b++) a.b[b].retired = 1;
  autoDayTurn(a, DAY + 8, 4);
  CHECK(a.park == AUTO_PARK_NO_BENEFIT);
  autoStatusText(a, -1, -1, text, sizeof(text));
  CHECK_STR(text, "mode:auto now:off; parked (no benefit)");
  CHECK(!autoApplied(a, 4, 9));

  // Disabled: off, and no tests.
  autoInit(a, DAY);
  for (int i = 0; i < 14; i++) autoAddTest(a, 10, pair(true, true, 3, 6), T0 + i * 700, DAY);
  CHECK(autoApplied(a, 10) && !autoApplied(a, 0, 0));
  autoStatusText(a, -1, -1, text, sizeof(text));
  CHECK_STR(text, "mode:auto; on 20:00-22:00");
  a.enabled = 0;
  CHECK(!autoApplied(a, 10, 21) && !autoTestDue(a, T0 + 99999, DAY, 10));

  // A test over a busy channel is skipped and counted.
  autoInit(a, DAY);
  CHECK(autoChannelOk(a, 25) && !autoChannelOk(a, 26) && a.busy == 1);

  // Noise that changed between the halves: once the usual shift is known, a pair far off it is thrown out.
  for (int i = 0; i < 5; i++) autoAddTest(a, 3, pair(true, true, 3, 3), T0 + i * 700, DAY);
  CHECK(a.floor_n == 5 && a.noisy == 0);
  AutoTest jumped = pair(false, true, 0, 5);
  jumped.floor_dbm[1] = -80;          // 37 dB against the usual 22: noise arrived mid-test
  CHECK(!autoAddTest(a, 3, jumped, T0 + 9000, DAY) && a.noisy == 1 && a.b[3].on_w == 0);

  // The audit: block 10 learned on, but its on hours decode worse than its off hours, so on is banned there.
  autoInit(a, DAY);
  for (int i = 0; i < 14; i++) autoAddTest(a, 10, pair(true, true, 3, 6), T0 + i * 700, DAY);
  CHECK(autoApplied(a, 10));
  for (int h = 0; h < 6; h++) autoAddHour(a, 10, false, 100, 2, 0, 3, -117);
  for (int h = 0; h < 5; h++) autoAddHour(a, 10, true, 100, 12, 0, 3, -95);
  CHECK(autoAudit(a, 10) == AUTO_R_NONE);   // 5 hours on: not enough yet
  autoAddHour(a, 10, true, 100, 12, 0, 3, -95);
  CHECK(autoAudit(a, 10) == AUTO_R_DECODES && !autoApplied(a, 10));
  CHECK(autoBanText(a, text, sizeof(text)));
  CHECK_STR(text, "on 20-22 banned 7d: decodes worse");
  autoStatusText(a, -1, -1, text, sizeof(text));
  CHECK_STR(text, "mode:auto; off all day");
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
    autoInit(a, DAY);
    for (int i = 0; i < 14; i++) autoAddTest(a, 10, pair(true, true, 3, 6), T0 + i * 700, DAY);
    for (int h = 0; h < 6; h++) autoAddHour(a, 10, false, 100, 2, 0, 3, -117);
    for (int h = 0; h < 6; h++) autoAddHour(a, 10, true, k.recv, k.errors, k.freezes, k.heard, k.floor);
    CHECK(autoAudit(a, 10) == k.why);
  }

  // An undecided block the engine finds overloaded runs off in every hour while the ban lasts.
  autoInit(a, DAY);
  autoBan(a, 1UL << 7, true, AUTO_R_OVERLOAD);   // 7am: block 3
  CHECK(!autoApplied(a, 3, 7) && autoApplied(a, 2, 5));
  // The audit judges an undecided block's on hours against its off hours.
  for (int h = 0; h < 6; h++) autoAddHour(a, 8, false, 100, 2, 0, 3, -117);
  for (int h = 0; h < 6; h++) autoAddHour(a, 8, true, 100, 12, 0, 3, -95);
  CHECK(autoAudit(a, 8) == AUTO_R_DECODES && !autoApplied(a, 8, 17));
  autoInit(a, DAY);
  autoBan(a, 1UL << 7, true, AUTO_R_OVERLOAD);
  CHECK(autoBanText(a, text, sizeof(text)));
  CHECK_STR(text, "on 06-08 banned 7d: overload");

  // A block running on runs off one day in 14, so both records stay current.
  autoInit(a, DAY);
  for (int i = 0; i < 14; i++) autoAddTest(a, 10, pair(true, true, 3, 6), T0 + i * 700, DAY);
  int refreshed = 0;
  for (int d = 1; d <= 14; d++) {
    for (int i = 0; i < 2; i++) autoAddTest(a, 10, pair(true, true, 3, 6), T0 + d * 86400UL + i * 700, DAY + d);
    autoDayTurn(a, DAY + d, 4);
    refreshed += !autoApplied(a, 10);
  }
  CHECK(refreshed == 1);
  DONE();
}
