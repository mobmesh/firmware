#pragma once

#include <helpers/diag_reports/DiagOut.h>
#include <helpers/diag_reports/DiagStore.h>

// Noise watch: the store's hourly floors and decode counts in, a verdict and report out. Pure.

#define NOISE_BLOCKS    6
#define NOISE_MIN_HOURS 6
#define NOISE_NO_MARGIN -1000.0f       // no neighbour judged yet

enum NoiseKind : uint8_t {
  NOISE_K_NONE,                        // no hours yet
  NOISE_K_QUIET,
  NOISE_K_ERRORS,                      // decodes failing without a noise rise
  NOISE_K_LOUD,                        // loud, too few packets to judge decodes
  NOISE_K_LOUD_OK,                     // loud, decodes unaffected
  NOISE_K_LOUD_DROP,                   // loud enough to sink the weakest link
  NOISE_K_LOUD_LOSS,                   // loud and failed decodes rose with it
  NOISE_K_FREEZES,                     // receiver freezes, no noise rise
  NOISE_K_BUSY,                        // loud hours that are mesh traffic, not interference
};

struct NoiseBlock {
  bool data;
  int8_t lo, hi;                       // rise above the floor, dB
  uint16_t err_tenths;                 // failed-decode rate, 0.1 %
  bool err_high;
  uint8_t freezes;
  uint8_t level;
};

struct NoiseView {
  uint8_t hours;                       // usable hours in the last 24
  int8_t floor_dbm;
  uint8_t floor_start, floor_len;      // quietest run, local hours
  uint8_t level;
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
  uint8_t freezes;                     // freezes in the window
  uint8_t freeze_start, freeze_len;    // longest run of hours with a freeze
  uint32_t loud_mask;                  // bit per local hour that was loud
  uint32_t err_mask;                   // bit per local hour in a block with raised failed decodes
  uint32_t freeze_mask;                // bit per local hour with a receiver freeze
  uint32_t have_mask;                  // bit per local hour with a usable floor
  NoiseBlock block[NOISE_BLOCKS];
};

// Only hours sampled in the given LNA state count: the LNA moves the floor by 20 dB or more.
NoiseView noiseAnalyse(const DiagStore& s, uint32_t now_hour, int8_t tz_hours, uint8_t lna, float margin_db);
// `capture` is the last loud-onset burst, or nullptr.
void noiseFormat(const NoiseView& v, const char* weakest, const StoreCapture* capture, int8_t tz_hours, const DiagNote* note, DiagOut& o);
// One-line form for the diag screen; returns the view's level.
int noiseSummary(const NoiseView& v, char* out, size_t len);
// The loud hours are mesh traffic: a capture saw packets, or they carry twice the packets of quiet hours.
void noiseMarkBusy(NoiseView& v);
void noiseLoudText(const NoiseView& v, char* out, size_t len);   // "8am-5pm", "at times" or "all day"
// Packets and failed decodes over the last 24 h, any LNA state; false when no hour has counts.
bool noiseCounts(const DiagStore& s, uint32_t now_hour, uint32_t* recv, uint32_t* errors);
// Longest run of set flags around the 24-hour circle; false when none is set.
bool diagLongestRun(const bool flags[24], uint8_t* start, uint8_t* len, int* runs);
