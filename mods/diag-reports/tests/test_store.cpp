#include <helpers/diag_reports/DiagStore.h>

#include <math.h>

#include "check.h"

#define NOW 500016U                    // a multiple of 24: local midnight at tz 0

int main() {
  static DiagStore s;
  storeInit(s);
  CHECK(sizeof(StoreHour) == 28 && sizeof(DiagStore) <= 2100);
  CHECK(storeHourAt(s, NOW) == nullptr && storeDay(s, 100).morning == STORE_NO_MV);

  // Noise and battery share an hour slot whichever arrives first.
  storeSample(s, NOW, 0, 3950, 21.4f);
  storeNoiseHour(s, NOW, -112, STORE_LNA_ON, true, 70000, 3);
  const StoreHour* h = storeHourAt(s, NOW);
  CHECK(h && h->floor_dbm == -112 && h->recv == 0xFFFF && h->errors == 3 && (h->flags & HOUR_COUNTS));
  CHECK(h->batt_first == 3950 && h->batt_last == 3950 && (h->flags & HOUR_BATT));
  storeSample(s, NOW, 0, 3940, 20.6f);
  storeSample(s, NOW, 0, 3960, 22.0f);
  CHECK(h->batt_lo == 3940 && h->batt_hi == 3960 && h->batt_first == 3950 && h->batt_last == 3960);

  // 25 slots keep 24 finished hours beside the one in progress.
  storeInit(s);
  for (uint32_t k = 0; k <= 24; k++) storeNoiseHour(s, NOW - 24 + k, -110, STORE_LNA_ON, false, 0, 0);
  CHECK(storeHourAt(s, NOW - 24) && storeHourAt(s, NOW));
  storeNoiseHour(s, NOW + 1, -110, STORE_LNA_ON, false, 0, 0);
  CHECK(storeHourAt(s, NOW - 24) == nullptr && storeHourAt(s, NOW - 23));
  CHECK(!(storeHourAt(s, NOW)->flags & HOUR_COUNTS));

  // Morning low is the lowest reading before local noon; temperature range covers the day.
  storeInit(s);
  storeSample(s, NOW + 6, 0, 3900, 5);
  storeSample(s, NOW + 8, 0, 3880, 9);
  storeSample(s, NOW + 15, 0, 3700, 31);
  CHECK(s.today.day == storeLocalDay(NOW, 0) && s.today.morning_mv == 3880 && s.today.tmin == 5 && s.today.tmax == 31);
  StoreDay d = storeDay(s, s.today.day);
  CHECK(storeUnpackMv(d.morning) == 3884 && d.tmin == 5 && d.tmax == 31);

  // The day turns at local midnight and the finished day is filed once.
  storeSample(s, NOW + 24, 0, 3890, 4);
  CHECK(s.last_day == storeLocalDay(NOW, 0) && s.today.day == s.last_day + 1 && s.today.morning_mv == 3890);
  CHECK(storeDay(s, s.last_day).tmax == 31);
  // The time zone moves where the day turns.
  static DiagStore z;
  storeInit(z);
  storeSample(z, NOW + 3, -5, 3900, 5);
  storeSample(z, NOW + 4, -5, 3900, 5);
  CHECK(z.last_day == 0 && storeLocalHour(NOW + 4, -5) == 23);
  storeSample(z, NOW + 5, -5, 3900, 5);
  CHECK(z.last_day == storeLocalDay(NOW + 4, -5) && storeLocalHour(NOW + 5, -5) == 0);

  // Skipped days are filed as unseen; a gap longer than the ring clears all of it.
  storeSample(s, NOW + 24 * 4 + 6, 0, 3800, 10);
  uint32_t last = s.last_day;
  CHECK(last == storeLocalDay(NOW + 24, 0));
  storeSample(s, NOW + 24 * 5 + 6, 0, 3790, 10);
  CHECK(storeDay(s, last + 1).morning == STORE_NO_MV && storeDay(s, last + 2).morning == STORE_NO_MV);
  CHECK(storeUnpackMv(storeDay(s, last + 3).morning) == 3804);
  storeSample(s, NOW + 24 * 60 + 6, 0, 3700, 10);
  storeSample(s, NOW + 24 * 61 + 6, 0, 3700, 10);
  int seen = 0;
  for (uint32_t day = s.last_day - 29; day <= s.last_day; day++) seen += storeDay(s, day).morning != STORE_NO_MV;
  CHECK(seen == 1);

  // A clock that moves back restarts today and files nothing.
  uint32_t filed = s.last_day;
  storeSample(s, NOW + 24 * 50 + 6, 0, 3650, 10);
  CHECK(s.last_day == filed && s.today.day == storeLocalDay(NOW + 24 * 50, 0) && s.today.morning_mv == 3650);

  // No battery: temperature still recorded, no battery slot or day tracking.
  storeInit(s);
  storeSample(s, NOW + 6, 0, 0, 12);
  CHECK(s.today.tmin == 12 && s.today.morning_mv == 0 && s.batt.day == 0);
  CHECK(storeHourAt(s, NOW + 6) && storeHourAt(s, NOW + 6)->temp_max == 12 && !(storeHourAt(s, NOW + 6)->flags & HOUR_BATT));
  // No sensor: nothing recorded for temperature.
  storeSample(s, NOW + 7, 0, 3900, NAN);
  CHECK(s.today.tmin == 12 && s.today.morning_mv == 3900);

  // Whole days: consecutive, with at least 20 hours since the day's first reading.
  storeInit(s);
  for (uint32_t k = 0; k < 73; k++) storeSample(s, NOW + k, 0, (uint16_t)(4000 - k), 20);
  CHECK(s.batt.full_days == 2 && s.batt.day_mv == 4000 - 72 && s.batt.yesterday_mv == 4000 - 48 && s.batt.before_mv == 4000 - 24);
  storeSample(s, NOW + 72 + 48, 0, 3900, 20);
  CHECK(s.batt.full_days == 0);

  // Freezes and guard-off land in the hour, the day and the totals.
  storeInit(s);
  storeFreeze(s, (NOW + 9) * 3600 + 30, 0);
  storeFreeze(s, (NOW + 9) * 3600 + 90, 0);
  storeGuardOff(s, (NOW + 9) * 3600 + 95, 0x04, 0x05);
  h = storeHourAt(s, NOW + 9);
  CHECK(h && h->freezes == 2 && h->guard_off == 1 && h->floor_dbm == STORE_NO_FLOOR);
  CHECK(s.guard.freezes == 2 && s.guard.off == 1 && s.guard.off_before == 0x04 && s.guard.off_after == 0x05);
  CHECK(s.today.freezes == 2 && s.guard.last_freeze == (NOW + 9) * 3600 + 90);
  storeFreeze(s, 100, 0);
  CHECK(s.guard.freezes == 3 && s.today.freezes == 2);
  // A strong signal at a reset is its own count, never a freeze.
  storeStrong(s, (NOW + 9) * 3600 + 120);
  CHECK(s.guard.strong == 1 && storeHourAt(s, NOW + 9)->strong == 1 && storeHourAt(s, NOW + 9)->freezes == 2 && s.guard.freezes == 3);

  // Hourly load lands in the hour and in the day's mean; the day record carries it.
  storeInit(s);
  storeLoadHour(s, NOW + 1, 0, 10);
  storeLoadHour(s, NOW + 2, 0, 30);
  storeLoadHour(s, NOW + 3, 0, 200);
  CHECK(storeHourAt(s, NOW + 2)->load_pct == 30 && storeHourAt(s, NOW + 3)->load_pct == 100);
  CHECK(storeDay(s, s.today.day).load_pct == 46);
  storeSample(s, NOW + 30, 0, 3900, 20);
  CHECK(storeDay(s, s.last_day).load_pct == 46 && storeDay(s, s.today.day).load_pct == STORE_NO_LOAD);

  // Recurrence: the same cause in overlapping hours counts; other causes and skipped days do not.
  storeInit(s);
  int recorded = 0;
  CHECK(storeRecurCount(s, 100, 3, 0xFF00, &recorded) == 0 && recorded == 0);
  storeRecurSet(s, 100, 3, 0x1FF00);
  storeRecurSet(s, 101, 3, 0x0FF00);
  storeRecurSet(s, 102, 5, 0x0FF00);
  storeRecurSet(s, 104, 3, 0x00F00);
  CHECK(storeRecurCount(s, 104, 3, 0x0FF00, &recorded) == 3 && recorded == 5);
  CHECK(storeRecurCount(s, 104, 3, 0x00003, &recorded) == 0);
  CHECK(storeRecurCount(s, 104, 5, 0, &recorded) == 1);
  storeRecurSet(s, 120, 3, 0x00F00);
  CHECK(storeRecurCount(s, 120, 3, 0x0FF00, &recorded) == 1 && recorded == 7);
  storeRecurSet(s, 119, 3, 0x00F00);
  CHECK(s.recur_day == 120);

  // Reboot records: what the last save left behind, newest first, planned flag consumed.
  storeInit(s);
  storeRebootSeen(s, 1000);
  CHECK(s.reboot[0].at == 0);
  storeSample(s, NOW + 5, 0, 3900, 66);
  s.last_seen = (NOW + 5) * 3600;
  storeRebootSeen(s, (NOW + 6) * 3600);
  CHECK(s.reboot[0].at == (NOW + 6) * 3600 && s.reboot[0].temp_before == 66 && s.reboot[0].batt_before == 3900 && !s.reboot[0].planned);
  s.reboot_planned = 1;
  storeRebootSeen(s, (NOW + 9) * 3600);
  CHECK(s.reboot[0].planned && s.reboot[1].temp_before == 66 && s.reboot_planned == 0);

  // The histogram median equals a sorted array's element at count / 2.
  DbmHistogram g;
  histoReset(g);
  CHECK(histoMedian(g) == STORE_NO_FLOOR);
  int8_t values[] = {-112, -110, -111, -90, -113, -112};
  for (int8_t v : values) histoAdd(g, v);
  CHECK(histoMedian(g) == -111);
  histoAdd(g, -120);
  CHECK(histoMedian(g) == -112 && g.count == 7);
  histoAdd(g, 5);
  histoAdd(g, -128);
  CHECK(g.count == 9);

  CHECK(storePackMv(2400) == 0 && storeUnpackMv(storePackMv(3350)) == 3348 && storePackMv(9000) == 254);
  // direct-retry's daily record: days add up, a reused slot drops the old day, sums run back from today.
  storeInit(s);
  storeRetryAdd(s, 100, 4, 3);
  storeRetryAdd(s, 100, 2, 2);
  storeRetryAdd(s, 99, 10, 2);
  storeRetryAdd(s, 93, 50, 50);   // older day in 100's slot: a clock set back, ignored
  uint32_t rr = 0, ww = 0;
  storeRetrySum(s, 100, 0, 0, &rr, &ww);
  CHECK(rr == 6 && ww == 5);
  storeRetrySum(s, 100, 0, 6, &rr, &ww);
  CHECK(rr == 16 && ww == 7);
  storeRetryAdd(s, 107, 1, 1);
  storeRetrySum(s, 107, 0, 6, &rr, &ww);
  CHECK(rr == 1 && ww == 1);
  DONE();
}
