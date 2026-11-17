#pragma once

#include <helpers/diag_reports/DiagBatt.h>
#include <helpers/diag_reports/DiagEvidence.h>
#include <helpers/diag_reports/DiagLinks.h>
#include <helpers/diag_reports/DiagNoise.h>
#include <helpers/diag_reports/DiagOut.h>
#include <helpers/diag_reports/DiagScan.h>
#include <helpers/diag_reports/DiagStore.h>

// The diag engine: evidence in, competing causes scored, each symptom attributed to the cause that explains it, one fix out. Pure.

enum DiagAreaId : uint8_t {
  AREA_LINKS, AREA_LNA, AREA_NEARBY, AREA_NOISE, AREA_GUARD, AREA_DECODES, AREA_BATTERY, AREA_LOAD, AREA_PATHS, AREA_CLOCK, AREA_COUNT,
};

enum DiagCause : uint8_t {
  CAUSE_NONE,
  CAUSE_BATT_LOW,
  CAUSE_GUARD_OFF,                     // freezes or lost protection with nothing to clear them
  CAUSE_NOISE,                         // a source on the channel during certain hours
  CAUSE_BLOCKER,                       // a strong signal beside the channel freezing the receiver
  CAUSE_ANTENNA,                       // this node's antenna or feedline
  CAUSE_LNA_FAULT,
  CAUSE_LNA_OVERLOAD,
  CAUSE_COLD,                          // charging paused below freezing
  CAUSE_LOAD_DRAIN,                    // battery loss follows channel load
  CAUSE_PANEL,                         // charging not keeping up
  CAUSE_FREEZES,                       // receiver freezes with no source caught
  CAUSE_FAR_SIDE,                      // a neighbour that does not hear this node
  CAUSE_WEAK_LINK,
  CAUSE_CONGESTION,
  CAUSE_COLLISIONS,
  CAUSE_LNA_SETTING,                   // the LNA is not in the state its study advised
  CAUSE_HEAT,
  CAUSE_SINGLE_PATH,
  CAUSE_CLOCK,
  CAUSE_STRONG_NEIGHBOUR,              // a very strong neighbour swamping the receiver
  CAUSE_FOREIGN_LORA,                  // another LoRa network sharing the channel
  CAUSE_COUNT,
};

#define ENGINE_AUTO_ON     0
#define ENGINE_AUTO_OFF    1
#define ENGINE_AUTO_PAUSED 2

struct EngineInput {
  const DiagStore* store;
  const LinkTable* links;
  const NoiseView* noise;
  const BattView* batt;
  uint32_t now_hour, now_ms, uptime_s;
  int8_t tz;
  uint8_t sf;
  bool clock_set, batt_present;
  bool lna_available, lna_on;          // the gate, and the live state
  bool radio_sx126x;                   // scan and guard registers exist
  bool have_scan;
  uint32_t scan_age_min;
  ScanFinding scan;
  uint8_t hist_kind;                   // HistKind
  uint8_t temp_kind;                   // TempKind
  bool has_temp;
  int8_t temp_now;
  bool study_active;
  uint8_t study_result;                // LnaVerdict
  int16_t study_percent_on;
  int8_t study_floor_shift;            // floor with the LNA on minus off, dB; 0 = unknown
  uint16_t study_h, study_of_h;
  uint32_t recv, errors;               // last 24 h when the noise watch has counts, else since boot
  uint32_t air_s;
  uint8_t agc_interval;                // AGC reset interval as configured; 0 = off
  bool guard_known, guard_on, frozen_now;   // the live register read
  uint8_t auto_state;
  uint16_t pause_min;
  uint16_t cleared_one_way;            // neighbour id a probe ping just proved two-way; 0 = none
  uint8_t retry_one_way;               // 1 + links index whose direct-retry resends mostly go unconfirmed; 0 = none
  uint16_t retry_tried, retry_won;     // that hop's resends and those confirmed
  const DiagEvidence* ev;              // the shared evidence; built by the caller from the same sources
  const char* lna_auto;                // LNA auto status ("auto: off now; on 12-14"), or nullptr when auto is off
  bool lna_auto_dead;                  // auto parked because the LNA does not work
};

struct EngineArea {
  bool shown;
  uint8_t level;
  uint8_t cause;                       // DiagCause that explains it, or CAUSE_NONE
  bool learning;                       // unknown only until more data arrives
  char text[48];
};

struct EngineResult {
  EngineArea area[AREA_COUNT];
  uint8_t score[CAUSE_COUNT];          // 0-100 per cause
  uint8_t cause;                       // the leading cause
  uint8_t level;                       // its severity
  uint32_t cause_hours;                // local hours the leading cause shows in; 0 = no hour pattern
  uint8_t explained;                   // areas the leading cause explains
  char headline[48];
  char fix[2][64];
  char see[48];
  char confidence[56];
};

void engineRun(const EngineInput& in, EngineResult& out);
void engineFormat(const EngineInput& in, const EngineResult& r, DiagOut& o);
// True when battery loss over the 30-day ring is worse after busy days than after quiet ones.
bool engineLoadDrain(const DiagStore& s);
// A 24-bit mask moved between local and UTC hours of day.
uint32_t engineRotate(uint32_t mask, int hours);
const char* engineCauseName(uint8_t cause);
// The check `diag probe` runs to confirm a cause: "scan", "noise", "guard", "lna", "batt", "ping" (the neighbour in question), or "" when none applies.
const char* engineProbeFor(uint8_t cause);
