#pragma once

#include <helpers/diag_reports/DiagOut.h>
#include <helpers/diag_reports/DiagStore.h>

// Battery over 24 hours and 30 days, and CPU temperature over 30 days, all from the store. Pure.

#define BATT_LOW_MV      3350          // the low warning, on a board with a measured divider
#define HIST_MIN_DAYS    7             // a week of mornings before a verdict
#define TEMP_COLD_C      0             // most lithium chargers stop below freezing, and the pack is colder than the CPU
#define TEMP_HOT_C       65            // a CPU this hot means the enclosure is baking
#define TEMP_RECENT_DAYS 7

enum BattKind : uint8_t {
  BATT_K_WATCHING,                     // under a full cycle of data
  BATT_K_STEADY,
  BATT_K_CHARGING,
  BATT_K_DRAINING,                     // falling with no charging seen
  BATT_K_NET_LOSS,                     // charged but ended lower, without two whole days to call it a pattern
  BATT_K_NOT_RECOVERING,               // charged, but fell across two whole days as well
  BATT_K_LOW,
};

struct BattView {
  uint8_t hours;                       // readable hours in the last 24
  uint8_t span;                        // hours from the oldest reading to now
  uint8_t kind;                        // BattKind
  uint8_t level;
  bool calibrated;
  uint16_t now_mv, lo_mv, hi_mv;
  uint8_t lo_hour, hi_hour;            // local hours
  int16_t net_mv;                      // now against the oldest reading
  int16_t hour_mv;                     // change over the newest finished hour and the one in progress
  uint8_t charge_hours;                // hours that rose
  int16_t night_drain;                 // mV per hour, local 10pm to 5am; -1 when unknown
  uint8_t chart_start;                 // local hour of the oldest column
  uint16_t chart[24];                  // mV, oldest first, ending now; 0 = no data
};

enum HistKind : uint8_t {
  HIST_K_WATCHING,
  HIST_K_HOLDING,
  HIST_K_LOSING,
  HIST_K_LOW,
};

struct HistView {
  uint8_t kind;                        // HistKind
  uint8_t level;
  uint8_t days;                        // days with a morning reading
  uint8_t missing;                     // days in the window without one
  uint8_t held, pairs;                 // day-to-day pairs that held within 30 mV, of all pairs
  uint16_t low_mv, last_mv;
  uint32_t low_day;
  int16_t net_mv;                      // newest morning against the oldest
  uint8_t net_days;
  int16_t days_to_low;                 // days until the low mark at the month's rate; -1 when not falling or not measured
  uint32_t first_day;                  // local day of the oldest column
  uint16_t chart[STORE_DAYS];          // morning mV, oldest first, ending today; 0 = none
};

enum TempKind : uint8_t {
  TEMP_K_NONE,                         // no sensor and no records
  TEMP_K_WATCHING,                     // a reading now, but no day recorded yet
  TEMP_K_NORMAL,
  TEMP_K_COLD,
  TEMP_K_HOT,
};

struct TempView {
  uint8_t kind;                        // TempKind
  uint8_t level;
  bool has_now;
  int8_t now_c;
  uint8_t days;                        // days with a reading in the window
  int8_t tmin, tmax;
  uint32_t cold_day, warm_day;
  uint8_t cold_days, hot_days;         // in the 30 days
  uint32_t first_day;                  // local day of the oldest column
  int8_t lows[STORE_DAYS], highs[STORE_DAYS];   // daily, oldest first, ending today; STORE_NO_TEMP = none
};

BattView battAnalyse(const DiagStore& s, uint32_t now_hour, int8_t tz_hours, bool calibrated);
void battFormat(const BattView& v, const DiagNote* note, DiagOut& o);
int battSummary(const BattView& v, char* out, size_t len);

HistView histAnalyse(const DiagStore& s, bool calibrated);
void histFormat(const HistView& v, const DiagNote* note, DiagOut& o);

// `with_today` is false while the clock is unset: today's running values then belong to no day.
TempView tempAnalyse(const DiagStore& s, bool with_today, float now_c);
void tempFormat(const TempView& v, const DiagNote* note, DiagOut& o);
