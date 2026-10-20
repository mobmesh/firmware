#pragma once

#include <stdint.h>
#include <string.h>

#define OTA_AUTO_INTERVAL_MIN   4
#define OTA_AUTO_INTERVAL_MAX   90
#define OTA_AUTO_INTERVAL_DEF   7
#define OTA_AUTO_CONFIRM_MIN    2
#define OTA_AUTO_CONFIRM_MAX    10
#define OTA_AUTO_CONFIRM_DEF    2
#define OTA_AUTO_DAY_SECS       86400u
#define OTA_AUTO_CLOCK_FLOOR    1715770351u

enum OtaAutoResult : uint8_t {
  OTA_AUTO_NONE,
  OTA_AUTO_CURRENT,
  OTA_AUTO_COUNTED,
  OTA_AUTO_INSTALLED,
  OTA_AUTO_FAILED,
  OTA_AUTO_SKIP_LOCK,
  OTA_AUTO_SKIP_BUSY,
  OTA_AUTO_REJECTED,
};

inline const char* otaAutoResultName(uint8_t result) {
  switch (result) {
    case OTA_AUTO_CURRENT: return "current";
    case OTA_AUTO_COUNTED: return "counted";
    case OTA_AUTO_INSTALLED: return "installed";
    case OTA_AUTO_FAILED: return "failed";
    case OTA_AUTO_SKIP_LOCK: return "skipped lock";
    case OTA_AUTO_SKIP_BUSY: return "skipped busy";
    case OTA_AUTO_REJECTED: return "skipped rolled-back";
  }
  return "none";
}

inline bool otaAutoClockBelievable(uint32_t now, uint32_t build_epoch) {
  return now > OTA_AUTO_CLOCK_FLOOR && now >= build_epoch;
}

// "<N>d", N in range; the d is required.
inline bool otaAutoParseInterval(const char* text, uint8_t& days) {
  uint32_t value = 0;
  size_t digits = 0;
  while (text[digits] >= '0' && text[digits] <= '9') {
    value = value * 10 + (uint32_t)(text[digits] - '0');
    if (++digits > 3) return false;
  }
  if (digits == 0 || text[digits] != 'd' || text[digits + 1] != 0) return false;
  if (value < OTA_AUTO_INTERVAL_MIN || value > OTA_AUTO_INTERVAL_MAX) return false;
  days = (uint8_t)value;
  return true;
}

inline bool otaAutoParseConfirm(const char* text, uint8_t& count) {
  uint32_t value = 0;
  size_t digits = 0;
  while (text[digits] >= '0' && text[digits] <= '9') {
    value = value * 10 + (uint32_t)(text[digits] - '0');
    if (++digits > 2) return false;
  }
  if (digits == 0 || text[digits] != 0) return false;
  if (value < OTA_AUTO_CONFIRM_MIN || value > OTA_AUTO_CONFIRM_MAX) return false;
  count = (uint8_t)value;
  return true;
}

// Next anchor + k*interval (k >= 1) after the last check; only a last check a full interval ahead counts from now.
inline uint32_t otaAutoNextDue(uint32_t anchor, uint8_t interval_days, uint32_t last_check, uint32_t now) {
  uint64_t step = (uint64_t)interval_days * OTA_AUTO_DAY_SECS;
  if (last_check > now && (uint64_t)(last_check - now) >= step) last_check = now;
  uint64_t k = 1;
  if (last_check >= anchor) k = (uint64_t)(last_check - anchor) / step + 1;
  uint64_t due = (uint64_t)anchor + k * step;
  return due > 0xffffffffull ? 0xffffffffu : (uint32_t)due;
}

// Spreads nodes whose clocks are set at the same moment, such as by one sync-time campaign.
inline uint32_t otaAutoNodeOffset(const uint8_t key[32]) {
  uint32_t mix = 2166136261u;
  for (int i = 0; i < 32; ++i) mix = (mix ^ key[i]) * 16777619u;
  return mix % OTA_AUTO_DAY_SECS;
}

// Sightings in a row after this one; installs once it reaches confirm.
inline uint8_t otaAutoSighting(bool same_as_candidate, uint8_t candidate_count) {
  if (!same_as_candidate) return 1;
  return candidate_count < 255 ? (uint8_t)(candidate_count + 1) : candidate_count;
}
