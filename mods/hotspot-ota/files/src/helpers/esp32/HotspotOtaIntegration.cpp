#include <helpers/esp32/HotspotOtaIntegration.h>

#include <MeshCore.h>
#include <SPIFFS.h>
#include <Update.h>
#include <WiFi.h>
#include <helpers/TxtDataHelpers.h>
#include <helpers/esp32/HotspotOTA.h>
#include <helpers/esp32/RollbackGuard.h>

// DISABLE_WIFI_OTA is upstream's switch for its own `start ota`; this mod replaces that
// implementation, so its AP is gated on the role marker alone.
#if defined(ADMIN_PASSWORD)
#include <ESPAsyncWebServer.h>
// Generated at build time from mods/hotspot-ota/web/ota-page.min.html.
#include <helpers/esp32/OtaWebPage.h>
#endif

#ifndef OTA_MOD_BUILD_DATE
#define OTA_MOD_BUILD_DATE "unknown"
#endif

bool hotspotOtaRadioInit(const char* build_id) {
  SPIFFS.begin(true);
  for (int attempt = 0; attempt < 3; attempt++) {
    if (modBoardRadioInit()) {
      RollbackGuard::begin(build_id);
      return true;
    }
    delay(500);
  }
  MESH_DEBUG_PRINTLN("Radio init failed!");
  RollbackGuard::onRadioInitFailure();
  return false;
}

// Upstream's `start ota` raises the access point and web server, keeps no handle on the server and
// never stops either. This mod owns both instead, so the listener can actually be closed.
#define OTA_AP_DEADLINE_MS  (20UL * 60 * 1000)
#define OTA_AP_STALL_MS     (10UL * 60 * 1000)
// Long enough for AsyncTCP to drain the response now that the callback no longer blocks.
#define OTA_REBOOT_GRACE_MS 1500UL

#if defined(ADMIN_PASSWORD)
#define OTA_AP_OWNED  1

static AsyncWebServer ota_server(80);
static wifi_mode_t ap_prev_mode = WIFI_OFF;
static char     ota_home[96];
static char     ota_identity[128];
static bool     ap_routed = false;
static bool     ap_up = false;
static bool     ap_uploading = false;
static bool     ap_reboot_pending = false;
static uint32_t ap_reboot_ms = 0;

// `start ota` refuses a second AP session, but not a second POST to the one already up, so each
// request carries its own verdict and only Update.end() success may claim it.
enum ApUploadState : uint8_t { AP_UPLOAD_WRITING, AP_UPLOAD_FAILED, AP_UPLOAD_DONE };
struct ApUpload {
  ApUploadState state;
  const char* error;
};
// The single upload allowed to hold the flash writer; a second request is refused without
// touching this one's state.
static ApUpload* ap_upload_owner = NULL;
static uint32_t ap_started_ms = 0;
static uint32_t ap_progress_ms = 0;
static size_t   ap_progress_bytes = 0;

static uint32_t apSecondsLeft() {
  uint32_t elapsed = millis() - ap_started_ms;
  return elapsed >= OTA_AP_DEADLINE_MS ? 0 : (OTA_AP_DEADLINE_MS - elapsed) / 1000;
}

static void apUploadRelease(ApUpload* upload) {
  if (ap_upload_owner == upload) ap_upload_owner = NULL;
}

static void apUploadFail(ApUpload* upload, const char* reason) {
  upload->state = AP_UPLOAD_FAILED;
  upload->error = reason;
  apUploadRelease(upload);
}

// Routes outlive a session: AsyncWebServer holds them independently of the listening socket, and
// re-registering on each start would stack duplicate handlers.
static void apStart(const char* id, char* reply) {
  if (!ap_routed) {
    sprintf(ota_home, "<H2>MeshCore node. ID: %s</H2>", id);
    snprintf(ota_identity, sizeof(ota_identity),
             "{\"id\": \"%s\", \"hardware\": \"ESP32\"}", id);
    ota_server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
      request->send(200, "text/html", ota_home);
    });
    ota_server.on("/log", HTTP_GET, [](AsyncWebServerRequest* request) {
      request->send(SPIFFS, "/packet_log", "text/plain");
    });
    // Before /update: a handler also matches any path under its own, and the first registered
    // wins, so the page would otherwise answer this request with itself.
    ota_server.on("/update/identity", HTTP_GET, [](AsyncWebServerRequest* request) {
      request->send(200, "application/json", ota_identity);
    });
    // The upload page, served gzipped straight from flash.
    ota_server.on("/update", HTTP_GET, [](AsyncWebServerRequest* request) {
      AsyncWebServerResponse* resp = request->beginResponse_P(
          200, "text/html", MOBMESH_OTA_PAGE, MOBMESH_OTA_PAGE_LEN);
      resp->addHeader("Content-Encoding", "gzip");
      request->send(resp);
    });
    // Rebooting from inside the callback preempts the queued response and leaves every client
    // unable to tell a completed update from a hung one, so the reboot is deferred to apPoll().
    ota_server.on("/update", HTTP_POST,
      // The only response this request sends: answering from the chunk callback too would send
      // two, and a rejection Update never saw would otherwise read as success and reboot.
      [](AsyncWebServerRequest* request) {
        // _tempObject is null when the chunk callback never ran -- a POST carrying no file part.
        ApUpload* upload = (ApUpload*)request->_tempObject;
        bool ok = upload && upload->state == AP_UPLOAD_DONE;
        const char* body = ok ? "OK"
                              : (upload && upload->error ? upload->error : "no firmware uploaded");
        AsyncWebServerResponse* resp = request->beginResponse(ok ? 200 : 400, "text/plain", body);
        resp->addHeader("Connection", "close");
        request->send(resp);
        if (ok) { ap_reboot_pending = true; ap_reboot_ms = millis(); }
      },
      [](AsyncWebServerRequest* request, String filename, size_t index, uint8_t* data, size_t len,
         bool final) {
        ApUpload* upload = (ApUpload*)request->_tempObject;
        if (index == 0 && !upload) {
          // Freed by the request destructor, so it must come from malloc.
          upload = (ApUpload*)malloc(sizeof(ApUpload));
          if (!upload) return;
          upload->state = AP_UPLOAD_WRITING;
          upload->error = NULL;
          request->_tempObject = upload;

          if (ap_reboot_pending) {
            apUploadFail(upload, "reboot pending; update already installed");
            return;
          }
          if (ap_upload_owner) {
            // Refused without touching the upload that holds the writer.
            upload->state = AP_UPLOAD_FAILED;
            upload->error = "another upload is in progress";
            return;
          }
          if (!request->hasParam("MD5", true)) {
            apUploadFail(upload, "MD5 parameter missing");
            return;
          }
          int cmd = (filename == "filesystem") ? U_SPIFFS : U_FLASH;
          if (!Update.begin(UPDATE_SIZE_UNKNOWN, cmd)) {
            apUploadFail(upload, "OTA could not begin");
            return;
          }
          ap_upload_owner = upload;
          // After begin(), never before: begin() clears the expected digest, so upstream's order
          // leaves MD5 verification silently disabled.
          if (!Update.setMD5(request->getParam("MD5", true)->value().c_str())) {
            Update.abort();
            apUploadFail(upload, "MD5 parameter invalid");
            return;
          }
        }
        // Chunks keep arriving after a rejection, and a refused request never owned the writer.
        if (!upload || upload->state != AP_UPLOAD_WRITING || ap_upload_owner != upload) return;
        if (len && Update.write(data, len) != len) {
          Update.abort();
          apUploadFail(upload, "OTA write failed");
          return;
        }
        if (final) {
          if (!Update.end(true)) {
            Update.abort();
            apUploadFail(upload, "could not end OTA");
            return;
          }
          upload->state = AP_UPLOAD_DONE;
          apUploadRelease(upload);
        }
      });
    ap_routed = true;
  }
  // softAP() alone only adds AP to the current mode; a disconnected station would survive into
  // APSTA and could reconnect under the server, exposing the unauthenticated page off-network.
  ap_prev_mode = WiFi.getMode();
  WiFi.mode(WIFI_AP);
  WiFi.softAP("MeshCore-OTA", NULL);
  ota_server.begin();
  modBoardInhibitSleep(true);
  ap_up = true;
  ap_uploading = false;
  ap_started_ms = millis();
  sprintf(reply, "Started: http://%s/update", WiFi.softAPIP().toString().c_str());
}

// Ending the server frees the listening socket, so the port is released and a later `start ota`
// binds again. softAPdisconnect stays in its wifi-on form -- the wifi-off form hung a Heltec V4 --
// so the mode captured at start is restored separately, leaving no radio up that was not up before.
static bool apTeardown() {
  ota_server.end();
  // Ending the server destroys any in-flight request, freeing the state ap_upload_owner points at.
  ap_upload_owner = NULL;
  bool stopped = WiFi.softAPdisconnect(false);
  WiFi.mode(ap_prev_mode);
  modBoardInhibitSleep(false);
  ap_up = false;
  ap_uploading = false;
  return stopped;
}

// The upload page and the WAN service share the flash writer, the inactive slot and the sleep
// inhibit, and apPoll would otherwise measure a WAN download against the AP's stall deadline.
static bool refuseWhileApUp(char* reply) {
  if (!ap_up) return false;
  sprintf(reply, "ERR: OTA AP up, %us left; stop ota first", (unsigned)apSecondsLeft());
  return true;
}

// The AP upload holds the same flash writer the WAN service does, so it needs the same interlock.
static bool refuseWhileApWriting(char* reply) {
  if (!Update.isRunning() && !ap_reboot_pending) return false;
  strcpy(reply, ap_reboot_pending ? "ERR: reboot pending after OTA; wait for restart"
                                  : "ERR: OTA upload in progress");
  return true;
}

static void apPoll() {
  if (ap_reboot_pending && millis() - ap_reboot_ms >= OTA_REBOOT_GRACE_MS) modBoardReboot();
  if (!ap_up) return;
  if (Update.isRunning()) {
    size_t written = Update.progress();
    if (!ap_uploading || written != ap_progress_bytes) {
      ap_uploading = true;
      ap_progress_bytes = written;
      ap_progress_ms = millis();
    }
    if (millis() - ap_progress_ms < OTA_AP_STALL_MS) return;
    // A client that vanished mid-POST leaves the flash writer claimed with nothing to release it.
    Update.abort();
    ap_upload_owner = NULL;
  } else {
    // Cleared here, not at start: an upload that begins and fails leaves the tracking behind, and
    // the next attempt in the same session would then be measured against its stall clock.
    ap_uploading = false;
    if (millis() - ap_started_ms < OTA_AP_DEADLINE_MS) return;
  }
  apTeardown();
}

#else
// Boards built without the WiFi OTA stack keep the commands, answering that they are unsupported.
static const bool ap_up = false;
static void apPoll() {}
static bool refuseWhileApUp(char* reply) { return false; }
static bool refuseWhileApWriting(char* reply) { return false; }
static bool apTeardown() { return true; }
static uint32_t apSecondsLeft() { return 0; }
#endif

void hotspotOtaLoop() {
  RollbackGuard::poll();
  HotspotOTA::poll();
  apPoll();
}

// Mirrors the shapes CommonCLI::handleCommand() accepts -- same prefix lengths, `erase` serial-only
// -- so the interlock covers exactly the commands that would reach its destructive branch.
static bool isDestructive(const ModCliContext& context, const char* command) {
  if (memcmp(command, "poweroff", 8) == 0) return true;
  if (memcmp(command, "shutdown", 8) == 0) return true;
  if (memcmp(command, "reboot", 6) == 0) return true;
  if (memcmp(command, "clkreboot", 9) == 0) return true;
  if (context.sender_timestamp == 0 && strcmp(command, "erase") == 0) return true;
  return false;
}

// Refuse only while the rollback image is at stake; otherwise the caller falls through to upstream.
static bool refuseOtaStart(char* reply) {
  if (HotspotOTA::refuseWhileActive(reply)) return true;
  RollbackGuard::ProbationState probation = RollbackGuard::probation();
  if (!probation.known) {
    strcpy(reply, "ERR: rollback state unavailable; OTA refused");
    return true;
  }
  if (probation.pending) {
    sprintf(reply, "ERR: firmware on probation; retry in %us", (unsigned)probation.remaining_secs);
    return true;
  }
  return false;
}

static bool handleCommand(const ModCliContext& context, char* command, char* reply) {
  // A reboot, power-off or erase part-way through an OTA leaves a half-written slot behind --
  // for the AP upload as much as the WAN download, since both drive the one flash writer.
  if (isDestructive(context, command)
      && (HotspotOTA::refuseWhileActive(reply) || refuseWhileApWriting(reply))) return true;

  if (memcmp(command, "ver", 3) == 0) {
    sprintf(reply, "%s (%s) + ota (%s)", context.fw_version, context.fw_build_date,
            OTA_MOD_BUILD_DATE);
  } else if (memcmp(command, "start ota wan update", 21) == 0
             && (command[21] == 0 || command[21] == ' ')) {
    if (refuseWhileApUp(reply)) return true;
    HotspotOtaConfig cfg;
    HotspotOTA::loadConfig(cfg);
    if (cfg.url[0] == 0) {
      strcpy(reply, "ERR: ota.fw.url not configured");
    } else {
      modBoardStartOtaFromUrl(cfg.url, reply);
    }
  } else if (memcmp(command, "start ota wan ", 14) == 0) {
    if (refuseWhileApUp(reply)) return true;
    modBoardStartOtaFromUrl(&command[14], reply);
  } else if (memcmp(command, "start ota", 9) == 0) {
#ifdef OTA_AP_OWNED
    // Consumed here rather than passed to upstream, which leaks its web server and cannot stop it.
    if (refuseOtaStart(reply)) return true;
    if (refuseWhileApUp(reply)) return true;
    // The upload page is unauthenticated, so it stays reachable only over the access point.
    if (WiFi.status() == WL_CONNECTED) {
      strcpy(reply, "ERR: station connected; ota wan leave first");
      return true;
    }
    char id[48];
    sprintf(id, "%s (%s)", context.fw_version, OTA_MOD_BUILD_DATE);
    apStart(id, reply);
#else
    strcpy(reply, "ERR: WiFi OTA not supported");
#endif
  } else if (strcmp(command, "stop ota") == 0) {
    if (!ap_up) {
      strcpy(reply, "ERR: no OTA AP running");
    } else if (Update.isRunning()) {
      strcpy(reply, "ERR: upload in progress");
    } else {
      strcpy(reply, apTeardown() ? "OK - OTA AP stopped"
                                 : "ERR: AP stop failed; reboot to clear");
    }
  } else if (strcmp(command, "ota cancel") == 0) {
    HotspotOTA::cancel(reply);
  } else if (memcmp(command, "ota wan join", 12) == 0) {
    if (refuseWhileApUp(reply)) return true;
    HotspotOTA::wifiConnect(reply);
  } else if (memcmp(command, "ota wan leave", 13) == 0) {
    if (HotspotOTA::isActive()) {
      strcpy(reply, "ERR: OTA active");
    } else {
      HotspotOTA::wifiDisconnect();
      strcpy(reply, "OK - disconnected");
    }
  } else if (memcmp(command, "ota wan check", 13) == 0) {
    HotspotOTA::checkWan(reply);
  } else if (memcmp(command, "ota slot boot ", 14) == 0) {
    // Repointing the bootloader mid-write would boot whatever the interrupted upload left behind.
    if (HotspotOTA::isActive()) {
      strcpy(reply, "ERR: OTA active");
    } else if (refuseWhileApWriting(reply)) {
      return true;
    } else if (RollbackGuard::setActivePartition(command[14], reply)) {
      modBoardReboot();
    }
  } else {
    return false;
  }
  return true;
}

static bool handleSet(char* command, char* reply) {
  char* config = &command[4];
  if (memcmp(config, "ota.wan.wifi ", 13) == 0) {
    HotspotOtaConfig cfg;
    HotspotOTA::loadConfig(cfg);
    char* comma = strchr(&config[13], ',');
    if (comma) {
      *comma = 0;
      StrHelper::strncpy(cfg.ssid, &config[13], sizeof(cfg.ssid));
      StrHelper::strncpy(cfg.password, comma + 1, sizeof(cfg.password));
      HotspotOTA::saveConfig(cfg);
      strcpy(reply, "OK");
    } else {
      strcpy(reply, "ERR: expected <ssid>,<password>");
    }
  } else if (memcmp(config, "ota.fw.sha256 ", 14) == 0) {
    HotspotOTA::setSha256Hex(memcmp(&config[14], "clear", 5) == 0 ? "" : &config[14]);
    strcpy(reply, "OK");
  } else if (memcmp(config, "ota.fw.url ", 11) == 0) {
    HotspotOtaConfig cfg;
    HotspotOTA::loadConfig(cfg);
    if (strlen(&config[11]) >= sizeof(cfg.url)) {
      sprintf(reply, "ERR: URL too long (max %d chars)", (int)sizeof(cfg.url) - 1);
    } else {
      StrHelper::strncpy(cfg.url, &config[11], sizeof(cfg.url));
      HotspotOTA::saveConfig(cfg);
      strcpy(reply, "OK");
    }
  } else if (memcmp(config, "ota.wan.pwr ", 12) == 0) {
    if (HotspotOTA::isActive()) {
      strcpy(reply, "ERR: OTA active");
    } else if (memcmp(&config[12], "on", 2) == 0) {
      HotspotOTA::setPower(true);
      strcpy(reply, "OK");
    } else if (memcmp(&config[12], "off", 3) == 0) {
      HotspotOTA::setPower(false);
      strcpy(reply, "OK");
    } else {
      strcpy(reply, "ERR: expected on|off");
    }
  } else if (memcmp(config, "ota.fw.marker ", 14) == 0) {
    if (memcmp(&config[14], "on", 2) == 0) {
      HotspotOTA::setMarkerBypass(false);
      strcpy(reply, "OK");
    } else if (memcmp(&config[14], "off", 3) == 0) {
      HotspotOTA::setMarkerBypass(true);
      strcpy(reply, "OK");
    } else {
      strcpy(reply, "ERR: expected on|off");
    }
  } else {
    return false;
  }
  return true;
}

static bool handleGet(char* command, char* reply) {
  char* config = &command[4];
  if (memcmp(config, "ota.fw.url", 10) == 0) {
    HotspotOtaConfig cfg;
    HotspotOTA::loadConfig(cfg);
    sprintf(reply, "> %s", cfg.url[0] ? cfg.url : "(not set)");
  } else if (memcmp(config, "ota.wan.pwr", 11) == 0) {
    sprintf(reply, "> %s", HotspotOTA::getPower() ? "on" : "off");
  } else if (memcmp(config, "ota.status", 10) == 0) {
    HotspotOTA::status(reply);
  } else if (memcmp(config, "ota.ap", 6) == 0) {
    if (!ap_up) {
      strcpy(reply, "> down");
    } else {
      sprintf(reply, "> up, %us left%s", (unsigned)apSecondsLeft(),
              Update.isRunning() ? ", uploading" : "");
    }
  } else if (memcmp(config, "ota.slot", 8) == 0) {
    // Skip verification while either writer owns that slot -- this mod's service, or an upstream
    // `start ota` upload. A torn image mid-write is expected, not a fault.
    bool quiet = HotspotOTA::isActive() || HotspotOTA::flashWriteInProgress();
    sprintf(reply, "> %s", RollbackGuard::status(!quiet));
  } else {
    return false;
  }
  return true;
}

bool hotspotOtaHandleCli(const ModCliContext& context, char* command, char* reply) {
  if (memcmp(command, "set ", 4) == 0) return handleSet(command, reply);
  if (memcmp(command, "get ", 4) == 0) return handleGet(command, reply);
  return handleCommand(context, command, reply);
}
