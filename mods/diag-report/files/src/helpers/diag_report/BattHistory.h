#pragma once

#include <stddef.h>
#include <stdint.h>

// Thirty days of the morning battery low and CPU temperature, one record per local day: pure arithmetic and text, no clock or storage.

#define HIST_DAYS      30
#define HIST_LINE      112
#define HIST_MIN_DAYS  7               // a week of mornings before a verdict
#define HIST_NO_MV     0xFF
#define HIST_NO_TEMP   (-128)
#define HIST_NOON      12              // the morning low is the lowest reading before local noon

struct HistDay {
  uint8_t morning;                     // (mV - 2500) / 8, or HIST_NO_MV
  int8_t tmin, tmax;                   // CPU die, whole °C, or HIST_NO_TEMP
};

// The stored file: day d lives at d % HIST_DAYS; last_day is the newest local day recorded, 0 = none.
struct BattHistory {
  uint32_t last_day;
  HistDay d[HIST_DAYS];
};

// The only resident state: what the current local day has seen so far.
struct HistToday {
  uint32_t day;                        // local day number, as BattLog counts it; 0 = not started
  uint16_t morning_mv;                 // 0 = none yet
  int8_t tmin, tmax;
};

enum HistKind : uint8_t {
  HIST_K_WATCHING,
  HIST_K_HOLDING,
  HIST_K_LOSING,
  HIST_K_LOW,
};

struct HistView {
  uint8_t kind;                        // HistKind
  uint8_t level;                       // DIAG_GOOD, DIAG_CAUTION, DIAG_PROBLEM or DIAG_UNKNOWN
  uint8_t days;                        // days with a morning reading
  uint8_t missing;                     // days in the window without one
  uint8_t held, pairs;                 // day-to-day pairs that held within 30 mV, of all pairs
  uint16_t low_mv;
  uint32_t low_day;
  int16_t net_mv;                      // newest morning against the oldest
  uint8_t net_days;
  uint32_t first_day;                  // local day of the oldest column
  uint16_t chart[HIST_DAYS];           // morning mV, oldest first, ending today; 0 = none
};

void histInit(BattHistory& h);
void histTodayStart(HistToday& t, uint32_t local_day);
// Folds one reading into today; when the day has turned, copies the finished day to `finished` and returns true.
bool histSample(HistToday& t, HistToday& finished, uint32_t local_day, uint8_t local_hour, uint16_t mv, float temp_c);
// Records a finished day, marking any skipped days missing.
void histClose(BattHistory& h, const HistToday& t);
HistView histAnalyse(const BattHistory& h, const HistToday& t, bool calibrated);
// One day's record from the file, or today's running values; HIST_NO_MV and HIST_NO_TEMP where nothing was seen.
HistDay histDay(const BattHistory& h, const HistToday& t, uint32_t day);
// Day and month of a local day number (days since 1970 plus one, as BattLog counts), and "14 Oct" text.
void histDateParts(uint32_t local_day, unsigned* day, unsigned* month);
void histDateText(uint32_t local_day, char* out, size_t len);
extern const char* const HIST_MONTHS[12];
int histFormat(const HistView& v, char lines[][HIST_LINE], int max_lines);
