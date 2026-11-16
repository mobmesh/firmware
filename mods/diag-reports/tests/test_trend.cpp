#include <helpers/diag_reports/DiagTrend.h>

#include "check.h"

#define NOW 500016U                    // local midnight at tz 0, a multiple of 24
#define DAY (NOW / 24 + 1)             // local day number of NOW at tz 0

int main() {
  static DiagStore s;
  char text[80];

  // U1: a week of quiet 2pm hours learns -112; today's 2pm at -104 is 8 dB above its usual.
  storeInit(s);
  for (int d = 0; d < 7; d++) storeBaseHour(s, 14, (int8_t)(-112 + (d % 2)), true, 100, 1, 6);
  CHECK(s.base[14].n == 7 && s.base[14].floor_dbm >= -112 && s.base[14].floor_dbm <= -111 && s.base[14].spread <= 1);
  storeNoiseHour(s, NOW - 24 + 14, -104, STORE_LNA_ON, true, 100, 1);
  storeNoiseHour(s, NOW - 1, -111, STORE_LNA_ON, true, 100, 1);
  TrendUsual u = trendUsual(s, NOW, 0);
  CHECK(u.ready && (u.above & (1UL << 14)) && u.worst_hour == 14 && u.worst_db >= 7);
  // A site whose 2pm always wobbles 5 dB needs 10 dB to count as unusual.
  storeInit(s);
  for (int d = 0; d < 7; d++) storeBaseHour(s, 14, (int8_t)(d % 2 ? -107 : -117), true, 100, 1, 6);
  storeNoiseHour(s, NOW - 24 + 14, -104, STORE_LNA_ON, true, 100, 1);
  u = trendUsual(s, NOW, 0);
  CHECK(u.ready && !(u.above & (1UL << 14)));
  // Too few days: not ready.
  storeInit(s);
  storeBaseHour(s, 14, -112, true, 100, 1, 6);
  storeNoiseHour(s, NOW - 24 + 14, -100, STORE_LNA_ON, true, 100, 1);
  CHECK(!trendUsual(s, NOW, 0).ready);

  // U2: C5FB at 4 dB for a week, then 0, -1, -2 over the last three days: down 5 dB, falling.
  storeInit(s);
  for (int back = 9; back >= 0; back--) {
    int8_t v = back >= 3 ? 4 : (int8_t)(-(2 - back));
    storeNbDay(s, DAY - back, 0xC5FB, 2, v);
    storeNbDay(s, DAY - back, 0x4EA1, 2, 9);
  }
  TrendNb nb = trendWorstNb(s, DAY);
  CHECK(nb.found && nb.id == 0xC5FB && nb.drop_db == 5 && nb.since_day == DAY - 2 && nb.slope_db_day < -0.3f);
  CHECK(trendDaysToLimit(6, nb.slope_db_day) > 0 && trendDaysToLimit(6, 0.5f) == -1);
  // Steady neighbours are not flagged; a missed day is not a drop.
  storeInit(s);
  for (int back = 9; back >= 0; back--) if (back != 1) storeNbDay(s, DAY - back, 0x4EA1, 2, 9);
  CHECK(!trendWorstNb(s, DAY).found);
  // The table reuses the stalest slot when full.
  storeInit(s);
  for (int i = 0; i < STORE_NB_MAX; i++) storeNbDay(s, DAY - 3, (uint16_t)(0x1000 + i), 2, 5);
  storeNbDay(s, DAY, 0x9999, 2, 7);
  int found = 0;
  for (int i = 0; i < STORE_NB_MAX; i++) found += s.nb[i].id == 0x9999;
  CHECK(found == 1);

  // U3: of 100 failed decodes, 80 in the loud hours: noise explains 80%.
  storeInit(s);
  for (uint32_t k = 0; k < 24; k++) storeNoiseHour(s, NOW - 24 + k, -112, STORE_LNA_ON, true, 100, (k >= 8 && k < 12) ? 20 : 1);
  TrendShares sh = trendShares(s, NOW, 0, 0xF00, 0, 0);
  CHECK(sh.errors == 100 && sh.noise == 80 && sh.noise_hours == 16);

  // U4: loud 8-12 every weekday, quiet at weekends, for two weeks: a weekday schedule.
  storeInit(s);
  for (uint32_t back = 14; back >= 1; back--) {
    uint32_t day = DAY - back;
    storeLoudDay(s, day, trendWeekday(day) >= 5 ? 0 : 0xF00);
  }
  TrendSchedule sc = trendSchedule(s, DAY);
  CHECK(sc.found && sc.weekdays_only && sc.hours == 0xF00);
  // Every day: a daily schedule.
  storeInit(s);
  for (uint32_t back = 14; back >= 1; back--) storeLoudDay(s, DAY - back, 0x30000);
  sc = trendSchedule(s, DAY);
  CHECK(sc.found && !sc.weekdays_only && sc.hours == 0x30000);
  // Scattered: none.
  storeInit(s);
  for (uint32_t back = 14; back >= 1; back--) storeLoudDay(s, DAY - back, 1UL << (back % 24));
  CHECK(!trendSchedule(s, DAY).found);
  CHECK(trendWeekday(1) == 3 && trendWeekday(20773) == 6 && trendWeekday(20774) == 0);

  // U5: a green report still says how things are moving.
  storeInit(s);
  for (int d = 0; d < 7; d++) for (uint8_t h = 0; h < 24; h++) storeBaseHour(s, h, -112, true, 100, 1, 6);
  for (uint32_t k = 0; k < 24; k++) storeNoiseHour(s, NOW - 24 + k, -112, STORE_LNA_ON, true, 100, 1);
  for (int back = 9; back >= 0; back--) storeNbDay(s, DAY - 1 - back, 0x4EA1, 2, 9);
  for (int back = 7; back >= 0; back--) storeSample(s, NOW - 24U * back + 6, 0, (uint16_t)(3950 + 4 * (7 - back)), 20);
  trendHealth(s, NOW, 0, text, sizeof(text));
  CHECK_STR(text, "floor usual, links steady, batt +4 mV/day");
  DONE();
}
