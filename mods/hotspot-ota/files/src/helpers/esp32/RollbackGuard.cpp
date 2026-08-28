#include "RollbackGuard.h"
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_system.h>   // esp_restart()
#include <SPIFFS.h>

// Long enough to catch an early crash/boot-loop; short enough not to add much to the ~2 minutes a
// hotspot-fetch update can already take.
#define OTA_ROLLBACK_CONFIRM_DELAY_MS   90000

// Cross-boot retry cap for onRadioInitFailure() -- gives a transient radio failure a few boot
// attempts to clear before bottoming out at a permanent halt. SPIFFS used for the counter instead
// of RTC memory (unreliable across esp_restart() on this chip/IDF combo).
#define RADIO_INIT_RESET_CAP   5
#define RADIO_FAIL_COUNT_PATH  "/radio_fail_count"

// Arduino's own rollback-confirmation hook runs inside initArduino(), before setup() -- too early to
// judge anything. This overrides it to defer to RollbackGuard instead. Must be extern "C": the weak
// symbol it replaces is declared in a plain .c file, and C++ mangling would otherwise link a
// different, unused symbol.
extern "C" bool verifyRollbackLater() { return true; }

static uint32_t boot_time_ms = 0;
static bool confirmed_or_not_applicable = false;

// begin() runs synchronously inside setup(), already deep in MeshCore's own init call chain --
// writing to SPIFFS there added enough extra stack-frame depth on top of that to reproduce the
// same class of USB-CDC-flicker/boot instability as the handleGetCmd() stack incident (see
// 987639c), even though no single variable here is anywhere near that large. Deferred to the
// first poll() instead, which runs from loop()'s much shallower call depth.
static char pending_version[24] = {0};
static bool version_write_pending = false;

static bool isPendingVerify(esp_ota_img_states_t* out_state = nullptr) {
  esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
  const esp_partition_t* running = esp_ota_get_running_partition();
  esp_ota_get_state_partition(running, &state);
  if (out_state) *out_state = state;
  return state == ESP_OTA_IMG_PENDING_VERIFY;
}

// 'A'/'B' labels for ota_0/ota_1 -- friendlier than raw partition names over a CLI.
static char partitionLetter(const esp_partition_t* p) {
  if (p->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_0) return 'A';
  if (p->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_1) return 'B';
  return '?';
}

static const esp_partition_t* findPartitionByLetter(char letter) {
  esp_partition_subtype_t subtype = (letter == 'A') ? ESP_PARTITION_SUBTYPE_APP_OTA_0 : ESP_PARTITION_SUBTYPE_APP_OTA_1;
  return esp_partition_find_first(ESP_PARTITION_TYPE_APP, subtype, NULL);
}

// esp_ota_get_state_partition() reads state persisted in the otadata partition, not something only
// the running image can see -- any partition's recorded state is readable from either slot.
static const char* stateLabel(esp_ota_img_states_t state) {
  switch (state) {
    case ESP_OTA_IMG_VALID: return "valid";
    case ESP_OTA_IMG_PENDING_VERIFY: return "pending";
    case ESP_OTA_IMG_INVALID: return "invalid";
    case ESP_OTA_IMG_ABORTED: return "aborted";
    case ESP_OTA_IMG_NEW: return "new";
    default: return "n/a";   // ESP_OTA_IMG_UNDEFINED -- never went through OTA (e.g. factory/USB flash)
  }
}

static uint8_t readFailCount() {
  File f = SPIFFS.open(RADIO_FAIL_COUNT_PATH, "r");
  if (!f) return 0;
  uint8_t count = 0;
  f.read(&count, 1);
  f.close();
  return count;
}

static void writeFailCount(uint8_t count) {
  File f = SPIFFS.open(RADIO_FAIL_COUNT_PATH, "w");
  if (!f) return;
  f.write(&count, 1);
  f.close();
}

// The compiled-in ESP-IDF app image header doesn't carry FIRMWARE_VERSION -- it's stamped with the
// prebuilt Arduino core's own build info instead -- so each slot's version is self-reported into
// SPIFFS the first time it actually boots, keyed by slot letter, and read back for the other slot.
static const char* versionPath(char letter) {
  return (letter == 'A') ? "/ota_ver_a" : "/ota_ver_b";
}

static void readVersion(char letter, char* out, size_t out_len) {
  File f = SPIFFS.open(versionPath(letter), "r");
  bool open_ok = (bool)f;   // captured before close() -- File::operator bool() goes false once closed
  if (open_ok) {
    int n = f.read((uint8_t*)out, out_len - 1);
    if (n < 0) n = 0;
    out[n] = 0;
    f.close();
  }
  if (!open_ok || out[0] == 0) strcpy(out, "v?");   // never booted -- nothing self-reported yet
}

static void writeVersion(char letter, const char* version) {
  File f = SPIFFS.open(versionPath(letter), "w");
  if (!f) return;
  f.write((const uint8_t*)version, strlen(version));
  f.close();
}

void RollbackGuard::begin(const char* version) {
  boot_time_ms = millis();
  confirmed_or_not_applicable = !isPendingVerify();
  writeFailCount(0);   // successful boot -- clear any prior failure streak
  strncpy(pending_version, version, sizeof(pending_version) - 1);
  pending_version[sizeof(pending_version) - 1] = 0;
  version_write_pending = true;   // actually written on the first poll() -- see comment above
}

void RollbackGuard::poll() {
  if (version_write_pending) {
    writeVersion(partitionLetter(esp_ota_get_running_partition()), pending_version);
    version_write_pending = false;
  }
  if (confirmed_or_not_applicable) return;
  if (millis() - boot_time_ms < OTA_ROLLBACK_CONFIRM_DELAY_MS) return;
  esp_ota_mark_app_valid_cancel_rollback();
  confirmed_or_not_applicable = true;
}

bool RollbackGuard::reportUnhealthy() {
  if (!isPendingVerify()) return false;
  return esp_ota_mark_app_invalid_rollback_and_reboot() == ESP_OK;   // doesn't return on success
}

void RollbackGuard::onRadioInitFailure() {
  reportUnhealthy();   // OTA-probation case: forces rollback + reboot, doesn't return on success

  // Not on probation -- transient or genuine radio failure unrelated to any update. Retry with cap
  // instead of hanging forever on the first attempt.
  uint8_t count = readFailCount();
  if (count < RADIO_INIT_RESET_CAP) {
    writeFailCount(count + 1);
    esp_restart();   // does not return
  }

  // Cap exhausted -- same terminal behavior as before this feature existed.
  while (1) ;
}

const char* RollbackGuard::status() {
  static char buf[96];
  char active_letter = partitionLetter(esp_ota_get_running_partition());

  const esp_partition_t* a = findPartitionByLetter('A');
  const esp_partition_t* b = findPartitionByLetter('B');
  esp_ota_img_states_t state_a = ESP_OTA_IMG_UNDEFINED, state_b = ESP_OTA_IMG_UNDEFINED;
  if (a) esp_ota_get_state_partition(a, &state_a);
  if (b) esp_ota_get_state_partition(b, &state_b);

  static char ver_a[24], ver_b[24];   // off status()'s own stack frame too, for the same reason
  readVersion('A', ver_a, sizeof(ver_a));
  readVersion('B', ver_b, sizeof(ver_b));

  sprintf(buf, "Slots: A=%s (%s%s) | B=%s (%s%s)",
          ver_a, active_letter == 'A' ? "active, " : "", stateLabel(state_a),
          ver_b, active_letter == 'B' ? "active, " : "", stateLabel(state_b));
  return buf;
}

bool RollbackGuard::setActivePartition(char letter, char reply[]) {
  if (letter >= 'a' && letter <= 'z') letter -= 32;   // uppercase, no <ctype.h> needed for one char
  if (letter != 'A' && letter != 'B') {
    strcpy(reply, "ERR: expected A or B");
    return false;
  }

  const esp_partition_t* running = esp_ota_get_running_partition();
  if (partitionLetter(running) == letter) {
    sprintf(reply, "ERR: %c already active", letter);
    return false;
  }

  const esp_partition_t* target = findPartitionByLetter(letter);
  if (!target) {
    sprintf(reply, "ERR: partition %c not found", letter);
    return false;
  }

  uint8_t magic = 0;
  if (esp_partition_read(target, 0, &magic, 1) != ESP_OK || magic != 0xE9) {   // ESP_IMAGE_HEADER_MAGIC
    sprintf(reply, "ERR: %c has no valid image", letter);
    return false;
  }

  if (esp_ota_set_boot_partition(target) != ESP_OK) {
    strcpy(reply, "ERR: could not set boot partition");
    return false;
  }

  strcpy(reply, "OK - rebooting");
  return true;   // caller reboots
}
