#include <Arduino.h>
#include <math.h>
#include <helpers/ModHooks.h>
#include <helpers/ModTemperature.h>

float modMcuTemperature() {
#if defined(ESP32)
  // Upstream's ESP32 reader sums into an unsigned integer, which drops fractions and breaks below 0 °C; this averages floats.
  float sum = 0;
  for (int i = 0; i < 4; i++) sum += temperatureRead();
  return sum / 4;
#else
  return modBoardMcuTemperature();
#endif
}
