#pragma once

#include <stddef.h>
#include <stdint.h>

// Pure noise-watch arithmetic and text: hourly floors and decode counts in, report out. No radio, clock or storage.

#define NOISE_HOURS     24
#define NOISE_BLOCKS    6
#define NOISE_LINE      56
#define NOISE_LNA_NA    2              // board without a switchable LNA
#define NOISE_NO_MARGIN -1000.0f       // no neighbour judged yet

struct NoiseHour {
  uint32_t hour;                       // UTC epoch hour the slot holds; 0 = empty
  int8_t floor_dbm;                    // median idle RSSI over the hour
  uint8_t lna;                         // LNA state while sampled: LNA_OFF, LNA_ON or NOISE_LNA_NA
  uint16_t recv;                       // packets decoded during the hour
  uint16_t errors;                     // failed decodes during the hour
  uint8_t counts_valid;                // 0 when the counters reset mid-hour
};

struct NoiseLog {
  NoiseHour h[NOISE_HOURS];            // indexed by epoch hour % 24
};

enum NoiseKind : uint8_t {
  NOISE_K_NONE,                        // no hours yet
  NOISE_K_QUIET,
  NOISE_K_ERRORS,                      // decodes failing without a noise rise
  NOISE_K_LOUD,                        // loud, too few packets to judge decodes
  NOISE_K_LOUD_OK,                     // loud, decodes unaffected
  NOISE_K_LOUD_DROP,                   // loud enough to sink the weakest link
  NOISE_K_LOUD_LOSS,                   // loud and failed decodes rose with it
};

struct NoiseBlock {
  bool data;
  int8_t lo, hi;                       // rise above the floor, dB
  uint16_t err_tenths;                 // failed-decode rate, 0.1 %
  bool err_high;
  uint8_t level;
};

struct NoiseView {
  uint8_t hours;                       // usable hours in the last 24
  int8_t floor_dbm;
  uint8_t floor_start, floor_len;      // quietest run, local hours
  uint8_t level;                       // DIAG_GOOD, DIAG_CAUTION, DIAG_PROBLEM or DIAG_UNKNOWN
  uint8_t kind;                        // NoiseKind
  int8_t max_rise;
  bool scattered;                      // loud at three or more separate times
  uint8_t loud_start, loud_len;        // longest loud run, local hours
  bool drops;
  uint8_t drop_start, drop_len;        // longest run where the rise exceeds the weakest link's margin
  bool have_margin;
  int8_t spare_db;                     // weakest link's margin left at the loudest hour
  bool mesh_known, mesh_busy;          // loud hours carry at least twice the packets of quiet ones
  uint8_t err_start;                   // first local hour of a block with raised errors
  NoiseBlock block[NOISE_BLOCKS];
};

void noiseAddHour(NoiseLog& log, const NoiseHour& hour);
// Only hours sampled in the given LNA state count: the LNA moves the floor by 20 dB or more.
NoiseView noiseAnalyse(const NoiseLog& log, uint32_t now_hour, int8_t tz_hours, uint8_t lna, float margin_db);
int noiseFormat(const NoiseView& v, const char* weakest, char lines[][NOISE_LINE], int max_lines);
// One-line form for the diag summary; returns the view's level.
int noiseSummary(const NoiseView& v, char* out, size_t len);
// Packets and failed decodes over the last 24 h, any LNA state; false when no hour has counts.
bool noiseCounts(const NoiseLog& log, uint32_t now_hour, uint32_t* recv, uint32_t* errors);
void noiseHourText(uint8_t hour, bool brief, char* out, size_t len);
