#pragma once
#include <helpers/esp32/TrySet.h>

#include <map>
#include <set>
#include <string>
#include <vector>

struct Fake {
  std::map<std::string, std::string> prefs;
  std::vector<std::string> dispatched;
  uint32_t clock = 1000000;

  bool save_ok = true;          // false makes trySetSave refuse, as a full filesystem would
  bool saved = false;           // whether a record is on the fake flash
  TrySetSlot record{};

  bool temp_radio_live = false;

  std::set<std::string> set_fails;                    // keys whose `set` is refused
  std::set<std::string> get_missing;                  // keys the board does not support
  std::set<std::string> set_persists_before_failing;  // refused, but the pref moved anyway

  void reset();
};

extern Fake fake;
