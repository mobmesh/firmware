#include <helpers/esp32/TrySet.h>

#include <SPIFFS.h>

#define TRY_SET_PATH  "/tryset"
#define TRY_SET_MAGIC 0x54535431UL   // "TST1"

// Mounted here rather than assumed: this mod does not require hotspot-ota, which is what
// otherwise calls SPIFFS.begin() first. A second begin() on a mounted volume is a no-op.
static bool mounted = false;

static bool ensureMounted() {
  if (!mounted) mounted = SPIFFS.begin(true);
  return mounted;
}

bool trySetLoad(TrySetSlot& slot) {
  slot.clear();
  if (!ensureMounted() || !SPIFFS.exists(TRY_SET_PATH)) return false;

  File f = SPIFFS.open(TRY_SET_PATH, "r");
  if (!f) return false;

  uint32_t magic = 0;
  bool ok = f.read((uint8_t*)&magic, sizeof(magic)) == sizeof(magic)
            && magic == TRY_SET_MAGIC
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

bool trySetSave(const TrySetSlot& slot) {
  if (!ensureMounted()) return false;
  File f = SPIFFS.open(TRY_SET_PATH, "w", true);
  if (!f) return false;

  uint32_t magic = TRY_SET_MAGIC;
  bool ok = f.write((const uint8_t*)&magic, sizeof(magic)) == sizeof(magic)
            && f.write((const uint8_t*)&slot, sizeof(slot)) == sizeof(slot);
  f.close();
  return ok;
}

void trySetErase() {
  if (ensureMounted() && SPIFFS.exists(TRY_SET_PATH)) SPIFFS.remove(TRY_SET_PATH);
}
