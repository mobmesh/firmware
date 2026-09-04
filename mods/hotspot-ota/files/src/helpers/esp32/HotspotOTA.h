#pragma once
#include <Arduino.h>
#include <helpers/BaseChatMesh.h>   // MAX_TEXT_LEN

// Persisted separately from NodePrefs, its own SPIFFS file. sha256_hex is deliberately not a
// member here -- it's RAM-only (see setSha256Hex()/getSha256Hex()), not persisted with the rest.
struct HotspotOtaConfig {
  char ssid[32];
  char password[64];
  char url[147];   // sized to MAX_TEXT_LEN (160) minus "start ota wan " (14) plus null terminator --
                    // the LoRa CLI message budget, not a round number
};

// Catches MAX_TEXT_LEN drifting out from under url[]'s hand-derived size -- silently truncating
// long URLs again -- as a compile error instead.
static_assert(sizeof(HotspotOtaConfig::url) == MAX_TEXT_LEN - 14 + 1,
              "HotspotOtaConfig.url must track MAX_TEXT_LEN minus the \"start ota wan \" prefix");

// Reached from the CLI hook. Declared here rather than as a board virtual so this mod
// leaves upstream's class hierarchy alone; the body is in HotspotOtaBoard.cpp.
bool modBoardStartOtaFromUrl(const char* url, char reply[]);

namespace HotspotOTA {
  bool loadConfig(HotspotOtaConfig& cfg);
  bool saveConfig(const HotspotOtaConfig& cfg);

  bool start(const HotspotOtaConfig& cfg, char reply[]);
  bool cancel(char reply[]);
  bool isActive();

  // Interlock for destructive commands: true when the caller must refuse, with reply[] telling
  // the operator to cancel or to wait, whichever the current state actually allows.
  bool refuseWhileActive(char reply[]);

  // True while Arduino's global Update object is mid-write -- how upstream's own `start ota` upload
  // shows up. Not wired to the destructive-command interlock: a stalled upload would never clear.
  bool flashWriteInProgress();
  void status(char reply[]);
  void poll();

  void setPower(bool on);   // manual GPIO47 control, independent of the OTA service
  bool getPower();

  // Pins the whole-file hash -- RAM-only, cleared every boot. `clear` returns to the
  // image's own embedded digest.
  void setSha256Hex(const char* hex);
  const char* getSha256Hex();

  void setMarkerBypass(bool on);   // RAM-only, one-time -- see `set ota.fw.marker`

  // The running image's own MOBMESH trailer, for the OTA page's identity payload. Buffers must
  // hold 17, 13 and 25 bytes. False when the running image carries no trailer.
  bool runningMetadata(char* version, char* sha, char* role);

  // Pre-flight versions of the service's join/WAN-check steps.
  bool wifiConnect(char reply[]);
  void wifiDisconnect();
  bool checkWan(char reply[]);
}
