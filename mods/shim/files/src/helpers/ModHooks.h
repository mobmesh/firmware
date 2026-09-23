#pragma once

#include <stddef.h>
#include <stdint.h>

class RegionMap;
namespace mesh { class Packet; }

// One hook surface for every mod, so upstream's main.cpp carries one-line calls instead
// of each mod's code inline, and two mods never edit the same region of it.

struct ModCliContext {
  uint32_t sender_timestamp;
  const char* fw_version;
  const char* fw_build_date;
};

bool modRadioInit(const char* build_id);   // wraps upstream's radio_init()
void modLoop();                            // called first in loop()
bool modWantsPowerSaving();                // OR'd with the operator's powersaving_enabled
void modBeforeDeepSleep();                 // before a mod's own sleep while the radio is not running

struct ModRegionMatch {
  uint8_t state;
  uint8_t key[16];
};

enum {
  MOD_REGION_NONE,
  MOD_REGION_ALLOW,
  MOD_REGION_DENY,
};

bool modResolveRegion(mesh::Packet* packet, RegionMap* base, ModRegionMatch* out);
// Vetoes a flood forward; scope_known is whether the packet resolved to a permitted region.
bool modAllowFlood(const mesh::Packet* packet, bool scope_known);
bool modResolveRegionName(const char* name, ModRegionMatch* out);
bool modRegionNameForKey(const uint8_t key[16], char* out, size_t capacity);
int modExportRegions(RegionMap* base, char* out, size_t capacity,
                     uint8_t excluded_flags);
void modObserveRecv(const mesh::Packet* packet, bool accepted,
                    const uint8_t scope_key[16]);
void modObserveTx(uint32_t packet_id, bool succeeded);

// Every mod CLI command, dispatched from MyMesh before upstream's own chain runs. Version
// and build date are passed because only the call site can see the example's macros.
bool modHandleCliCommand(uint32_t sender_timestamp, char* command, char* reply,
                         const char* fw_version, const char* fw_build_date);

// The reverse direction. Bodies name the concrete `board`, `rtc_clock` and `the_mesh`, so
// an upstream refactor breaks this one file instead of silently unhooking a mod.
bool     modBoardRadioInit();
void     modBoardReboot();                     // does not return
uint16_t modBoardBattMilliVolts();
void     modBoardDeepSleep(uint32_t secs);     // does not return
void     modBoardInhibitSleep(bool inhibit);
uint32_t modClockGet();
void     modClockSet(uint32_t epoch);

// Straight into upstream's CLI chain, bypassing mod dispatch, so validation stays in
// upstream's own parser rather than reaching its protected handleGetCmd/handleSetCmd.
void modCliDispatch(uint32_t sender_timestamp, char* command, char* reply);

struct ModPolicyValues {
  uint8_t flood_max;
  uint8_t flood_max_unscoped;
  uint8_t flood_max_advert;
  uint8_t advert_interval;
  uint8_t flood_advert_interval;
  uint8_t path_hash_mode;
  uint8_t loop_detect;
  uint8_t multi_acks;
  float airtime_factor;
  float tx_delay_factor;
  uint8_t agc_reset_interval;
};

bool modPolicyRead(ModPolicyValues* out);

struct ModRadioValues {
  float freq;
  float bw;
  uint8_t sf;
  uint8_t cr;
};

bool modRadioPrefsGet(ModRadioValues* out);
bool modRandomFill(uint8_t* out, size_t len);

bool modPublisherKey(uint8_t out[32]);
bool modSignDetached(const uint8_t* data, size_t len, uint8_t signature[64]);
bool modSendGroup(const uint8_t* secret, uint8_t hash, const uint8_t* data,
                  size_t len, bool scoped, const uint8_t scope_key[16],
                  uint32_t* packet_id, uint32_t* airtime_ms);

// The live tempradio trial, or false when none runs. Both timers are consulted: pending_*
// are untested before the 2s apply and stale after the revert, which clears only the timer.
bool modTempRadioGet(float* freq, float* bw, uint8_t* sf, uint8_t* cr);

// Zero-hop only: upstream schedules a flood advert every 47 hours, so it is not a caller's
// to spend. The delay lets a reply go out before the radio transmits.
void     modSendZeroHopAdvert(int delay_millis);

// FEM LNA bypass, where the board declares MOBMESH_HAS_FEM_LNA. Elsewhere the
// query answers false and the setter does nothing.
bool modFemLnaAvailable();
bool modFemLnaGet();
void modFemLnaSet(bool on);
