#pragma once

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// Overridable only so a bench build can shorten the window; shipping builds use these.
#ifndef PROBATION_CRASH_CHECK_MS
#define PROBATION_CRASH_CHECK_MS   90000UL
#endif
#ifndef PROBATION_WINDOW_MS
#define PROBATION_WINDOW_MS        3600000UL
#endif
#ifndef PROBATION_ADVERT_GAP_MS
#define PROBATION_ADVERT_GAP_MS    1200000UL
#endif
#define PROBATION_ADVERTS          3
#define PROBATION_HEARD_WITHIN_MS  (3UL * 24 * 3600 * 1000)

// The mesh check applies only to a new image on a node that was hearing the mesh and is not short of power.
inline bool probationNeedsMeshCheck(bool pending, bool heard_before, bool known_good, bool battery_ok) {
  return pending && heard_before && !known_good && battery_ok;
}

// Adverts go out at the start of the window and then every gap, up to the cap.
inline bool probationAdvertDue(uint32_t window_elapsed_ms, uint8_t adverts_sent) {
  return adverts_sent < PROBATION_ADVERTS && window_elapsed_ms >= adverts_sent * PROBATION_ADVERT_GAP_MS;
}

inline bool probationWindowOver(uint32_t window_elapsed_ms) {
  return window_elapsed_ms >= PROBATION_WINDOW_MS;
}

// Uptime-based, so a node up for less than the limit counts anything heard since boot.
inline bool probationHeardRecently(bool have_rise, uint32_t last_rise_ms, uint32_t now_ms) {
  return have_rise && (uint32_t)(now_ms - last_rise_ms) <= PROBATION_HEARD_WITHIN_MS;
}

// Sum of flood_rx and direct_rx from a stats-packets reply; false when either is missing.
inline bool probationParseRx(const char* reply, uint32_t& total) {
  const char* flood = strstr(reply, "\"flood_rx\":");
  const char* direct = strstr(reply, "\"direct_rx\":");
  if (flood == NULL || direct == NULL) return false;
  total = (uint32_t)strtoul(flood + 11, NULL, 10) + (uint32_t)strtoul(direct + 12, NULL, 10);
  return true;
}

struct RejectOutcome {
  bool reject;      // the release stays rejected for timed updates
  bool remember;    // record it as the one release allowed a retry
};

// A crash rejects at once; any other rollback earns one retry, and a second one rejects.
inline RejectOutcome probationRejectOutcome(bool crash, bool already_retried) {
  if (crash || already_retried) return {true, false};
  return {false, true};
}
