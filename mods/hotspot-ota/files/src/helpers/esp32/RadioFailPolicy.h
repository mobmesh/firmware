#pragma once

#include <stdint.h>

// What a failed radio init does next: restart to retry, or deep sleep for sleep_secs.
struct RadioFailAction {
  bool restart;
  uint32_t sleep_secs;
};

// A count that did not persist reads as zero next boot, so a restart would loop without bound.
inline RadioFailAction radioFailAction(uint8_t count, bool durable, uint8_t cap,
                                       uint32_t start_secs, uint32_t max_secs) {
  if (!durable) return {false, max_secs};
  if (count < cap) return {true, 0};
  uint32_t shift = count - cap;
  uint32_t secs = max_secs;
  if (shift < 16) secs = start_secs << shift;
  if (secs > max_secs) secs = max_secs;
  return {false, secs};
}
