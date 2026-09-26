#include "HotspotOTA.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <Update.h>
#include <SPIFFS.h>
#include <mbedtls/sha256.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_sntp.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <time.h>
#include <helpers/TxtDataHelpers.h>   // StrHelper lives here, no standalone StrHelper.h
#include <helpers/ModHooks.h>          // modClockSet()
#include "RollbackGuard.h"             // probation state -- see start()

// The OTA_WIFI_*, OTA_HTTP_*, OTA_WAN_* and PIN_HOTSPOT_PWR values are injected as build flags
// from variants/<board>/overrides.yaml, which carries each board's values and rationale.
#define OTA_WAN_CHECK_HOST       "google.com"   // 3rd-party host, distinct from the firmware host

// Neither target board has a battery-backed RTC, so the clock is always wrong after a reboot.
// Piggybacked on WiFi join to reuse the radio-on window OTA already paid for.
#define OTA_NTP_SERVER            "us.pool.ntp.org"   // regional zone -- lower latency on a blocking call
#define OTA_NTP_SERVER_FALLBACK   "pool.ntp.org"      // only tried if the regional zone doesn't answer
#define OTA_NTP_SYNC_TIMEOUT_MS   5000
#define OTA_NTP_SANITY_FLOOR      1700000000   // ~Nov 2023 -- rules out an unset/failed sync
#define OTA_WAN_VERIFY_ATTEMPTS   2
#define OTA_NTP_VERIFY_ATTEMPTS   2
#define OTA_WAN_RESTORE_ATTEMPTS  2

// CI scans the built image for this, so the mod's bit is evidence it compiled in rather than a
// claim from build config. Keep it referenced: --gc-sections drops an unreferenced string.
static const char OTA_MOD_MARKER[] = "H0TSP0T";   // must never change

// 80 bytes CI writes into esp_app_desc_t's reserved tail -- patch_ota_metadata.py. Fixed
// offset, so no scan window and no sidecar carrying where the marker happened to land.
#define OTA_META_OFFSET        208
#define OTA_META_LEN           80
#define OTA_META_MIN_BYTES     (OTA_META_OFFSET + OTA_META_LEN)   // decided in the first packet
#define OTA_IMAGE_DIGEST_LEN   32

static const char OTA_META_MAGIC[] = "MOBMESH";   // must never change -- 7 chars + its NUL
#define OTA_META_MAGIC_LEN   sizeof(OTA_META_MAGIC)   // includes the NUL: the field is 8 wide

// Numbering is permanent -- mods/bit-registry.md.
#define MOD_BIT_HOTSPOT_OTA   (1u << 1)

struct OtaMetadata {
  char upstream_version[17];
  char repo_sha[13];
  char board_role[25];
  uint32_t mods;
};

// Layout version 0x01 only: a newer layout may have moved the fields below.
static bool parseMetadata(const uint8_t* block, OtaMetadata& out) {
  if (memcmp(block, OTA_META_MAGIC, OTA_META_MAGIC_LEN) != 0) return false;
  if (block[8] != 0x01) return false;
  memcpy(out.upstream_version, block + 12, 16);  out.upstream_version[16] = 0;
  memcpy(out.repo_sha, block + 28, 12);          out.repo_sha[12] = 0;
  memcpy(out.board_role, block + 40, 24);        out.board_role[24] = 0;
  memcpy(&out.mods, block + 64, sizeof(out.mods));   // u32 LE, same byte order both ends
  return true;
}

// The same block from the partition this node booted, to compare against an incoming image.
static bool runningMetadata(OtaMetadata& out) {
  const esp_partition_t* running = esp_ota_get_running_partition();
  if (running == NULL) return false;
  uint8_t block[OTA_META_LEN];
  if (esp_partition_read(running, OTA_META_OFFSET, block, sizeof(block)) != ESP_OK) return false;
  return parseMetadata(block, out);
}

bool HotspotOTA::runningMetadata(char* version, char* sha, char* role) {
  OtaMetadata meta;
  if (!runningMetadata(meta)) return false;
  strcpy(version, meta.upstream_version);
  strcpy(sha, meta.repo_sha);
  strcpy(role, meta.board_role);
  return true;
}

static bool marker_bypass = false;   // RAM-only, one-time

enum class OtaServiceState : uint8_t {
  Idle,
  Queued,
  PoweringOn,
  Joining,
  CheckingWan,
  Opening,
  Downloading,
  Verifying,
  Committing,
  Succeeded,
  Failed,
  Canceled,
  WanComplete
};

enum class ServiceKind : uint8_t { None, Ota, WanVerify };
enum class WanResult : uint8_t { Unknown, WanFailed, NtpFailed, Verified, RestoreFault };

static const char* WAN_PATHS[] = {"/ota_wan0", "/ota_wan1"};
static const uint8_t WAN_RECORD_VERSION = 1;

struct __attribute__((packed)) WanRecord {
  char magic[4];
  uint8_t version;
  uint8_t result;
  uint8_t proven;
  uint8_t reserved;
  uint32_t sequence;
  uint8_t credentials[32];
  uint8_t digest[32];
};

struct WanRuntimeState {
  wifi_mode_t wifi_mode;
  bool power;
  bool connected;
  char ssid[32];
};

static_assert(sizeof(WanRecord) == 76, "WAN record layout changed");

static portMUX_TYPE service_mux = portMUX_INITIALIZER_UNLOCKED;
static OtaServiceState service_state = OtaServiceState::Idle;
static HotspotOtaConfig service_config;
static char service_result[MAX_TEXT_LEN] = {0};
static size_t service_written = 0;
static int service_total = -1;
static bool service_bypass_marker = false;
static bool service_cancel_requested = false;
static bool service_sleep_inhibited = false;
static uint32_t service_clock_epoch = 0;
static ServiceKind service_kind = ServiceKind::None;
static uint32_t service_run = 0;
static uint32_t wan_run_seq = 0;
static uint32_t wan_done_run = 0;
static WanResult service_wan_result = WanResult::Unknown;
static WanResult wan_done_result = WanResult::Unknown;
static WanRuntimeState service_wan_initial;

static void sha256(const uint8_t* data, size_t len, uint8_t out[32]) {
  mbedtls_sha256_context ctx;
  mbedtls_sha256_init(&ctx);
  mbedtls_sha256_starts(&ctx, 0);
  mbedtls_sha256_update(&ctx, data, len);
  mbedtls_sha256_finish(&ctx, out);
  mbedtls_sha256_free(&ctx);
}

static void credentialDigest(const HotspotOtaConfig& cfg, uint8_t out[32]) {
  mbedtls_sha256_context ctx;
  mbedtls_sha256_init(&ctx);
  mbedtls_sha256_starts(&ctx, 0);
  mbedtls_sha256_update(&ctx, (const uint8_t*)cfg.ssid, strnlen(cfg.ssid, sizeof(cfg.ssid)));
  const uint8_t separator = 0;
  mbedtls_sha256_update(&ctx, &separator, 1);
  mbedtls_sha256_update(&ctx, (const uint8_t*)cfg.password,
                        strnlen(cfg.password, sizeof(cfg.password)));
  mbedtls_sha256_finish(&ctx, out);
  mbedtls_sha256_free(&ctx);
}

static bool wanRecordValid(const WanRecord& record) {
  if (memcmp(record.magic, "OWAN", 4) != 0 || record.version != WAN_RECORD_VERSION) return false;
  if (record.result > (uint8_t)WanResult::RestoreFault || record.proven > 1
      || record.reserved != 0) return false;
  uint8_t digest[32];
  sha256((const uint8_t*)&record, offsetof(WanRecord, digest), digest);
  return memcmp(digest, record.digest, sizeof(digest)) == 0;
}

static bool readWanSlot(uint8_t slot, WanRecord& record) {
  if (!SPIFFS.exists(WAN_PATHS[slot])) return false;
  File file = SPIFFS.open(WAN_PATHS[slot], "r");
  if (!file) return false;
  size_t read = file.read((uint8_t*)&record, sizeof(record));
  bool exact = read == sizeof(record) && !file.available();
  file.close();
  return exact && wanRecordValid(record);
}

static bool newerSequence(uint32_t left, uint32_t right) {
  return (int32_t)(left - right) > 0;
}

static bool loadWanRecord(WanRecord& record, int* selected = NULL) {
  WanRecord slots[2];
  bool valid0 = readWanSlot(0, slots[0]);
  bool valid1 = readWanSlot(1, slots[1]);
  if (!valid0 && !valid1) return false;
  int slot = !valid0 ? 1 : (!valid1 || newerSequence(slots[0].sequence, slots[1].sequence) ? 0 : 1);
  record = slots[slot];
  if (selected != NULL) *selected = slot;
  return true;
}

static WanRecord newWanRecord() {
  WanRecord record = {};
  memcpy(record.magic, "OWAN", 4);
  record.version = WAN_RECORD_VERSION;
  record.result = (uint8_t)WanResult::Unknown;
  return record;
}

static bool saveWanRecord(WanRecord record) {
  WanRecord current;
  int current_slot = -1;
  if (loadWanRecord(current, &current_slot)) record.sequence = current.sequence + 1;
  else record.sequence = 1;
  sha256((const uint8_t*)&record, offsetof(WanRecord, digest), record.digest);
  uint8_t target = current_slot < 0 ? 0 : 1 - current_slot;
  File file = SPIFFS.open(WAN_PATHS[target], "w");
  if (!file) return false;
  size_t written = file.write((const uint8_t*)&record, sizeof(record));
  file.close();
  WanRecord check;
  return written == sizeof(record) && readWanSlot(target, check)
         && check.sequence == record.sequence;
}

static bool recordMatches(const WanRecord& record, const HotspotOtaConfig& cfg) {
  uint8_t digest[32];
  credentialDigest(cfg, digest);
  return memcmp(digest, record.credentials, sizeof(digest)) == 0;
}

static bool serviceIsActive(OtaServiceState state) {
  return state >= OtaServiceState::Queued && state <= OtaServiceState::Committing;
}

static const char* serviceStateName(OtaServiceState state) {
  switch (state) {
    case OtaServiceState::Idle: return "idle";
    case OtaServiceState::Queued: return "queued";
    case OtaServiceState::PoweringOn: return "powering-on";
    case OtaServiceState::Joining: return "joining";
    case OtaServiceState::CheckingWan: return "checking-wan";
    case OtaServiceState::Opening: return "opening";
    case OtaServiceState::Downloading: return "downloading";
    case OtaServiceState::Verifying: return "verifying";
    case OtaServiceState::Committing: return "committing";
    case OtaServiceState::Succeeded: return "succeeded";
    case OtaServiceState::Failed: return "failed";
    case OtaServiceState::Canceled: return "canceled";
    case OtaServiceState::WanComplete: return "wan-complete";
  }
  return "unknown";
}

static void setServiceState(OtaServiceState state) {
  portENTER_CRITICAL(&service_mux);
  service_state = state;
  portEXIT_CRITICAL(&service_mux);
}

static void setServiceProgress(size_t written, int total) {
  portENTER_CRITICAL(&service_mux);
  service_written = written;
  service_total = total;
  portEXIT_CRITICAL(&service_mux);
}

static bool serviceCancelRequested() {
  portENTER_CRITICAL(&service_mux);
  bool requested = service_cancel_requested;
  portEXIT_CRITICAL(&service_mux);
  return requested;
}

static bool advanceServiceState(OtaServiceState state) {
  portENTER_CRITICAL(&service_mux);
  bool canceled = service_cancel_requested;
  if (!canceled) service_state = state;
  portEXIT_CRITICAL(&service_mux);
  return !canceled;
}

void HotspotOTA::setMarkerBypass(bool on) {
  marker_bypass = on;
}

// Collects just enough of the stream to read the metadata block, then stops caring.
struct HeaderInspector {
  uint8_t head[OTA_META_MIN_BYTES];
  size_t have = 0;

  void feed(const uint8_t* data, size_t n) {
    if (have >= sizeof(head)) return;
    size_t take = min(n, sizeof(head) - have);
    memcpy(head + have, data, take);
    have += take;
  }
  bool complete() const { return have >= sizeof(head); }
  bool read(OtaMetadata& out) const {
    return complete() && parseMetadata(head + OTA_META_OFFSET, out);
  }
};

// The image's own SHA-256 is its last 32 bytes, so hash with a 32-byte delay: at EOF the
// held bytes are the expected digest. Padding and the checksum byte are hashed like the rest.
struct TrailingDigest {
  mbedtls_sha256_context ctx;
  uint8_t hold[OTA_IMAGE_DIGEST_LEN];
  size_t held = 0;

  void begin() { mbedtls_sha256_init(&ctx); mbedtls_sha256_starts(&ctx, 0); }
  void release() { mbedtls_sha256_free(&ctx); }

  void feed(const uint8_t* data, size_t n) {
    size_t total = held + n;
    if (total <= sizeof(hold)) {           // still short of a full tail -- hash nothing yet
      memcpy(hold + held, data, n);
      held = total;
      return;
    }
    size_t flush = total - sizeof(hold);   // everything older than the last 32 bytes
    size_t from_hold = min(flush, held);
    if (from_hold > 0) {
      mbedtls_sha256_update(&ctx, hold, from_hold);
      memmove(hold, hold + from_hold, held - from_hold);
      held -= from_hold;
    }
    size_t from_data = flush - from_hold;
    if (from_data > 0) mbedtls_sha256_update(&ctx, data, from_data);
    memcpy(hold + held, data + from_data, n - from_data);
    held += n - from_data;
  }

  bool matches() {
    if (held != sizeof(hold)) return false;
    uint8_t digest[OTA_IMAGE_DIGEST_LEN];
    mbedtls_sha256_finish(&ctx, digest);
    return memcmp(digest, hold, sizeof(hold)) == 0;
  }
};

// Best-effort, never gates the caller; offsets are 0 because RTCClock's epoch is UTC. Bypasses
// the "time" command's cannot-go-backwards guard -- with no RTC there is nothing to go back from.
static bool syncNtpTime() {
  if (esp_sntp_enabled()) esp_sntp_stop();
  esp_sntp_set_sync_status(SNTP_SYNC_STATUS_RESET);
  configTime(0, 0, OTA_NTP_SERVER, OTA_NTP_SERVER_FALLBACK);
  uint32_t start = millis();
  bool synced = false;
  while (millis() - start < OTA_NTP_SYNC_TIMEOUT_MS) {
    if (esp_sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
      synced = true;
      break;
    }
    delay(100);
  }
  time_t now;
  time(&now);
  if (synced && now > OTA_NTP_SANITY_FLOOR) {
    portENTER_CRITICAL(&service_mux);
    service_clock_epoch = (uint32_t)now;
    portEXIT_CRITICAL(&service_mux);
    return true;
  }
  return false;
}

// The service is unattended and can be patient; wifiConnect() runs inline and must fail fast.
static bool joinWifiStation(const char* ssid, const char* pwd, char reply[], int max_attempts,
                            bool cancellable, bool sync_clock) {
  WiFi.mode(WIFI_STA);
  for (int attempt = 0; attempt < max_attempts; attempt++) {
    WiFi.begin(ssid, pwd);
    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < OTA_WIFI_JOIN_ATTEMPT_TIMEOUT_MS) {
      if (cancellable && serviceCancelRequested()) {
        strcpy(reply, "ERR: canceled");
        WiFi.disconnect(true);
        return false;
      }
      delay(250);
    }
    if (WiFi.status() == WL_CONNECTED) {
      if (sync_clock) syncNtpTime();
      return true;
    }
    WiFi.disconnect(true);
    if (attempt < max_attempts - 1) {
      uint32_t retry_start = millis();
      while (millis() - retry_start < OTA_WIFI_JOIN_RETRY_DELAY_MS) {
        if (cancellable && serviceCancelRequested()) {
          strcpy(reply, "ERR: canceled");
          return false;
        }
        delay(100);
      }
    }
  }
  strcpy(reply, "ERR: could not join hotspot WiFi");
  return false;
}

// Checks the actual connected SSID, not just WL_CONNECTED -- a stale connection from an earlier
// `ota wan join` against different credentials must not be silently reused.
static bool alreadyConnectedTo(const HotspotOtaConfig& cfg) {
  return WiFi.status() == WL_CONNECTED && WiFi.SSID() == cfg.ssid;
}

// Advisory only; used to give a more specific error if a later step fails too.
static bool checkWanConnectivity(int max_attempts = OTA_WAN_CHECK_ATTEMPTS,
                                 bool cancellable = false) {
  IPAddress ip;
  for (int attempt = 0; attempt < max_attempts; attempt++) {
    if (cancellable && serviceCancelRequested()) return false;
    if (WiFi.hostByName(OTA_WAN_CHECK_HOST, ip)) return true;
    if (attempt < max_attempts - 1) {
      uint32_t retry_start = millis();
      while (millis() - retry_start < OTA_WAN_CHECK_RETRY_DELAY_MS) {
        if (cancellable && serviceCancelRequested()) return false;
        delay(100);
      }
    }
  }
  return false;
}

// Drops the transfer rather than draining it: this fires ~288 bytes in with the whole image
// outstanding, and draining costs the bandwidth the early check exists to save.
static void closeNow(HTTPClient& http) {
  WiFiClient* stream = http.getStreamPtr();
  if (stream != NULL) stream->stop();
  http.end();
}

// Drains then closes, BEFORE Update.abort() at every abort site -- hardware testing showed the
// order matters. Bounded by OTA_HTTP_TIMEOUT_MS, so a server that stops sending cannot hang it.
static void drainAndClose(HTTPClient& http) {
  WiFiClient* stream = http.getStreamPtr();
  uint8_t discard[512];
  uint32_t start = millis();
  while (http.connected() && millis() - start < OTA_HTTP_TIMEOUT_MS) {
    size_t avail = stream->available();
    if (!avail) { delay(1); continue; }
    int n = stream->readBytes(discard, min(avail, sizeof(discard)));
    if (n <= 0) break;
  }
  http.end();
}

static bool runService(const HotspotOtaConfig& cfg, bool bypass_marker_check, char reply[]) {
  if (cfg.ssid[0] == 0 || cfg.url[0] == 0) {
    strcpy(reply, "ERR: ota.wan.wifi not configured");
    return false;
  }

  setServiceState(OtaServiceState::PoweringOn);
  pinMode(PIN_HOTSPOT_PWR, OUTPUT);
  digitalWrite(PIN_HOTSPOT_PWR, HIGH);   // must stay HIGH throughout -- load switch, not a latch

  if (!alreadyConnectedTo(cfg)) {
    setServiceState(OtaServiceState::Joining);
    if (!joinWifiStation(cfg.ssid, cfg.password, reply, OTA_WIFI_JOIN_MAX_ATTEMPTS, true, true)) {
      digitalWrite(PIN_HOTSPOT_PWR, LOW);
      return false;
    }
  }

  setServiceState(OtaServiceState::CheckingWan);
  bool wan_ok = checkWanConnectivity(OTA_WAN_CHECK_ATTEMPTS, true);
  if (serviceCancelRequested()) {
    strcpy(reply, "ERR: canceled");
    digitalWrite(PIN_HOTSPOT_PWR, LOW);
    WiFi.disconnect(true);
    return false;
  }

  setServiceState(OtaServiceState::Opening);
  HTTPClient http;
  http.setTimeout(OTA_HTTP_TIMEOUT_MS);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);   // GitHub release URLs 302 to a presigned link
  if (!http.begin(cfg.url)) {
    strcpy(reply, "ERR: could not open URL");
    digitalWrite(PIN_HOTSPOT_PWR, LOW);
    WiFi.disconnect(true);
    return false;
  }

  int code = http.GET();
  if (serviceCancelRequested()) {
    strcpy(reply, "ERR: canceled");
    http.end();
    digitalWrite(PIN_HOTSPOT_PWR, LOW);
    WiFi.disconnect(true);
    return false;
  }
  if (code != HTTP_CODE_OK) {
    if (!wan_ok) {
      strcpy(reply, "ERR: WiFi joined but no WAN connectivity");
    } else {
      sprintf(reply, "ERR: HTTP GET failed (%d)", code);
    }
    http.end();
    digitalWrite(PIN_HOTSPOT_PWR, LOW);
    WiFi.disconnect(true);
    return false;
  }

  int len = http.getSize();   // -1 if no Content-Length
  if (!Update.begin(len > 0 ? (size_t)len : UPDATE_SIZE_UNKNOWN, U_FLASH)) {
    strcpy(reply, "ERR: Update.begin failed");
    http.end();
    digitalWrite(PIN_HOTSPOT_PWR, LOW);
    WiFi.disconnect(true);
    return false;
  }

  TrailingDigest trailing;
  trailing.begin();
  HeaderInspector header;
  bool header_judged = false;

  setServiceProgress(0, len);
  setServiceState(OtaServiceState::Downloading);
  WiFiClient* stream = http.getStreamPtr();
  uint8_t buf[1024];
  int written = 0;
  uint32_t last_progress = millis();
  while (http.connected() && (len < 0 || written < len)) {
    if (serviceCancelRequested()) {
      strcpy(reply, "ERR: canceled");
      closeNow(http);
      Update.abort();
      trailing.release();
      digitalWrite(PIN_HOTSPOT_PWR, LOW);
      WiFi.disconnect(true);
      return false;
    }
    size_t avail = stream->available();
    if (!avail) {
      if (millis() - last_progress >= OTA_HTTP_TIMEOUT_MS) {
        strcpy(reply, "ERR: download stalled");
        closeNow(http);
        Update.abort();
        trailing.release();
        digitalWrite(PIN_HOTSPOT_PWR, LOW);
        WiFi.disconnect(true);
        return false;
      }
      delay(1);
      continue;
    }
    int n = stream->readBytes(buf, min(avail, sizeof(buf)));
    if (n <= 0) break;
    last_progress = millis();
    if (Update.write(buf, n) != (size_t)n) {
      strcpy(reply, "ERR: flash write failed");
      drainAndClose(http);   // network torn down before touching Update state -- see drainAndClose()
      Update.abort();
      trailing.release();
      digitalWrite(PIN_HOTSPOT_PWR, LOW);
      WiFi.disconnect(true);
      return false;
    }
    trailing.feed(buf, n);
    header.feed(buf, n);
    written += n;
    setServiceProgress(written, len);

    // Decided in the first packet: a foreign stream costs ~288 bytes, not 64 KB.
    if (!header_judged && header.complete()) {
      header_judged = true;
      OtaMetadata incoming;
      OtaMetadata running;
      const char* refusal = NULL;

      if (!header.read(incoming)) {
        if (!bypass_marker_check) {
          sprintf(reply, "ERR: no %s metadata -- not a build of this project", OTA_MOD_MARKER);
          refusal = reply;
        }
      } else if (!(incoming.mods & MOD_BIT_HOTSPOT_OTA) && !bypass_marker_check) {
        // A build of this project, but without the OTA mod -- flashing it strands this node.
        refusal = "ERR: image has no OTA support, would lose remote-update ability -- aborting";
      } else if (runningMetadata(running)) {
        if (strcmp(running.board_role, incoming.board_role) != 0) {
          sprintf(reply, "ERR: image is for %s, this node is %s", incoming.board_role, running.board_role);
          refusal = reply;
        } else if (!bypass_marker_check
                   && strcmp(running.upstream_version, incoming.upstream_version) == 0
                   && strcmp(running.repo_sha, incoming.repo_sha) == 0) {
          // `set ota.fw.marker` forces it through, for a re-flash over a corrupt partition.
          sprintf(reply, "already running %s (%s) -- nothing to do",
                  incoming.upstream_version, incoming.repo_sha);
          refusal = reply;
        }
      }

      if (refusal != NULL) {
        if (refusal != reply) strcpy(reply, refusal);
        closeNow(http);   // network torn down before touching Update state -- see drainAndClose()
        Update.abort();
        trailing.release();
        digitalWrite(PIN_HOTSPOT_PWR, LOW);
        WiFi.disconnect(true);
        return false;
      }
    }
  }
  http.end();

  if (!advanceServiceState(OtaServiceState::Verifying)) {
    strcpy(reply, "ERR: canceled");
    Update.abort();
    trailing.release();
    digitalWrite(PIN_HOTSPOT_PWR, LOW);
    WiFi.disconnect(true);
    return false;
  }

  if (len > 0 && written != len) {
    strcpy(reply, "ERR: download incomplete");
    Update.abort();
    trailing.release();
    digitalWrite(PIN_HOTSPOT_PWR, LOW);
    WiFi.disconnect(true);
    return false;
  }

  // An image shorter than the metadata block never reached the check inside the loop.
  if (!bypass_marker_check && !header_judged) {
    strcpy(reply, "ERR: not a hotspot ota build (truncated), would lose remote-update ability -- aborting");
    Update.abort();
    trailing.release();
    digitalWrite(PIN_HOTSPOT_PWR, LOW);
    WiFi.disconnect(true);
    return false;
  }

  bool hash_ok = trailing.matches();
  trailing.release();
  if (!hash_ok) {
    strcpy(reply, "ERR: SHA-256 mismatch");
    Update.abort();
    digitalWrite(PIN_HOTSPOT_PWR, LOW);
    WiFi.disconnect(true);
    return false;
  }

  // Hash verified and bytes are already in the inactive partition -- network dependency ends here.
  digitalWrite(PIN_HOTSPOT_PWR, LOW);
  WiFi.disconnect(true);

  setServiceState(OtaServiceState::Committing);
  if (!Update.end(true)) {   // flips boot partition pointer
    strcpy(reply, "ERR: Update.end failed");
    return false;
  }

  strcpy(reply, "OK - update installed");
  return true;
}

static bool wanStateMatches(const WanRuntimeState& initial) {
  if (WiFi.getMode() != initial.wifi_mode) return false;
  bool connected = WiFi.status() == WL_CONNECTED;
  if (connected != initial.connected) return false;
  if (connected && WiFi.SSID() != initial.ssid) return false;
  return HotspotOTA::getPower() == initial.power;
}

static bool restoreWanState(const WanRuntimeState& initial, const HotspotOtaConfig& cfg) {
  bool wifi_ok = true;
  if (initial.connected) {
    if (strcmp(initial.ssid, cfg.ssid) != 0) {
      wifi_ok = false;
    } else if (!alreadyConnectedTo(cfg)) {
      char ignored[MAX_TEXT_LEN] = {0};
      wifi_ok = joinWifiStation(cfg.ssid, cfg.password, ignored,
                                OTA_WAN_RESTORE_ATTEMPTS, false, false);
    }
    if (wifi_ok) WiFi.mode(initial.wifi_mode);
  } else {
    WiFi.disconnect(true);
    wifi_ok = WiFi.mode(initial.wifi_mode);
  }

  pinMode(PIN_HOTSPOT_PWR, OUTPUT);
  digitalWrite(PIN_HOTSPOT_PWR, initial.power ? HIGH : LOW);
  delay(20);
  return wifi_ok && wanStateMatches(initial);
}

static bool runWanVerify(const HotspotOtaConfig& cfg, const WanRuntimeState& initial,
                         char reply[]) {
  WanRecord record;
  if (!loadWanRecord(record)) record = newWanRecord();
  if (!recordMatches(record, cfg)) {
    record.result = (uint8_t)WanResult::Unknown;
    record.proven = 0;
  }
  credentialDigest(cfg, record.credentials);

  pinMode(PIN_HOTSPOT_PWR, OUTPUT);
  digitalWrite(PIN_HOTSPOT_PWR, HIGH);

  bool joined = alreadyConnectedTo(cfg);
  if (!joined) {
    setServiceState(OtaServiceState::Joining);
    char ignored[MAX_TEXT_LEN] = {0};
    joined = joinWifiStation(cfg.ssid, cfg.password, ignored,
                             OTA_WIFI_JOIN_MAX_ATTEMPTS, false, false);
  }

  setServiceState(OtaServiceState::CheckingWan);
  bool wan_ok = joined && checkWanConnectivity(OTA_WAN_VERIFY_ATTEMPTS);
  bool ntp_ok = false;
  if (wan_ok) {
    setServiceState(OtaServiceState::Verifying);
    for (int attempt = 0; attempt < OTA_NTP_VERIFY_ATTEMPTS && !ntp_ok; attempt++) {
      ntp_ok = syncNtpTime();
    }
  }

  bool restored = restoreWanState(initial, cfg);
  if (!restored) {
    record.result = (uint8_t)WanResult::RestoreFault;
    portENTER_CRITICAL(&service_mux);
    service_wan_result = WanResult::RestoreFault;
    portEXIT_CRITICAL(&service_mux);
    saveWanRecord(record);
    strcpy(reply, "ERR: WAN state restore failed");
    return false;
  }

  record.result = (uint8_t)(!wan_ok ? WanResult::WanFailed
                                    : (!ntp_ok ? WanResult::NtpFailed : WanResult::Verified));
  if (wan_ok && ntp_ok) record.proven = 1;
  portENTER_CRITICAL(&service_mux);
  service_wan_result = (WanResult)record.result;
  portEXIT_CRITICAL(&service_mux);
  if (!saveWanRecord(record)) {
    strcpy(reply, "ERR: WAN health not saved");
    return false;
  }

  if (!wan_ok) strcpy(reply, "WAN failed; NTP skipped; state restored");
  else if (!ntp_ok) strcpy(reply, "WAN OK; NTP failed; state restored");
  else strcpy(reply, "WAN OK; NTP OK; state restored");
  return wan_ok && ntp_ok;
}

static void serviceTaskMain(void*) {
  HotspotOtaConfig cfg;
  WanRuntimeState initial;
  bool bypass_marker_check;
  ServiceKind kind;

  portENTER_CRITICAL(&service_mux);
  cfg = service_config;
  initial = service_wan_initial;
  bypass_marker_check = service_bypass_marker;
  kind = service_kind;
  portEXIT_CRITICAL(&service_mux);

  char result[MAX_TEXT_LEN] = {0};
  bool ok;
  if (kind == ServiceKind::Ota) {
    ok = runService(cfg, bypass_marker_check, result);
    digitalWrite(PIN_HOTSPOT_PWR, LOW);
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
  } else if (kind == ServiceKind::WanVerify) {
    ok = runWanVerify(cfg, initial, result);
  } else {
    ok = false;
    strcpy(result, "ERR: invalid service");
  }

  portENTER_CRITICAL(&service_mux);
  StrHelper::strncpy(service_result, result, sizeof(service_result));
  if (kind == ServiceKind::Ota) {
    service_state = ok ? OtaServiceState::Succeeded
                       : (service_cancel_requested ? OtaServiceState::Canceled
                                                   : OtaServiceState::Failed);
  } else {
    service_state = OtaServiceState::WanComplete;
  }
  portEXIT_CRITICAL(&service_mux);
  vTaskDelete(NULL);
}

bool HotspotOTA::start(const HotspotOtaConfig& cfg, char reply[]) {
  if (cfg.ssid[0] == 0 || cfg.url[0] == 0) {
    strcpy(reply, "ERR: ota.wan.wifi not configured");
    return false;
  }
  if (!modLoadAllowed()) {
    strcpy(reply, "ERR: battery low; hotspot not powered");
    return false;
  }
  // On probation the other slot holds the only known-good firmware; overwriting it trades a
  // recoverable bad update for a brick. The reply names the retry time, so it reads as a wait.
  RollbackGuard::ProbationState probation = RollbackGuard::probation();
  if (!probation.known) {
    strcpy(reply, "ERR: rollback state unavailable; OTA refused");
    return false;
  }
  if (probation.pending) {
    sprintf(reply, "ERR: firmware on probation; retry in %us", (unsigned)probation.remaining_secs);
    return false;
  }

  // An upstream `start ota` upload owns the same flash writer and the same inactive slot, and it
  // starts without this mod seeing it. Checked before claiming the service for ourselves.
  if (Update.isRunning()) {
    strcpy(reply, "ERR: an upload is already writing flash");
    return false;
  }

  portENTER_CRITICAL(&service_mux);
  bool busy = serviceIsActive(service_state) || service_state == OtaServiceState::Succeeded;
  if (!busy) {
    service_config = cfg;
    service_kind = ServiceKind::Ota;
    service_bypass_marker = marker_bypass;
    marker_bypass = false;
    service_cancel_requested = false;
    service_written = 0;
    service_total = -1;
    service_result[0] = 0;
    service_state = OtaServiceState::Queued;
    service_sleep_inhibited = true;
  }
  portEXIT_CRITICAL(&service_mux);

  if (busy) {
    strcpy(reply, "ERR: OTA already active");
    return false;
  }

  modBoardInhibitSleep(true);
  BaseType_t created = xTaskCreate(serviceTaskMain, "wan-ota", 8192, NULL, 1, NULL);
  if (created != pdPASS) {
    portENTER_CRITICAL(&service_mux);
    service_state = OtaServiceState::Failed;
    service_kind = ServiceKind::None;
    strcpy(service_result, "ERR: could not start OTA task");
    service_sleep_inhibited = false;
    portEXIT_CRITICAL(&service_mux);
    modBoardInhibitSleep(false);
    strcpy(reply, "ERR: could not start OTA task");
    return false;
  }

  strcpy(reply, "OK - OTA queued");
  return true;
}

bool HotspotOTA::cancel(char reply[]) {
  portENTER_CRITICAL(&service_mux);
  bool cancellable = service_kind == ServiceKind::Ota
                     && service_state >= OtaServiceState::Queued
                     && service_state <= OtaServiceState::Downloading;
  if (cancellable) service_cancel_requested = true;
  portEXIT_CRITICAL(&service_mux);

  strcpy(reply, cancellable ? "OK - cancel requested" : "ERR: OTA is not cancellable");
  return cancellable;
}

bool HotspotOTA::flashWriteInProgress() {
  return Update.isRunning();
}

bool HotspotOTA::isActive() {
  portENTER_CRITICAL(&service_mux);
  bool active = serviceIsActive(service_state) || service_state == OtaServiceState::Succeeded;
  portEXIT_CRITICAL(&service_mux);
  return active;
}

bool HotspotOTA::refuseWhileActive(char reply[]) {
  portENTER_CRITICAL(&service_mux);
  OtaServiceState state = service_state;
  ServiceKind kind = service_kind;
  portEXIT_CRITICAL(&service_mux);

  if (!serviceIsActive(state) && state != OtaServiceState::Succeeded) return false;

  // Verifying and Committing are not cancellable by design, and Succeeded is past the commit.
  bool cancellable = state >= OtaServiceState::Queued && state <= OtaServiceState::Downloading;
  if (kind != ServiceKind::Ota) {
    strcpy(reply, "ERR: WAN verification active; wait for it to finish");
  } else if (cancellable) {
    strcpy(reply, "ERR: OTA active; ota cancel first");
  } else {
    sprintf(reply, "ERR: OTA %s; wait for it to finish", serviceStateName(state));
  }
  return true;
}

void HotspotOTA::status(char reply[]) {
  OtaServiceState state;
  size_t written;
  int total;
  char result[MAX_TEXT_LEN];

  portENTER_CRITICAL(&service_mux);
  state = service_state;
  written = service_written;
  total = service_total;
  memcpy(result, service_result, sizeof(result));
  portEXIT_CRITICAL(&service_mux);

  if (state == OtaServiceState::Downloading) {
    if (total > 0) {
      snprintf(reply, MAX_TEXT_LEN, "> %s %u/%u", serviceStateName(state),
               (unsigned)written, (unsigned)total);
    } else {
      snprintf(reply, MAX_TEXT_LEN, "> %s %u bytes", serviceStateName(state), (unsigned)written);
    }
  } else if (result[0] != 0) {
    snprintf(reply, MAX_TEXT_LEN, "> %s: %s", serviceStateName(state), result);
  } else {
    snprintf(reply, MAX_TEXT_LEN, "> %s", serviceStateName(state));
  }
}

void HotspotOTA::poll() {
  OtaServiceState state;
  bool release_sleep = false;
  uint32_t clock_epoch;
  ServiceKind kind;

  portENTER_CRITICAL(&service_mux);
  state = service_state;
  kind = service_kind;
  clock_epoch = service_clock_epoch;
  service_clock_epoch = 0;
  if (service_sleep_inhibited
      && (state == OtaServiceState::Failed || state == OtaServiceState::Canceled
          || state == OtaServiceState::WanComplete)) {
    service_sleep_inhibited = false;
    service_kind = ServiceKind::None;
    release_sleep = true;
  }
  portEXIT_CRITICAL(&service_mux);

  if (clock_epoch != 0) modClockSet(clock_epoch);
  if (release_sleep) modBoardInhibitSleep(false);
  if (release_sleep && kind == ServiceKind::WanVerify) {
    portENTER_CRITICAL(&service_mux);
    wan_done_run = service_run;
    wan_done_result = service_wan_result;
    portEXIT_CRITICAL(&service_mux);
  }
  if (state == OtaServiceState::Succeeded) {
    HotspotOTA::setPower(false);
    modBoardReboot();
  }
}

bool HotspotOTA::loadConfig(HotspotOtaConfig& cfg) {
  memset(&cfg, 0, sizeof(cfg));
  File f = SPIFFS.open("/ota_hotspot", "r");
  if (!f) return false;
  f.read((uint8_t*)&cfg, sizeof(cfg));
  f.close();
  return true;
}

bool HotspotOTA::saveConfig(const HotspotOtaConfig& cfg) {
  File f = SPIFFS.open("/ota_hotspot", "w");
  if (!f) return false;
  f.write((const uint8_t*)&cfg, sizeof(cfg));
  f.close();
  return true;
}

void HotspotOTA::setPower(bool on) {
  pinMode(PIN_HOTSPOT_PWR, OUTPUT);
  digitalWrite(PIN_HOTSPOT_PWR, on ? HIGH : LOW);
}

bool HotspotOTA::getPower() {
  // No pinMode() here -- reading shouldn't have side effects; digitalRead() works regardless.
  return digitalRead(PIN_HOTSPOT_PWR) == HIGH;
}

bool HotspotOTA::wifiConnect(char reply[]) {
  if (HotspotOTA::isActive()) {
    strcpy(reply, "ERR: OTA active");
    return false;
  }
  HotspotOtaConfig cfg;
  HotspotOTA::loadConfig(cfg);
  if (cfg.ssid[0] == 0) {
    strcpy(reply, "ERR: ota.wan.wifi not configured");
    return false;
  }
  if (!modLoadAllowed()) {
    strcpy(reply, "ERR: battery low; hotspot not powered");
    return false;
  }

  pinMode(PIN_HOTSPOT_PWR, OUTPUT);
  digitalWrite(PIN_HOTSPOT_PWR, HIGH);   // hotspot needs power before its AP exists to join

  if (!joinWifiStation(cfg.ssid, cfg.password, reply, OTA_DIAG_WIFI_JOIN_ATTEMPTS, false, true)) {
    digitalWrite(PIN_HOTSPOT_PWR, LOW);
    return false;
  }

  strcpy(reply, "OK - joined");
  return true;
}

void HotspotOTA::wifiDisconnect() {
  if (HotspotOTA::isActive()) return;
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  digitalWrite(PIN_HOTSPOT_PWR, LOW);
}

bool HotspotOTA::checkWan(char reply[]) {
  if (HotspotOTA::isActive()) {
    strcpy(reply, "ERR: OTA active");
    return false;
  }
  bool ok = checkWanConnectivity();
  strcpy(reply, ok ? "WAN OK" : "WAN ERR");
  return ok;
}

bool HotspotOTA::verifyWan(char reply[]) {
  if (Update.isRunning()) {
    strcpy(reply, "ERR: OTA upload in progress");
    return false;
  }

  HotspotOtaConfig cfg;
  HotspotOTA::loadConfig(cfg);
  if (cfg.ssid[0] == 0) {
    strcpy(reply, "ERR: ota.wan.wifi not configured");
    return false;
  }
  if (!modLoadAllowed()) {
    strcpy(reply, "ERR: battery low; hotspot not powered");
    return false;
  }

  wifi_mode_t mode = WiFi.getMode();
  if (mode == WIFI_MODE_AP || mode == WIFI_MODE_APSTA) {
    strcpy(reply, "ERR: WiFi access point active");
    return false;
  }
  bool connected = WiFi.status() == WL_CONNECTED;
  if (connected && WiFi.SSID() != cfg.ssid) {
    strcpy(reply, "ERR: connected WiFi cannot be restored");
    return false;
  }

  portENTER_CRITICAL(&service_mux);
  bool busy = serviceIsActive(service_state) || service_state == OtaServiceState::Succeeded;
  if (!busy) service_state = OtaServiceState::Queued;
  portEXIT_CRITICAL(&service_mux);
  if (busy) {
    strcpy(reply, "ERR: OTA/WAN operation active");
    return false;
  }

  WanRuntimeState initial = {mode, HotspotOTA::getPower(), connected, {0}};
  if (connected) StrHelper::strncpy(initial.ssid, WiFi.SSID().c_str(), sizeof(initial.ssid));

  portENTER_CRITICAL(&service_mux);
  service_config = cfg;
  service_wan_initial = initial;
  service_kind = ServiceKind::WanVerify;
  service_wan_result = WanResult::Unknown;
  if (++wan_run_seq == 0) wan_run_seq = 1;
  service_run = wan_run_seq;
  service_result[0] = 0;
  service_sleep_inhibited = true;
  portEXIT_CRITICAL(&service_mux);
  modBoardInhibitSleep(true);
  if (xTaskCreate(serviceTaskMain, "wan-verify", 8192, NULL, 1, NULL) != pdPASS) {
    portENTER_CRITICAL(&service_mux);
    service_state = OtaServiceState::Failed;
    service_kind = ServiceKind::None;
    service_sleep_inhibited = false;
    portEXIT_CRITICAL(&service_mux);
    modBoardInhibitSleep(false);
    strcpy(reply, "ERR: could not start WAN verification");
    return false;
  }

  strcpy(reply, "OK - WAN verification queued");
  return true;
}

static const char* wanResultName(WanResult result) {
  switch (result) {
    case WanResult::Unknown: return "UNKNOWN";
    case WanResult::WanFailed: return "WAN_0|NTP_0";
    case WanResult::NtpFailed: return "WAN_1|NTP_0";
    case WanResult::Verified: return "WAN_1|NTP_1";
    case WanResult::RestoreFault: return "RESTORE_FAULT";
  }
  return "UNKNOWN";
}

void HotspotOTA::wanHealth(char reply[]) {
  ServiceKind kind;
  OtaServiceState state;
  portENTER_CRITICAL(&service_mux);
  kind = service_kind;
  state = service_state;
  portEXIT_CRITICAL(&service_mux);
  if (kind == ServiceKind::WanVerify && serviceIsActive(state)) {
    strcpy(reply, "> checking");
    return;
  }

  HotspotOtaConfig cfg;
  HotspotOTA::loadConfig(cfg);
  WanRecord record;
  if (!loadWanRecord(record) || !recordMatches(record, cfg)) {
    strcpy(reply, "> latest=UNKNOWN proven=no");
    return;
  }
  snprintf(reply, MAX_TEXT_LEN, "> latest=%s proven=%s",
           wanResultName((WanResult)record.result), record.proven ? "yes" : "no");
}

bool HotspotOTA::wanProven() {
  HotspotOtaConfig cfg;
  HotspotOTA::loadConfig(cfg);
  WanRecord record;
  return loadWanRecord(record) && record.proven && recordMatches(record, cfg);
}

bool HotspotOTA::resetWanHealth() {
  if (HotspotOTA::isActive()) return false;
  HotspotOtaConfig cfg;
  HotspotOTA::loadConfig(cfg);
  WanRecord record = newWanRecord();
  credentialDigest(cfg, record.credentials);
  return saveWanRecord(record);
}

bool HotspotOTA::startWanVerify(uint32_t& run) {
  char reply[MAX_TEXT_LEN];
  if (!HotspotOTA::verifyWan(reply)) return false;
  portENTER_CRITICAL(&service_mux);
  run = service_run;
  portEXIT_CRITICAL(&service_mux);
  return true;
}

HotspotOTA::WanRun HotspotOTA::wanVerifyResult(uint32_t run) {
  portENTER_CRITICAL(&service_mux);
  uint32_t current = service_run;
  uint32_t done = wan_done_run;
  WanResult result = wan_done_result;
  portEXIT_CRITICAL(&service_mux);
  if (run == 0) return WanRun::Unknown;
  if (run == done) {
    switch (result) {
      case WanResult::WanFailed: return WanRun::WanFailed;
      case WanResult::NtpFailed: return WanRun::NtpFailed;
      case WanResult::Verified: return WanRun::Verified;
      case WanResult::RestoreFault: return WanRun::RestoreFault;
      default: return WanRun::Unknown;
    }
  }
  return run == current ? WanRun::Pending : WanRun::Unknown;
}
