#pragma once

#include <helpers/diag_reports/DiagStore.h>

// What the stored history says beyond today: this site's usual, slow change, measured shares, schedules and forecasts. Pure.

#define TREND_ABOVE_DB     4            // an hour this far above its usual (or twice its usual wobble) is unusual
#define TREND_FADE_DB      4            // a neighbour this far below its own recent level is degrading
#define TREND_MIN_DAYS     3            // baselines and schedules need this many days

// Hours today that sit above this site's usual for that hour.
struct TrendUsual {
  bool ready;                          // enough days learned for at least the hours compared
  uint32_t above;                      // local hours above their usual
  int8_t worst_db;                     // largest excess
  uint8_t worst_hour;                  // local hour of it
  int8_t now_db;                       // the newest finished hour against its usual; 0 = unknown
};

// One neighbour's slow change over the stored days.
struct TrendNb {
  bool found;
  uint16_t id;
  uint8_t id_len;
  int8_t drop_db;                      // last 3 days against the 7 before
  uint32_t since_day;                  // local day the drop began
  float slope_db_day;                  // least-squares slope over the stored days
};

// How much of the failed decodes fell in each kind of hour.
struct TrendShares {
  uint32_t errors;                     // failed decodes in the window
  uint8_t noise, freeze, busy;         // percent of them in loud, freezing and busy hours
  uint8_t noise_hours, freeze_hours, busy_hours;   // percent of watched hours of each kind
};

// A loud schedule seen across days of the week.
struct TrendSchedule {
  bool found;
  bool weekdays_only;                  // loud on weekdays, quiet at weekends
  uint32_t hours;                      // local hours
  uint8_t days_seen;
};

TrendUsual trendUsual(const DiagStore& s, uint32_t now_hour, int8_t tz_hours);
// The neighbour with the largest recent drop, or found = false; `day` is today.
TrendNb trendWorstNb(const DiagStore& s, uint32_t day);
// Days until a link at `margin_db` reaches its limit at the slope; -1 when not falling.
int trendDaysToLimit(float margin_db, float slope_db_day);
TrendShares trendShares(const DiagStore& s, uint32_t now_hour, int8_t tz_hours, uint32_t loud, uint32_t freeze, uint32_t busy);
TrendSchedule trendSchedule(const DiagStore& s, uint32_t today);
// Local day number -> 0 Monday .. 6 Sunday.
uint8_t trendWeekday(uint32_t local_day);
// "floor usual, links steady, batt +4 mV/day": the line a green report ends with.
void trendHealth(const DiagStore& s, uint32_t now_hour, int8_t tz_hours, char* out, size_t len);
