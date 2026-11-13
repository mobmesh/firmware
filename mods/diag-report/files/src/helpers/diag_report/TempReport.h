#pragma once

#include <helpers/diag_report/BattHistory.h>

// The CPU's own die temperature over 30 days, read from the battery history's daily records: pure arithmetic and text.

#define TEMP_LINE        112
#define TEMP_COLD_C      0             // most lithium chargers stop below freezing, and the pack is colder than the CPU
#define TEMP_HOT_C       65            // a CPU this hot means the enclosure is baking
#define TEMP_RECENT_DAYS 7             // the verdict looks at the last week; the month stays in the counts
#define TEMP_WEEKS       5             // table rows; the coldest/warmest line still covers all 30 days

enum TempKind : uint8_t {
  TEMP_K_NONE,                         // no sensor and no records
  TEMP_K_WATCHING,                     // a reading now, but no day recorded yet
  TEMP_K_NORMAL,
  TEMP_K_COLD,
  TEMP_K_HOT,
};

struct TempView {
  uint8_t kind;                        // TempKind
  uint8_t level;                       // DIAG_GOOD, DIAG_CAUTION or DIAG_UNKNOWN
  bool has_now;
  int8_t now_c;
  uint8_t days;                        // days with a reading in the window
  int8_t tmin, tmax;
  uint32_t cold_day, warm_day;
  uint8_t cold_days, hot_days;         // in the 30 days
  uint32_t first_day;                  // local day of the oldest column
  int8_t lows[HIST_DAYS], highs[HIST_DAYS];   // daily, oldest first, ending today; HIST_NO_TEMP = none
};

TempView tempAnalyse(const BattHistory& h, const HistToday& t, float now_c);
int tempFormat(const TempView& v, char lines[][TEMP_LINE], int max_lines);
