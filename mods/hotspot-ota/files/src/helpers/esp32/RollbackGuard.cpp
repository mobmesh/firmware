#include "RollbackGuard.h"
#include "RadioFailPolicy.h"
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_system.h>   // esp_restart()
#include <esp_image_format.h>   // esp_image_verify() -- bootloader_support, linked into the app
#include <SPIFFS.h>
#include <helpers/ModHooks.h>
#include "HotspotOTA.h"
#include "ProbationPolicy.h"

// Long enough to catch an early crash/boot-loop; short enough not to add much to the ~2 minutes a
// hotspot-fetch update can already take.
#define OTA_ROLLBACK_CONFIRM_DELAY_MS   PROBATION_CRASH_CHECK_MS
#define RX_POLL_IDLE_MS     60000UL
#define RX_POLL_WINDOW_MS   5000UL
#define HEARD_PATH          "/ota_heard"
#define RADIO_FAIL_PATH     "/ota_radio_fail"

// The counter lives in SPIFFS, not RTC memory, which is unreliable across esp_restart().
// Confirm retries are spaced because the call writes otadata.
#define CONFIRM_RETRY_MS   5000

#define RADIO_INIT_RESET_CAP   5
#define RADIO_FAIL_COUNT_PATH  "/radio_fail_count"
#ifndef OTA_RADIO_FAIL_SLEEP_START_SECS
#define OTA_RADIO_FAIL_SLEEP_START_SECS  60
#endif
#ifndef OTA_RADIO_FAIL_SLEEP_MAX_SECS
#define OTA_RADIO_FAIL_SLEEP_MAX_SECS    900
#endif

// Arduino confirms inside initArduino(), before setup() and too early to judge; this defers to
// RollbackGuard. extern "C" because the weak symbol it replaces is declared in a .c file.
extern "C" bool verifyRollbackLater() { return true; }

static uint32_t boot_time_ms = 0;
static bool confirmed_or_not_applicable = false;
static uint32_t last_confirm_attempt_ms = 0;

// Defer the SPIFFS write to poll() to avoid adding stack depth inside MeshCore's init chain.
static char pending_version[24] = {0};
static bool version_write_pending = false;
static bool confirm_reported = false;

// Mesh check: decided once the crash check passes, then held until the counters rise or the window ends.
static bool heard_before = false;
static bool known_good = false;
static bool mesh_decided = false;
static bool mesh_required = false;
static uint32_t window_start_ms = 0;
static uint8_t adverts_sent = 0;

// Recency of valid packets, for the record an install leaves for the next image.
static uint32_t rx_total = 0;
static bool have_rise = false;
static uint32_t last_rise_ms = 0;
static uint32_t last_rx_poll_ms = 0;
static bool rx_polled = false;

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
    uint32_t now = millis();
    uint32_t end_ms;
    if (mesh_decided && mesh_required) {
      end_ms = window_start_ms + PROBATION_WINDOW_MS;
    } else {
      end_ms = boot_time_ms + OTA_ROLLBACK_CONFIRM_DELAY_MS;
      if (!mesh_decided && heard_before && !known_good) end_ms += PROBATION_WINDOW_MS;
    }
    int32_t left = (int32_t)(end_ms - now);
    out.remaining_secs = left <= 0 ? 0 : ((uint32_t)left + 999) / 1000;
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

// Read back, because an unmounted or full volume can accept the open and still lose the byte.
static bool writeFailCount(uint8_t count) {
  File f = SPIFFS.open(RADIO_FAIL_COUNT_PATH, "w");
  if (!f) return false;
  size_t written = f.write(&count, 1);
  f.close();
  return written == 1 && readFailCount() == count;
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

static bool otherSlotRejected() {
  const esp_partition_t* other = esp_ota_get_next_update_partition(NULL);
  esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
  if (other == NULL || esp_ota_get_state_partition(other, &state) != ESP_OK) return false;
  return state == ESP_OTA_IMG_INVALID || state == ESP_OTA_IMG_ABORTED;
}

static void removeIfPresent(const char* path) {
  if (SPIFFS.exists(path)) SPIFFS.remove(path);
}

static bool readFlag(const char* path) {
  if (!SPIFFS.exists(path)) return false;
  File f = SPIFFS.open(path, "r");
  if (!f) return false;
  uint8_t value = 0;
  bool ok = f.read(&value, 1) == 1;
  f.close();
  return ok && value == 1;
}

static void writeFlag(const char* path, bool value) {
  File f = SPIFFS.open(path, "w");
  if (!f) return;
  uint8_t byte = value ? 1 : 0;
  f.write(&byte, 1);
  f.close();
}

// The reset that ended the previous boot; a panic, a watchdog or a recorded radio failure is a crash.
static bool lastBootCrashed() {
  esp_reset_reason_t reason = esp_reset_reason();
  bool crash = reason == ESP_RST_PANIC || reason == ESP_RST_INT_WDT || reason == ESP_RST_TASK_WDT
               || reason == ESP_RST_WDT || readFlag(RADIO_FAIL_PATH);
  removeIfPresent(RADIO_FAIL_PATH);
  return crash;
}

static void pollRx(uint32_t interval_ms) {
  if (rx_polled && millis() - last_rx_poll_ms < interval_ms) return;
  rx_polled = true;
  last_rx_poll_ms = millis();
  char command[] = "stats-packets";
  char reply[200] = {0};
  modCliDispatch(0, command, reply);
  uint32_t total = 0;
  if (!probationParseRx(reply, total)) return;
  if (total > rx_total) {
    have_rise = true;
    last_rise_ms = millis();
  } else if (!probationHeardRecently(have_rise, last_rise_ms, millis())) {
    have_rise = false;   // expired here, so a millis() wrap cannot make an old rise look recent
  }
  rx_total = total;
}

static void sendAdvert() {
  char command[] = "advert";
  char reply[200] = {0};
  modCliDispatch(0, command, reply);
}

static void confirmNow() {
  if (esp_ota_mark_app_valid_cancel_rollback() == ESP_OK) confirmed_or_not_applicable = true;
}

void RollbackGuard::poll() {
  if (version_write_pending) {
    writeVersion(partitionLetter(esp_ota_get_running_partition()), pending_version);
    version_write_pending = false;
    if (otherSlotRejected()) HotspotOTA::onImageRejected(lastBootCrashed());
    else removeIfPresent(RADIO_FAIL_PATH);
    heard_before = readFlag(HEARD_PATH);
    removeIfPresent(HEARD_PATH);
    known_good = !confirmed_or_not_applicable && HotspotOTA::runningIsKnownGood();
  }
  bool listening = !confirmed_or_not_applicable && mesh_decided && mesh_required;
  pollRx(listening ? RX_POLL_WINDOW_MS : RX_POLL_IDLE_MS);
  if (confirmed_or_not_applicable) {
    if (!confirm_reported) {
      confirm_reported = true;
      HotspotOTA::onImageConfirmed();
    }
    return;
  }
  if (millis() - boot_time_ms < OTA_ROLLBACK_CONFIRM_DELAY_MS) return;
  if (!mesh_decided) {
    mesh_decided = true;
    // Read once, before any advert: a transmit dips the reading, and a low battery may end in a deep sleep.
    bool battery_ok = modLoadAllowed();
    mesh_required = probationNeedsMeshCheck(true, heard_before, known_good, battery_ok);
    window_start_ms = millis();
    Serial.printf("probation: mesh check %s (heard %d, known-good %d, battery %d, rx %u)\n",
                  mesh_required ? "on" : "off", heard_before, known_good, battery_ok, (unsigned)rx_total);
  }
  if (mesh_required) {
    if (rx_total > 0) {
      Serial.println("probation: mesh heard");
      mesh_required = false;
    } else {
      uint32_t elapsed = millis() - window_start_ms;
      if (probationWindowOver(elapsed)) {
        Serial.println("probation: mesh silent, rolling back");
        esp_ota_mark_app_invalid_rollback_and_reboot();   // does not return on success
        return;
      }
      if (probationAdvertDue(elapsed, adverts_sent)) {
        adverts_sent++;
        Serial.printf("probation: advert %u\n", (unsigned)adverts_sent);
        sendAdvert();
      }
      return;
    }
  }
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
  if (isPendingVerify()) writeFlag(RADIO_FAIL_PATH, true);
  reportUnhealthy();   // OTA-probation case: forces rollback + reboot, doesn't return on success

  // Not on probation -- transient or genuine radio failure unrelated to any update. Retry with cap
  // instead of hanging forever on the first attempt.
  uint8_t count = readFailCount();
  bool durable = writeFailCount(count < 255 ? count + 1 : count);
  RadioFailAction action = radioFailAction(count, durable, RADIO_INIT_RESET_CAP,
                                           OTA_RADIO_FAIL_SLEEP_START_SECS,
                                           OTA_RADIO_FAIL_SLEEP_MAX_SECS);
  if (action.restart) {
    esp_restart();   // does not return
  }

  // Cap exhausted, or the count could not be kept -- one retry per wake, until begin() clears it.
  if (durable) {
    Serial.printf("radio init failed %u times -- deep sleep %us\n", (unsigned)count + 1,
                  (unsigned)action.sleep_secs);
  } else {
    Serial.printf("radio init failed; fail count not saved -- deep sleep %us\n",
                  (unsigned)action.sleep_secs);
  }
  Serial.flush();
  modBeforeDeepSleep();
  modBoardDeepSleep(action.sleep_secs);   // does not return
}

RollbackGuard::Slots RollbackGuard::slots() {
  Slots out;
  out.active = partitionLetter(esp_ota_get_running_partition());
  out.target = (out.active == 'A') ? 'B' : 'A';
  readVersion(out.active, out.active_version, sizeof(out.active_version));
  readVersion(out.target, out.target_version, sizeof(out.target_version));

  esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
  const esp_partition_t* active = findPartitionByLetter(out.active);
  if (active) esp_ota_get_state_partition(active, &state);
  out.active_state = stateLabel(state);

  const esp_partition_t* target = findPartitionByLetter(out.target);
  out.target_size = target ? target->size : 0;
  return out;
}

// Running slot: its state, or which probation stage it is in.
static void activeLabel(esp_ota_img_states_t state, char* out, size_t len) {
  RollbackGuard::ProbationState probation = RollbackGuard::probation();
  if (!probation.pending) {
    snprintf(out, len, "%s", stateLabel(state));
  } else if (mesh_decided && mesh_required) {
    snprintf(out, len, "pending: mesh %u/%u %um", (unsigned)adverts_sent, (unsigned)PROBATION_ADVERTS,
             (unsigned)((probation.remaining_secs + 59) / 60));
  } else {
    uint32_t elapsed = millis() - boot_time_ms;
    uint32_t left = elapsed >= OTA_ROLLBACK_CONFIRM_DELAY_MS ? 0 : (OTA_ROLLBACK_CONFIRM_DELAY_MS - elapsed + 999) / 1000;
    snprintf(out, len, "pending: boot %us", (unsigned)left);
  }
}

// Other slot: its recorded state, or dirty when that says valid but the image no longer verifies.
static const char* standbyLabel(esp_ota_img_states_t state, const char* image) {
  if (state == ESP_OTA_IMG_VALID && strcmp(image, "image-invalid") == 0) return "dirty";
  return stateLabel(state);
}

const char* RollbackGuard::status(bool verify_inactive) {
  static char buf[160];
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
  char active[40];
  activeLabel(b_inactive ? state_a : state_b, active, sizeof(active));

  if (b_inactive) {
    snprintf(buf, sizeof(buf), "Slots: A=%s (active, %s) | B=%s (standby, %s, %s)",
             ver_a, active, ver_b, standbyLabel(state_b, image), image);
  } else {
    snprintf(buf, sizeof(buf), "Slots: A=%s (standby, %s, %s) | B=%s (active, %s)",
             ver_a, standbyLabel(state_a, image), image, ver_b, active);
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

bool RollbackGuard::heardRecently() {
  return probationHeardRecently(have_rise, last_rise_ms, millis());
}

void RollbackGuard::noteInstall() {
  writeFlag(HEARD_PATH, RollbackGuard::heardRecently());
}

bool RollbackGuard::confirm() {
  if (confirmed_or_not_applicable || !isPendingVerify()) return false;
  confirmNow();
  return confirmed_or_not_applicable;
}
