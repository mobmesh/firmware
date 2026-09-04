// A free function rather than a board virtual, so hotspot-ota patches neither
// MeshCore.h nor ESP32Board.h and upstream owns its own class hierarchy.
#include <helpers/ModHooks.h>
#include <helpers/esp32/HotspotOTA.h>

// Same gate upstream puts around startOTAUpdate(): no WiFi stack, no URL update.
#if defined(WITH_HOTSPOT_OTA) && defined(ADMIN_PASSWORD)

#include <helpers/TxtDataHelpers.h>   // StrHelper -- there is no standalone StrHelper.h

bool modBoardStartOtaFromUrl(const char* url, char reply[]) {
  HotspotOtaConfig cfg;
  HotspotOTA::loadConfig(cfg);
  // Reject outright rather than silently truncate to a wrong-but-valid-looking URL.
  if (strlen(url) >= sizeof(cfg.url)) {
    sprintf(reply, "ERR: URL too long (max %d chars)", (int)sizeof(cfg.url) - 1);
    return false;
  }
  StrHelper::strncpy(cfg.url, url, sizeof(cfg.url));

  return HotspotOTA::start(cfg, reply);
}

#else
bool modBoardStartOtaFromUrl(const char* url, char reply[]) {
  strcpy(reply, "ERR: WAN OTA not supported");
  return false;
}
#endif
