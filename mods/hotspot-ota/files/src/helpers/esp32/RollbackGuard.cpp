#include "RollbackGuard.h"
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_system.h>   // esp_restart()
#include <esp_image_format.h>   // esp_image_verify() -- bootloader_support, linked into the app
#include <SPIFFS.h>

// Long enough to catch an early crash/boot-loop; short enough not to add much to the ~2 minutes a
// hotspot-fetch update can already take.
#define OTA_ROLLBACK_CONFIRM_DELAY_MS   90000

// Cross-boot retry cap for onRadioInitFailure(). The counter lives in SPIFFS, not RTC memory,
// which is unreliable across esp_restart() on this chip/IDF combination.
// Spacing between confirm retries: the call writes otadata, so it must not run every loop().
#define CONFIRM_RETRY_MS   5000

#define RADIO_INIT_RESET_CAP   5
#define RADIO_FAIL_COUNT_PATH  "/radio_fail_count"

// Arduino confirms inside initArduino(), before setup() and too early to judge; this defers to
// RollbackGuard. extern "C" because the weak symbol it replaces is declared in a .c file.
extern "C" bool verifyRollbackLater() { return true; }

static uint32_t boot_time_ms = 0;
static bool confirmed_or_not_applicable = false;
static uint32_t last_confirm_attempt_ms = 0;

// begin() runs deep in MeshCore's init chain, where a SPIFFS write added enough stack depth to
// reproduce the handleGetCmd() boot instability (987639c). Deferred to poll(), called shallower.
static char pending_version[24] = {0};
static bool version_write_pending = false;

static bool isPendingVerify(esp_ota_img_states_t* out_state = nullptr) {
  esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
  const esp_partition_t* running = esp_ota_get_running_partition();
  esp_ota_get_state_partition(running, &state);
  if (out_state) *out_state = state;
  return state == ESP_OTA_IMG_PENDING_VERIFY;
}

RollbackGuard::ProbationState RollbackGuard::probation() {
  ProbationState out = {false, false, 0};
  const esp_partition_t* running = esp_ota_get_running_partition();
  if (running == NULL) return out;   // known stays false -- caller refuses
  esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
  if (esp_ota_get_state_partition(running, &state) != ESP_OK) return out;

  out.known = true;
  // Deliberately not consulting confirmed_or_not_applicable: a successful confirm writes otadata
  // synchronously, so the RAM flag could only ever differ from it after a *failed* confirm.
  out.pending = (state == ESP_OTA_IMG_PENDING_VERIFY);
  if (out.pending) {
    uint32_t elapsed = millis() - boot_time_ms;
    out.remaining_secs = (elapsed >= OTA_ROLLBACK_CONFIRM_DELAY_MS)
                         ? 0 : (OTA_ROLLBACK_CONFIRM_DELAY_MS - elapsed + 999) / 1000;
  }
  return out;
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

// A cancelled download leaves a stale recorded state over a truncated image, so otadata cannot
// answer "could this boot?". esp_image_verify() checks header, checksum and appended SHA-256.
static const char* imageLabel(const esp_partition_t* p) {
  if (!p) return "image-absent";
  esp_partition_pos_t pos = {};
  pos.offset = p->address;
  pos.size = p->size;
  esp_image_metadata_t meta = {};
  // SILENT: an invalid inactive slot is a condition this command reports, not a fault worth
  // printing a bootloader error over on every get ota.slot.
  return (esp_image_verify(ESP_IMAGE_VERIFY_SILENT, &pos, &meta) == ESP_OK) ? "image-ok" : "image-invalid";
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

// The app image header carries the prebuilt Arduino core's build info, not FIRMWARE_VERSION, so
// each slot self-reports its version into SPIFFS on first boot, keyed by slot letter.
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
  // Retried rather than latched blindly: a failed confirm leaves otadata PENDING_VERIFY, and
  // claiming success would hand the next OTA the only known-good image. Paced off the loop.
  if (last_confirm_attempt_ms != 0 && millis() - last_confirm_attempt_ms < CONFIRM_RETRY_MS) return;
  last_confirm_attempt_ms = millis();
  if (esp_ota_mark_app_valid_cancel_rollback() == ESP_OK) confirmed_or_not_applicable = true;
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

const char* RollbackGuard::status(bool verify_inactive) {
  static char buf[128];
  char active_letter = partitionLetter(esp_ota_get_running_partition());

  const esp_partition_t* a = findPartitionByLetter('A');
  const esp_partition_t* b = findPartitionByLetter('B');
  esp_ota_img_states_t state_a = ESP_OTA_IMG_UNDEFINED, state_b = ESP_OTA_IMG_UNDEFINED;
  if (a) esp_ota_get_state_partition(a, &state_a);
  if (b) esp_ota_get_state_partition(b, &state_b);

  static char ver_a[24], ver_b[24];   // off status()'s own stack frame too, for the same reason
  readVersion('A', ver_a, sizeof(ver_a));
  readVersion('B', ver_b, sizeof(ver_b));

  // Only the inactive slot is verified: the active one is running, so it is bootable by proof,
  // and esp_image_verify() hashes the whole image.
  bool b_inactive = (active_letter != 'B');
  const char* image = !verify_inactive ? "image-unchecked"
                                       : imageLabel(b_inactive ? b : a);

  if (b_inactive) {
    snprintf(buf, sizeof(buf), "Slots: A=%s (active, %s) | B=%s (recorded-%s, %s)",
             ver_a, stateLabel(state_a), ver_b, stateLabel(state_b), image);
  } else {
    snprintf(buf, sizeof(buf), "Slots: A=%s (recorded-%s, %s) | B=%s (active, %s)",
             ver_a, stateLabel(state_a), image, ver_b, stateLabel(state_b));
  }
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
