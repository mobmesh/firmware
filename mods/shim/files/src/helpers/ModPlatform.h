#pragma once

// Chip services mods share; on ESP32 they are the IDF calls themselves, so the code is unchanged there.
#if defined(ESP32)
#include <esp_system.h>
#define modRandom esp_random
#define modFillRandom esp_fill_random
#elif defined(NRF52_PLATFORM)
#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>

// The core's random() draws on the hardware RNG once seeded; mods use it for jitter and identifiers.
static inline uint32_t modRandom() { return ((uint32_t)random(0x10000) << 16) | (uint32_t)random(0x10000); }
static inline void modFillRandom(void* buf, size_t len) {
  uint8_t* out = (uint8_t*)buf;
  for (size_t i = 0; i < len; i++) out[i] = (uint8_t)random(0x100);
}
#else
#error "ModPlatform.h: no mapping for this platform"
#endif
