#pragma once

#include <Arduino.h>
#include <stdint.h>

// One trial at a time. A deployed node is testing one thing, and a single flat record needs no
// array parsing on the way back off flash.
struct TrySetSlot {
  char     key[24];
  char     snapshot[24];
  char     trial[24];
  uint32_t expires_at;      // RTC epoch seconds

  bool active() const { return key[0] != 0; }
  void clear() { key[0] = 0; snapshot[0] = 0; trial[0] = 0; expires_at = 0; }
};

// Persisted so an expiry survives a busy loop, not so a trial survives a reboot: a brownout
// leaves the RTC untrustworthy, so a slot found at boot is reverted rather than resumed.
bool trySetLoad(TrySetSlot& slot);
bool trySetSave(const TrySetSlot& slot);
void trySetErase();
