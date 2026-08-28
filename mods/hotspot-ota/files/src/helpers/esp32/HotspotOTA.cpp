#include "HotspotOTA.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <Update.h>
#include <SPIFFS.h>
#include <mbedtls/sha256.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <time.h>
#include <Utils.h>                    // mesh::Utils::fromHex
#include <helpers/TxtDataHelpers.h>   // StrHelper lives here, no standalone StrHelper.h
#include <helpers/ModHooks.h>          // modClockSet()

// The OTA_WIFI_*, OTA_HTTP_*, OTA_WAN_* and PIN_HOTSPOT_PWR values are injected as build flags
// from variants/<board>/overrides.yaml, which carries each board's values and rationale.
#define OTA_WAN_CHECK_HOST       "google.com"   // 3rd-party host, distinct from the firmware host

// Neither target board has a battery-backed RTC, so the clock is always wrong after a reboot.
// Piggybacked on WiFi join to reuse the radio-on window OTA already paid for.
#define OTA_NTP_SERVER            "us.pool.ntp.org"   // regional zone -- lower latency on a blocking call
#define OTA_NTP_SERVER_FALLBACK   "pool.ntp.org"      // only tried if the regional zone doesn't answer
#define OTA_NTP_SYNC_TIMEOUT_MS   5000
#define OTA_NTP_SANITY_FLOOR      1700000000   // ~Nov 2023 -- rules out an unset/failed sync

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

static bool marker_bypass = false;   // RAM-only, one-time
static char manual_sha256_hex[65] = {0};   // RAM-only -- see `set ota.fw.sha256`

void HotspotOTA::setMarkerBypass(bool on) {
  marker_bypass = on;
}

void HotspotOTA::setSha256Hex(const char* hex) {
  StrHelper::strncpy(manual_sha256_hex, hex, sizeof(manual_sha256_hex));
}

const char* HotspotOTA::getSha256Hex() {
  return manual_sha256_hex;
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
static void syncNtpTime() {
  configTime(0, 0, OTA_NTP_SERVER, OTA_NTP_SERVER_FALLBACK);
  struct tm timeinfo;
  uint32_t start = millis();
  while (!getLocalTime(&timeinfo, 100) && millis() - start < OTA_NTP_SYNC_TIMEOUT_MS) {
    // getLocalTime()'s own internal wait paces this loop
  }
  time_t now;
  time(&now);
  if (now > OTA_NTP_SANITY_FLOOR) {
    modClockSet((uint32_t)now);
  }
}

// run() is unattended and can be patient; wifiConnect() runs inline in the CLI and must fail fast.
static bool joinWifiStation(const char* ssid, const char* pwd, char reply[], int max_attempts) {
  WiFi.mode(WIFI_STA);
  for (int attempt = 0; attempt < max_attempts; attempt++) {
    WiFi.begin(ssid, pwd);
    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < OTA_WIFI_JOIN_ATTEMPT_TIMEOUT_MS) {
      delay(250);
    }
    if (WiFi.status() == WL_CONNECTED) {
      syncNtpTime();   // best-effort -- see syncNtpTime() above
      return true;
    }
    WiFi.disconnect(true);
    if (attempt < max_attempts - 1) {
      delay(OTA_WIFI_JOIN_RETRY_DELAY_MS);
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

// Advisory only -- never gates run(); used to give a more specific error if a later step fails too.
static bool checkWanConnectivity() {
  IPAddress ip;
  for (int attempt = 0; attempt < OTA_WAN_CHECK_ATTEMPTS; attempt++) {
    if (WiFi.hostByName(OTA_WAN_CHECK_HOST, ip)) return true;
    if (attempt < OTA_WAN_CHECK_ATTEMPTS - 1) delay(OTA_WAN_CHECK_RETRY_DELAY_MS);
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

// The only hash that does not come from the same server as the image, so this path stays.
static bool resolvePinnedHash(const char* manual_hex, uint8_t expect[32]) {
  if (manual_hex[0] == 0) return false;
  return mesh::Utils::fromHex(expect, 32, manual_hex);
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

bool HotspotOTA::run(const HotspotOtaConfig& cfg, char reply[]) {
  if (cfg.ssid[0] == 0 || cfg.url[0] == 0) {
    strcpy(reply, "ERR: ota.wan.wifi not configured");
    return false;
  }

  bool bypass_marker_check = marker_bypass;
  marker_bypass = false;   // one-time

  pinMode(PIN_HOTSPOT_PWR, OUTPUT);
  digitalWrite(PIN_HOTSPOT_PWR, HIGH);   // must stay HIGH throughout -- load switch, not a latch

  if (!alreadyConnectedTo(cfg)) {
    if (!joinWifiStation(cfg.ssid, cfg.password, reply, OTA_WIFI_JOIN_MAX_ATTEMPTS)) {
      digitalWrite(PIN_HOTSPOT_PWR, LOW);
      return false;
    }
  }

  bool wan_ok = checkWanConnectivity();   // advisory only, for a better error message below

  uint8_t pinned[32];
  bool have_pinned = resolvePinnedHash(manual_sha256_hex, pinned);

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

  // A pin covers the whole file, the embedded digest all but its own 32 bytes. One or the other.
  mbedtls_sha256_context pinned_ctx;
  TrailingDigest trailing;
  if (have_pinned) {
    mbedtls_sha256_init(&pinned_ctx);
    mbedtls_sha256_starts(&pinned_ctx, 0);
  } else {
    trailing.begin();
  }
  HeaderInspector header;
  bool header_judged = false;

  WiFiClient* stream = http.getStreamPtr();
  uint8_t buf[1024];
  int written = 0;
  while (http.connected() && (len < 0 || written < len)) {
    size_t avail = stream->available();
    if (!avail) { delay(1); continue; }
    int n = stream->readBytes(buf, min(avail, sizeof(buf)));
    if (n <= 0) break;
    if (Update.write(buf, n) != (size_t)n) {
      strcpy(reply, "ERR: flash write failed");
      drainAndClose(http);   // network torn down before touching Update state -- see drainAndClose()
      Update.abort();
      digitalWrite(PIN_HOTSPOT_PWR, LOW);
      WiFi.disconnect(true);
      return false;
    }
    if (have_pinned) {
      mbedtls_sha256_update(&pinned_ctx, buf, n);
    } else {
      trailing.feed(buf, n);
    }
    header.feed(buf, n);
    written += n;

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
        if (!have_pinned) trailing.release();
        digitalWrite(PIN_HOTSPOT_PWR, LOW);
        WiFi.disconnect(true);
        return false;
      }
    }
  }
  http.end();

  if (len > 0 && written != len) {
    strcpy(reply, "ERR: download incomplete");
    Update.abort();
    if (!have_pinned) trailing.release();
    digitalWrite(PIN_HOTSPOT_PWR, LOW);
    WiFi.disconnect(true);
    return false;
  }

  // An image shorter than the metadata block never reached the check inside the loop.
  if (!bypass_marker_check && !header_judged) {
    strcpy(reply, "ERR: not a hotspot ota build (truncated), would lose remote-update ability -- aborting");
    Update.abort();
    if (!have_pinned) trailing.release();
    digitalWrite(PIN_HOTSPOT_PWR, LOW);
    WiFi.disconnect(true);
    return false;
  }

  bool hash_ok;
  if (have_pinned) {
    uint8_t digest[32];
    mbedtls_sha256_finish(&pinned_ctx, digest);
    mbedtls_sha256_free(&pinned_ctx);
    hash_ok = memcmp(digest, pinned, 32) == 0;
  } else {
    hash_ok = trailing.matches();
    trailing.release();
  }
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

  if (!Update.end(true)) {   // flips boot partition pointer
    strcpy(reply, "ERR: Update.end failed");
    return false;
  }

  strcpy(reply, "OK - rebooting");
  return true;   // caller reboots
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
  HotspotOtaConfig cfg;
  HotspotOTA::loadConfig(cfg);
  if (cfg.ssid[0] == 0) {
    strcpy(reply, "ERR: ota.wan.wifi not configured");
    return false;
  }

  pinMode(PIN_HOTSPOT_PWR, OUTPUT);
  digitalWrite(PIN_HOTSPOT_PWR, HIGH);   // hotspot needs power before its AP exists to join

  if (!joinWifiStation(cfg.ssid, cfg.password, reply, OTA_DIAG_WIFI_JOIN_ATTEMPTS)) {
    digitalWrite(PIN_HOTSPOT_PWR, LOW);
    return false;
  }

  strcpy(reply, "OK - joined");
  return true;
}

void HotspotOTA::wifiDisconnect() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  digitalWrite(PIN_HOTSPOT_PWR, LOW);
}

bool HotspotOTA::checkWan(char reply[]) {
  bool ok = checkWanConnectivity();
  strcpy(reply, ok ? "WAN OK" : "WAN ERR");
  return ok;
}
