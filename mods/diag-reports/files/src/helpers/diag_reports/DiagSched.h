#pragma once

#include <helpers/diag_reports/DiagStore.h>

// Automatic captures: daily caps, spacing, the guards that stand them down, and the diag auto switch. Pure.

enum DiagTrigger : uint8_t {
  TRIG_FREEZES,                        // T1: repeated freezes -> scan
  TRIG_LOUD,                           // T2: loud onset -> RSSI burst
  TRIG_DECODES,                        // T3: failed-decode spike -> RSSI burst
  TRIG_COUNT,
};

#define SCHED_SCAN_CAP     3
#define SCHED_BURST_CAP    24
#define SCHED_SPACING_S    3600         // at most one capture per trigger an hour
#define SCHED_PAUSE_MAX    1440
#define SCHED_BUSY_PCT     25
#if defined(WITH_POWER_GUARD) && defined(POWER_GUARD_AUTO_ON_MV) && POWER_GUARD_AUTO_ON_MV > 0
#define SCHED_LOW_MV       (POWER_GUARD_AUTO_ON_MV + 150)   // LNA auto tests stop 150 mV above power-guard's engage voltage
#else
#define SCHED_LOW_MV       3450         // LNA auto tests stop here on builds without power-guard
#endif

struct SchedConditions {
  uint32_t epoch;                      // 0 = clock unset
  uint32_t local_day;
  bool auto_off;
  bool paused;
  bool packet_arriving;
  uint16_t batt_mv;                    // 0 = unknown
  uint8_t load_pct;                    // the last hour
  uint32_t scan_age_s;                 // 0xFFFFFFFF = no scan
};

uint8_t schedCap(uint8_t trigger);
// Whether a capture may run now; `why` names the guard that held it back.
// LNA auto tests transmit and switch the LNA on: they stop on a low battery; captures only listen and keep running.
bool schedTestBatteryLow(uint16_t batt_mv);
bool schedAllow(const DiagStore& s, uint8_t trigger, const SchedConditions& c, const char** why);
// Records a capture against the day's cap.
void schedUse(DiagStore& s, uint8_t trigger, const SchedConditions& c);
// Today's use of a trigger, 0 after the day turns.
uint8_t schedUsed(const DiagStore& s, uint8_t trigger, uint32_t local_day);

// T3: three or more failed decodes since the counter was read a minute ago; a counter that went back is no spike.
bool schedDecodeSpike(uint32_t errors_then, uint32_t errors_now);

// `diag auto pause [minutes]`: minutes from text, default 5, 1..1440; -1 when invalid.
int schedPauseMinutes(const char* text);
