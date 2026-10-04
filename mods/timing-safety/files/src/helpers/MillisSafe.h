#pragma once

#include <Arduino.h>
#include <stdint.h>

// Wrap-safe deadline check: millis() >= target fails across the 49-day 32-bit wrap.
// Signed subtraction (2's complement) is what makes the comparison survive it.
inline bool millis_passed(unsigned long target) {
  return (long)(millis() - target) > 0;
}

// Caps an elapsed span rather than reporting years, which reads as corruption. A reboot
// resets VolatileRTCClock to its fallback epoch while persisted timestamps keep real time.
#define SAFE_ELAPSED_MAX_SECS (365UL * 24 * 60 * 60)

inline uint32_t safeElapsedSecs(uint32_t current_time, uint32_t recorded_timestamp) {
  if (recorded_timestamp > current_time) {
    return 0;   // clock corrected backwards -- treat as "just now"
  }
  uint32_t elapsed = current_time - recorded_timestamp;
  return elapsed > SAFE_ELAPSED_MAX_SECS ? SAFE_ELAPSED_MAX_SECS : elapsed;
}
