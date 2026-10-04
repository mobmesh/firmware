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

  // Post-OTA probation state of the running image. `known` is false when ESP-IDF cannot report
  // it, and callers must then fail closed rather than assume the rollback image is expendable.
  struct ProbationState {
    bool known;
    bool pending;
    uint32_t remaining_secs;
  };
  ProbationState probation();

  // Reports the firmware unhealthy. Forces rollback and reboot when this boot is pending-verify
  // (does not return); returns false otherwise so the caller falls back to its own handling.
  bool reportUnhealthy();

  // From radio_init() failure once in-boot retries are spent: reportUnhealthy() first, then a
  // persisted cross-boot retry-with-cap, then a permanent halt. Does not return.
  void onRadioInitFailure();

  // For `get ota.slot`. otadata only records intent, so the inactive slot also carries
  // esp_image_verify()'s verdict: image-ok, image-invalid, image-absent, or image-unchecked.
  const char* status(bool verify_inactive);

  // For the OTA page's identity payload: the same facts `status()` renders, as data.
  // `target` is the slot an upload would overwrite, and `target_size` its capacity.
  struct Slots {
    char active;
    char target;
    char active_version[24];
    char target_version[24];
    const char* active_state;
    uint32_t target_size;
  };
  Slots slots();

  // For `ota slot boot <A|B>`: repoints the bootloader without touching flash. Refuses a target
  // already active, absent, or carrying no valid image. Fills reply[] either way.
  bool setActivePartition(char letter, char reply[]);
}
