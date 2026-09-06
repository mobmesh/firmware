#include <helpers/esp32/TempSet.h>

#include <SPIFFS.h>

#define TEMP_SET_PATH  "/tempset"
#define TEMP_SET_MAGIC 0x54535431UL   // "TST1"

// Mounted here rather than assumed: this mod does not require hotspot-ota, which is what
// otherwise calls SPIFFS.begin() first. A second begin() on a mounted volume is a no-op.
static bool mounted = false;

static bool ensureMounted() {
  if (!mounted) mounted = SPIFFS.begin(true);
  return mounted;
}

bool tempSetLoad(TempSetSlot& slot) {
  slot.clear();
  if (!ensureMounted() || !SPIFFS.exists(TEMP_SET_PATH)) return false;

  File f = SPIFFS.open(TEMP_SET_PATH, "r");
  if (!f) return false;

  uint32_t magic = 0;
  bool ok = f.read((uint8_t*)&magic, sizeof(magic)) == sizeof(magic)
            && magic == TEMP_SET_MAGIC
            && f.read((uint8_t*)&slot, sizeof(slot)) == sizeof(slot);
  f.close();

  if (!ok || !slot.active()) {
    slot.clear();
    return false;
  }
  slot.key[sizeof(slot.key) - 1] = 0;
  slot.snapshot[sizeof(slot.snapshot) - 1] = 0;
  slot.trial[sizeof(slot.trial) - 1] = 0;
  return true;
}

bool tempSetSave(const TempSetSlot& slot) {
  if (!ensureMounted()) return false;
  File f = SPIFFS.open(TEMP_SET_PATH, "w", true);
  if (!f) return false;

  uint32_t magic = TEMP_SET_MAGIC;
  bool ok = f.write((const uint8_t*)&magic, sizeof(magic)) == sizeof(magic)
            && f.write((const uint8_t*)&slot, sizeof(slot)) == sizeof(slot);
  f.close();
  return ok;
}

void tempSetErase() {
  if (ensureMounted() && SPIFFS.exists(TEMP_SET_PATH)) SPIFFS.remove(TEMP_SET_PATH);
}
