#pragma once

#include <Arduino.h>

// Awake and idle on ESP32: a 1 ms rest instead of spinning lets the CPU idle; inline so the loop compiles as if written in place.
template <typename Pending>
inline void modIdleRest(Pending pending) {
#if defined(ESP32)
  if (!pending()) delay(1);
#else
  (void)pending;
#endif
}
