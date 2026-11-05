#pragma once

#include <stddef.h>
#include <stdint.h>

// Pure battery-watch arithmetic and text: hourly readings in, report out. No ADC, clock or storage, so tests can drive it.

#define BATT_HOURS      24
#define BATT_LINE       112
#define BATT_FULL_HOURS 20             // a whole charge and drain cycle, so a night alone is not a verdict
#define BATT_LOW_MV     3350           // the low warning, on a board with a measured divider

struct BattHour {
  uint32_t hour;                       // UTC epoch hour the slot holds; 0 = empty
  uint16_t lo, hi;                     // lowest and highest reading in the hour, mV
  uint16_t first, last;                // first and latest reading in the hour, mV
};

struct BattLog {
  BattHour h[BATT_HOURS];              // indexed by epoch hour % 24
  uint32_t day;                        // local day of day_mv; 0 = none
  uint32_t day_hour;                   // epoch hour of day_mv
  uint16_t day_mv;                     // first reading of that day
  uint16_t yesterday_mv;               // first reading of the last whole local day
  uint16_t before_mv;                  // first reading of the whole day before it
  uint8_t full_days;                   // whole days recorded in a row, up to 2
};

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
  uint8_t level;                       // DIAG_GOOD, DIAG_CAUTION, DIAG_PROBLEM or DIAG_UNKNOWN
  uint16_t now_mv, lo_mv, hi_mv;
  uint8_t lo_hour, hi_hour;            // local hours
  int16_t net_mv;                      // now against the oldest reading
  uint8_t charge_hours;                // hours that rose
  int16_t night_drain;                 // mV per hour, local 10pm to 5am; -1 when unknown
  uint8_t chart_start;                 // local hour of the oldest column
  uint16_t chart[BATT_HOURS];          // mV, oldest first, ending now; 0 = no data
};

void battSample(BattLog& log, uint32_t hour, int8_t tz_hours, uint16_t mv);
BattView battAnalyse(const BattLog& log, uint32_t now_hour, int8_t tz_hours, bool calibrated);
int battFormat(const BattView& v, char lines[][BATT_LINE], int max_lines);
// One-line form for the diag summary; returns the view's level.
int battSummary(const BattView& v, char* out, size_t len);
