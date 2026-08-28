#pragma once
#include <Arduino.h>

#ifndef OTA_MOD_SHORT_SHA
#define OTA_MOD_SHORT_SHA "unknown"
#endif

// Confirms or rejects the running firmware in ESP-IDF's post-OTA pending-verify window; a no-op
// on an already-confirmed boot. RollbackGuard.cpp overrides Arduino's auto-confirming verifyOta().
namespace RollbackGuard {
  // Once from setup() after radio_init() succeeds; clears the persisted radio-failure counter.
  // `version` reaches SPIFFS on the first poll(): setup()'s call chain is too deep to write from.
  void begin(const char* version);

  // Every loop() iteration: writes begin()'s version on the first call, then marks the image
  // valid once the confirm delay elapses. Both no-ops once done.
  void poll();

  // Reports the firmware unhealthy. Forces rollback and reboot when this boot is pending-verify
  // (does not return); returns false otherwise so the caller falls back to its own handling.
  bool reportUnhealthy();

  // From radio_init() failure once in-boot retries are spent: reportUnhealthy() first, then a
  // persisted cross-boot retry-with-cap, then a permanent halt. Does not return.
  void onRadioInitFailure();

  // For `get ota.slot`: "Slots: A=<ver> (active, <state>) | B=<ver> (<state>)". State is pending,
  // valid, invalid, aborted, new, or n/a; <ver> is "v?" for a slot that has never booted.
  const char* status();

  // For `ota slot boot <A|B>`: repoints the bootloader without touching flash. Refuses a target
  // already active, absent, or carrying no valid image. Fills reply[] either way.
  bool setActivePartition(char letter, char reply[]);
}
