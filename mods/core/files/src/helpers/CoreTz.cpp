#include <stdio.h>
#include <string.h>
#include <helpers/ConfigSerializer.h>
#include <helpers/CoreIntegration.h>
#include <helpers/ModFS.h>
#include <helpers/ModTz.h>

#define TZ_PATH        "/core_tz.json"
#define TZ_LEGACY_PATH "/rdiag.json"     // diag-report kept the offset in its own file

// Stored as upstream's companion stores tz_offset: int8_t whole hours, key tz_offset.
class TzConfig : public ConfigSerializer {
public:
  int8_t tz_offset = 0;
protected:
  void structure() override { def("tz_offset", tz_offset); }
};

static TzConfig config;
static bool loaded;

static bool readFrom(const char* path, TzConfig& into) {
  if (!ModFS.exists(path)) return false;
  File f = ModFS.open(path, "r");
  if (!f) return false;
  into.loadSerial(f);
  f.close();
  if (into.tz_offset < -12 || into.tz_offset > 14) into.tz_offset = 0;
  return true;
}

static bool save() {
  File f = ModFS.open(TZ_PATH, "w", true);
  if (!f) return false;
  bool ok = config.saveSerial(f);
  f.close();
  return ok;
}

// Loaded on first use; a node upgraded from diag-report keeps the offset it had there, once.
static void load() {
  if (loaded) return;
  loaded = true;
  if (!ModFS.begin(true)) return;
  if (readFrom(TZ_PATH, config)) return;
  TzConfig legacy;
  if (readFrom(TZ_LEGACY_PATH, legacy) && legacy.tz_offset != 0) {
    config.tz_offset = legacy.tz_offset;
    save();
  }
}

int8_t modTzHours() {
  load();
  return config.tz_offset;
}

bool modTzSet(int8_t hours) {
  load();
  config.tz_offset = hours;
  return save();
}

bool modTzParse(const char* text, int8_t* hours) {
  while (*text == ' ') text++;
  int sign = 1;
  if (*text == '+' || *text == '-') {
    if (*text == '-') sign = -1;
    text++;
  }
  if (*text < '0' || *text > '9') return false;
  int value = 0;
  while (*text >= '0' && *text <= '9') {
    value = value * 10 + (*text - '0');
    if (value > 99) return false;
    text++;
  }
  if (*text == 'h' || *text == 'H') text++;
  while (*text == ' ') text++;
  if (*text != 0) return false;
  value *= sign;
  if (value < -12 || value > 14) return false;
  *hours = (int8_t)value;
  return true;
}

// `diag.tz` is the name diag-report used; both names read and write the one setting.
bool coreTzCli(char* command, char* reply) {
  if (strcmp(command, "get tz") == 0 || strcmp(command, "get diag.tz") == 0) {
    sprintf(reply, "> %d", modTzHours());
    return true;
  }
  const char* value = nullptr;
  if (strncmp(command, "set tz ", 7) == 0) value = command + 7;
  else if (strncmp(command, "set diag.tz ", 12) == 0) value = command + 12;
  if (value == nullptr) return false;
  int8_t hours;
  if (!modTzParse(value, &hours)) {
    strcpy(reply, "Error, must be from -12 to +14");
    return true;
  }
  strcpy(reply, modTzSet(hours) ? "OK" : "ERR: could not save");
  return true;
}
