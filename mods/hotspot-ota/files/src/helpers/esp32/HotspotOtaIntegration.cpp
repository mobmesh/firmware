#include <helpers/esp32/HotspotOtaIntegration.h>

#include <MeshCore.h>
#include <SPIFFS.h>
#include <helpers/TxtDataHelpers.h>
#include <helpers/esp32/HotspotOTA.h>
#include <helpers/esp32/RollbackGuard.h>

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

void hotspotOtaLoop() {
  RollbackGuard::poll();
  HotspotOTA::poll();
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
  // A reboot, power-off or erase part-way through an OTA leaves a half-written slot behind.
  if (isDestructive(context, command) && HotspotOTA::refuseWhileActive(reply)) return true;

  if (memcmp(command, "ver", 3) == 0) {
    sprintf(reply, "%s (%s) + ota (%s)", context.fw_version, context.fw_build_date,
            OTA_MOD_BUILD_DATE);
  } else if (memcmp(command, "start ota wan update", 21) == 0
             && (command[21] == 0 || command[21] == ' ')) {
    HotspotOtaConfig cfg;
    HotspotOTA::loadConfig(cfg);
    if (cfg.url[0] == 0) {
      strcpy(reply, "ERR: ota.fw.url not configured");
    } else {
      modBoardStartOtaFromUrl(cfg.url, reply);
    }
  } else if (memcmp(command, "start ota wan ", 14) == 0) {
    modBoardStartOtaFromUrl(&command[14], reply);
  } else if (memcmp(command, "start ota", 9) == 0) {
    // Upstream's own OTA path calls Update.begin(U_FLASH) -- a second writer to the same slot.
    if (!refuseOtaStart(reply)) return false;   // idle: upstream handles it unchanged
  } else if (strcmp(command, "ota cancel") == 0) {
    HotspotOTA::cancel(reply);
  } else if (memcmp(command, "ota wan join", 12) == 0) {
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
    if (HotspotOTA::isActive()) {
      strcpy(reply, "ERR: OTA active");
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
