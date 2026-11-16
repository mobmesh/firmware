#include <helpers/diag_reports/DiagSched.h>

#include "check.h"

int main() {
  static DiagStore s;
  storeInit(s);
  SchedConditions c = {1794765954, 20772, false, false, false, 3900, 6, 0xFFFFFFFF};
  const char* why = nullptr;

  // Forced triggers far above the caps: never more than 3 scans or 24 bursts a day, one an hour.
  int scans = 0, bursts = 0;
  for (int minute = 0; minute < 24 * 60; minute++) {
    c.epoch = 1794700800 + minute * 60;
    if (schedAllow(s, TRIG_FREEZES, c, &why)) {
      schedUse(s, TRIG_FREEZES, c);
      scans++;
    }
    if (schedAllow(s, TRIG_LOUD, c, &why)) {
      schedUse(s, TRIG_LOUD, c);
      bursts++;
    }
  }
  CHECK(scans == 3 && bursts == 24);
  CHECK(schedUsed(s, TRIG_FREEZES, c.local_day) == 3 && schedUsed(s, TRIG_DECODES, c.local_day) == 0);
  CHECK(!schedAllow(s, TRIG_FREEZES, c, &why));
  CHECK_STR(why, "daily cap");
  // A new local day resets the counts.
  c.local_day++;
  c.epoch += 3 * 3600;
  CHECK(schedUsed(s, TRIG_FREEZES, c.local_day) == 0 && schedAllow(s, TRIG_FREEZES, c, &why));

  // Each guard stands a capture down and says which.
  struct { const char* why; SchedConditions c; } cases[] = {
    {"auto off", {c.epoch, c.local_day, true, false, false, 3900, 6, 0xFFFFFFFF}},
    {"auto paused", {c.epoch, c.local_day, false, true, false, 3900, 6, 0xFFFFFFFF}},
    {"clock not set", {0, c.local_day, false, false, false, 3900, 6, 0xFFFFFFFF}},
    {"packet arriving", {c.epoch, c.local_day, false, false, true, 3900, 6, 0xFFFFFFFF}},
    {"channel busy", {c.epoch, c.local_day, false, false, false, 3900, 31, 0xFFFFFFFF}},
    {"recent scan", {c.epoch, c.local_day, false, false, false, 3900, 6, 600}},
  };
  for (auto& k : cases) {
    CHECK(!schedAllow(s, TRIG_FREEZES, k.c, &why));
    CHECK_STR(why ? why : "", k.why);
  }
  // Captures only listen: a low battery does not hold them back; LNA auto tests stop under the threshold.
  SchedConditions low = c;
  low.batt_mv = 3400;
  CHECK(schedAllow(s, TRIG_LOUD, low, &why) && why == nullptr);
  CHECK(schedTestBatteryLow(SCHED_LOW_MV - 1) && !schedTestBatteryLow(SCHED_LOW_MV) && !schedTestBatteryLow(0));
  // A board with no battery reading is not held back for it.
  SchedConditions none = c;
  none.batt_mv = 0;
  CHECK(schedAllow(s, TRIG_LOUD, none, &why) && why == nullptr);
  // A recent scan only holds back the scan trigger.
  SchedConditions scanned = c;
  scanned.scan_age_s = 60;
  CHECK(schedAllow(s, TRIG_DECODES, scanned, &why));

  CHECK(schedDecodeSpike(10, 13) && !schedDecodeSpike(10, 12) && !schedDecodeSpike(13, 2));
  CHECK(schedPauseMinutes("") == 5 && schedPauseMinutes(" 30 ") == 30 && schedPauseMinutes("1440") == 1440);
  CHECK(schedPauseMinutes("0") == -1 && schedPauseMinutes("1441") == -1 && schedPauseMinutes("5m") == -1);
  DONE();
}
