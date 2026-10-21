#pragma once
#include <Arduino.h>
#include <helpers/BaseChatMesh.h>   // MAX_TEXT_LEN

// Persisted separately from NodePrefs, its own SPIFFS file.
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

  void setMarkerBypass(bool on);   // RAM-only, one-time -- see `set ota.fw.marker`

  // The running image's own MOBMESH trailer, for the OTA page's identity payload. Buffers must
  // hold 13, 9 and 25 bytes. False when the running image carries no trailer.
  bool runningMetadata(char* version, char* sha, char* role);

  // Pre-flight versions of the service's join/WAN-check steps.
  bool wifiConnect(char reply[]);
  bool wifiDisconnect();
  void radioStatus(char reply[]);
  bool checkWan(char reply[]);
  bool verifyWan(char reply[]);
  void verifyCommand(bool refresh, char reply[]);   // `ota wan verify [refresh]`
  void wanHealth(char reply[]);
  bool wanProven();
  bool resetWanHealth();

  enum class WanRun : uint8_t { Pending, Unknown, WanFailed, NtpFailed, Verified, RestoreFault };
  bool startWanVerify(uint32_t& run);
  WanRun wanVerifyResult(uint32_t run);

  // `ota wan survey`: one asynchronous scan, paged from the scan API's own result set.
  void survey(const char* argument, char reply[]);
  void surveyPoll();

  // Timed automatic updates (`ota.wan.auto`).
  bool autoSet(const char* key, const char* value, char reply[]);
  bool autoGet(const char* key, char reply[]);
  void autoTick(bool ap_busy);
  void setAutoBlocked(bool blocked);   // written only by sync-settings, once per loop
  void onImageConfirmed();
  void onImageRejected(bool crash);
  bool runningIsKnownGood();
  void noteInstall();   // an install committed: record recency and arm a pending retry
}
