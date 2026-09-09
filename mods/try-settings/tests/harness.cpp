// Host-side fakes for everything TrySetIntegration.cpp calls out to. The real source is
// compiled unmodified against these, so a case exercises what ships.
#include "harness.h"

#include <map>
#include <string>
#include <vector>

Fake fake;

void Fake::reset() {
  prefs.clear();
  dispatched.clear();
  clock = 1000000;
  save_ok = true;
  saved = false;
  record = TrySetSlot{};
  temp_radio_live = false;
  set_fails.clear();
  get_missing.clear();
  set_persists_before_failing.clear();
}

uint32_t modClockGet() { return fake.clock; }
void modClockSet(uint32_t epoch) { fake.clock = epoch; }

bool modTempRadioGet(float* freq, float* bw, uint8_t* sf, uint8_t* cr) {
  if (!fake.temp_radio_live) return false;
  *freq = 915.0f; *bw = 250.0f; *sf = 11; *cr = 5;
  return true;
}

// Stands in for upstream's CLI chain: answers get/set the way CommonCLI does, including
// the "> value" read format try-settings parses.
void modCliDispatch(uint32_t, char* command, char* reply) {
  fake.dispatched.push_back(command);
  std::string c(command);

  if (c.rfind("get ", 0) == 0) {
    std::string key = c.substr(4);
    if (fake.get_missing.count(key)) { strcpy(reply, "Error: unsupported"); return; }
    auto it = fake.prefs.find(key);
    sprintf(reply, "> %s", it == fake.prefs.end() ? "" : it->second.c_str());
    return;
  }
  if (c.rfind("set ", 0) == 0) {
    size_t sp = c.find(' ', 4);
    std::string key = c.substr(4, sp - 4), value = c.substr(sp + 1);
    if (fake.set_fails.count(key)) {
      // radio.rxgain's shape: the pref moves even though the reply refuses.
      if (fake.set_persists_before_failing.count(key)) fake.prefs[key] = value;
      strcpy(reply, "Error: refused");
      return;
    }
    fake.prefs[key] = value;
    strcpy(reply, "OK");
    return;
  }
  if (c.rfind("tempradio", 0) == 0) { fake.temp_radio_live = true; strcpy(reply, "OK"); return; }
  strcpy(reply, "Unknown command");
}

bool trySetLoad(TrySetSlot& slot) {
  if (!fake.saved) return false;
  slot = fake.record;
  return true;
}

bool trySetSave(const TrySetSlot& slot) {
  if (!fake.save_ok) return false;
  fake.record = slot;
  fake.saved = true;
  return true;
}

void trySetErase() { fake.saved = false; fake.record = TrySetSlot{}; }
