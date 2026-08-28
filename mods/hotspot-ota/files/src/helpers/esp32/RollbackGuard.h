#pragma once
#include <Arduino.h>

#ifndef OTA_MOD_SHORT_SHA
#define OTA_MOD_SHORT_SHA "unknown"
#endif

// Confirms or rejects the running firmware during ESP-IDF's post-OTA "pending verify" window. A
// no-op on any normal, already-confirmed boot -- only acts on the first boot after an OTA update.
//
// Arduino's initArduino() auto-confirms every new image via a weak `verifyOta()` hook (default
// `return true`) before setup() runs -- RollbackGuard.cpp overrides it to defer that decision here.
namespace RollbackGuard {
  // Call once from setup(), after radio_init() succeeds. Records the boot time for poll() and
  // clears the persisted radio-failure counter so an unrelated future failure counts fresh.
  // `version` (e.g. "v1.16.0-0f11a30", see call sites) is recorded here but not written to SPIFFS
  // until the first poll() call -- setup()'s call chain is already deep by this point, and writing
  // here directly reproduced the same stack-depth boot instability as the handleGetCmd() incident.
  void begin(const char* version);

  // Call every loop() iteration. Writes the version passed to begin() to SPIFFS on its first call
  // (deferred from begin() -- see above), then marks the image valid once the confirm delay has
  // elapsed since begin(). Both are no-ops once done, forever after.
  void poll();

  // Call from a failure path to report the firmware unhealthy. If this boot is pending-verify,
  // forces immediate rollback + reboot (does not return). Otherwise returns false so the caller
  // falls back to its own handling (nothing to roll back to, or not on probation).
  bool reportUnhealthy();

  // Call from radio_init() failure after in-boot retries are exhausted. Tries reportUnhealthy()
  // first; if not applicable, falls back to a persisted cross-boot retry-with-cap, then halts
  // permanently once exhausted -- same terminal behavior as before this feature existed. Does not
  // return.
  void onRadioInitFailure();

  // For `get ota.slot` -- "Slots: A=<ver> (active, <state>) | B=<ver> (<state>)" (or B active),
  // each state one of "pending" (only possible for the running slot), "valid", "invalid",
  // "aborted", "new", or "n/a" (never involved in an OTA). <ver> is "v?" for a slot that's never
  // actually booted (nothing persisted for it yet) -- the same slots this reports "n/a" for.
  const char* status();

  // For `ota slot boot <A|B>` -- points the bootloader at the other OTA slot without touching flash
  // contents. Refuses if the target is already active, doesn't exist, or has no valid app image.
  // Fills reply[] either way; caller reboots on true.
  bool setActivePartition(char letter, char reply[]);
}
