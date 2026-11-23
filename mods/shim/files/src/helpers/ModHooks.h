#pragma once

#include <stddef.h>
#include <stdint.h>

class RegionMap;
class CommonCLI;
class NodePrefs;
class ClientACL;
struct TransportKey;
namespace mesh { class Packet; class Mesh; class PacketManager; class Radio; }
class PhysicalLayer;

// One hook surface for every mod, so upstream's main.cpp carries one-line calls instead
// of each mod's code inline, and two mods never edit the same region of it.

struct ModCliContext {
  uint32_t sender_timestamp;
  const char* fw_version;
  const char* fw_build_date;
};

bool modRadioInit(const char* build_id);   // wraps upstream's radio_init()
void modLoop();                            // called first in loop()
bool modSleepBusy();                       // a mod job needs the main loop awake
bool modWantsPowerSaving();                // OR'd with the operator's powersaving_enabled
bool modPowerSavingOn();                   // the operator's powersaving_enabled OR modWantsPowerSaving()
void modRxPacketRead(float rssi, float snr);  // a packet was just read: its RSSI and SNR, before receive restarts
float modLastRssi();                       // the last read packet's RSSI, as getLastRSSI() reports it
float modLastSnr();                        // the last read packet's SNR, as getLastSNR() reports it
bool modLoadAllowed();                     // AND of mods that veto a heavy load; true when none
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
// AND of mods' flood-forward vetoes, true when none; scope_known is whether the packet resolved to a permitted region.
bool modAllowFlood(const mesh::Packet* packet, bool scope_known);
bool modResolveRegionName(const char* name, ModRegionMatch* out);
bool modRegionNameForKey(const uint8_t key[16], char* out, size_t capacity);
int modExportRegions(RegionMap* base, char* out, size_t capacity,
                     uint8_t excluded_flags);
bool modSkipRegionLoadLine(const char* line);   // true drops a `region load` line before it reaches the native map
size_t modOwnerInfoMarker(uint8_t out[4]);
void modObserveRecv(const mesh::Packet* packet, bool accepted,
                    const uint8_t scope_key[16]);
void modObserveTx(uint32_t packet_id, bool succeeded);

// Direct routing, one owner. Recv returns true to drop the packet; busy holds the long idle sleep; the setting events below it answer or apply; the rest return false.
enum ModDirectEvent {
  MOD_DIRECT_RECV,
  MOD_DIRECT_FORWARD,
  MOD_DIRECT_FORWARD_MULTI_ACK,
  MOD_DIRECT_BUSY,
  MOD_DIRECT_SENT,
  MOD_DIRECT_PRESENT,
  MOD_DIRECT_ENABLED,
  MOD_DIRECT_APPLY,
};
// The value MOD_DIRECT_APPLY takes in delay_millis.
enum ModDirectSetting { MOD_ROUTE_KEEP, MOD_ROUTE_OFF, MOD_ROUTE_ON };
bool modDirectEvent(ModDirectEvent event, const mesh::Packet* packet, uint32_t delay_millis);

// Receive mode, one owner (a duty-cycled receiver). The radio wrapper asks at each point; with no owner every answer is upstream's.
enum ModRxEvent {
  MOD_RX_START,                        // returns MOD_RX_PASS for upstream's startReceive(), else a RadioLib status
  MOD_RX_STOP,                         // before standby, transmit or channel scan
  MOD_RX_ASLEEP,                       // 1 while the radio sleeps: no SPI, no RSSI
  MOD_RX_SAMPLE_OK,                    // 1 when the wrapper may take a noise-floor sample
  MOD_RX_LOOP,                         // MOD_RX_RESTART and/or MOD_RX_FRESH_FLOOR
  MOD_RX_PARAMS,                       // radio settings about to change
  MOD_RX_PAUSE,                        // a real reading wanted: 0 read now, 1 wait while a continuous window opens and settles
  MOD_RX_RESUME,                       // the reading is done; the window closes
  MOD_RX_FLOOR_DONE,                   // the wrapper just published a noise floor
  MOD_RX_BUSY,                         // 1 holds the CPU awake: a calibration window or a pause is open
  MOD_RX_SLEEP,                        // the CPU is about to enter light sleep (radio nullptr)
  MOD_RX_WAKE,                         // the CPU is back from light sleep (radio nullptr)
};
enum { MOD_RX_PASS = 1, MOD_RX_RESTART = 1, MOD_RX_FRESH_FLOOR = 2 };
int16_t modRxMode(ModRxEvent event, PhysicalLayer* radio);
bool modDirectResend(const mesh::Packet* packet);
bool modDirectReply(const uint8_t* data, size_t len, uint32_t delay_millis);
uint32_t modAirtimeFor(int len);

// Every mod CLI command, dispatched from MyMesh before upstream's own chain runs. Version
// and build date are passed because only the call site can see the example's macros.
bool modHandleCliCommand(uint32_t sender_timestamp, char* command, char* reply,
                         const char* fw_version, const char* fw_build_date);

// Several replies to one command: queue each with modCliAddRow and leave `reply` empty.
void modCliBegin(uint32_t sender_timestamp);   // called by modHandleCliCommand before any mod
bool modCliAddRow(uint32_t sender_timestamp, const char* text);   // serial prints at once; false when full
// Next queued remote reply framed as TXT_TYPE_CLI_DATA, delay counted from upstream's reply delay; false when none left.
bool modCliNextRow(uint32_t sender_timestamp, uint8_t* frame, size_t* len, uint32_t* delay_millis);

// The reverse direction. Bodies name the concrete `board`, `rtc_clock` and `the_mesh`, so
// an upstream refactor breaks this one file instead of silently unhooking a mod.
bool     modBoardRadioInit();
void     modBoardReboot();                     // does not return
uint16_t modBoardBattMilliVolts();
float    modBoardMcuTemperature();             // upstream's own MCU reading, NAN where the board has none
void     modBoardDeepSleep(uint32_t secs);     // does not return
void     modBoardInhibitSleep(bool inhibit);
uint32_t modClockGet();
bool     modRadioIsReceiving();
float    modRadioCurrentRssi();
void     modBeforeAgcReset();                  // called by the radio wrapper just before each AGC reset
void     modRadioCounters(uint32_t* recv, uint32_t* errors);   // packets decoded and failed decodes since boot
uint32_t modAirtimeTotalSecs();                // transmit plus receive airtime since boot

// Raw RSSI sweeps, SX126x only; elsewhere available() is false and the rest do nothing.
bool  modRadioScanAvailable();
void  modRadioIdle();
void  modRadioSetScanBandwidth(float bw);
void  modRadioScanCalibrate(float mhz);
void  modRadioRawStandby();
void  modRadioRawTune(float mhz);
void  modRadioRawStartReceive();
float modRadioRawRssi();
void  modRadioSetParams(float freq, float bw, uint8_t sf, uint8_t cr);
void  modRadioResetAgc();
// SX126x AGC lock-up guard: register 0x08B5 (bit 0 = protection on) and the two bytes at 0x08BF (08 12 = receiver locked); false elsewhere.
bool  modRadioGuardRead(uint8_t* guard, uint8_t state[2]);
uint16_t modRadioIrqPeek();                // SX126x IRQ flags, read without clearing; 0 on other radios
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
  float rx_delay;
  float direct_tx_delay;
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

bool modPublisherKey(uint8_t out[32]);
bool modSignDetached(const uint8_t* data, size_t len, uint8_t signature[64]);
bool modSendGroup(const uint8_t* secret, uint8_t hash, const uint8_t* data,
                  size_t len, bool scoped, const uint8_t scope_key[16],
                  uint32_t* packet_id, uint32_t* airtime_ms);

// The live tempradio trial, or false when none runs. Both timers are consulted: pending_*
// are untested before the 2s apply and stale after the revert, which clears only the timer.
bool modTempRadioGet(float* freq, float* bw, uint8_t* sf, uint8_t* cr);

// What core's services act on, handed over once by MyMesh's constructor.
struct ModBindings {
  CommonCLI* cli;
  NodePrefs* prefs;
  mesh::Mesh* mesh;
  RegionMap* regions;
  mesh::PacketManager* packets;
  mesh::Radio* radio;
  // MyMesh::sendFloodScoped is not virtual, so the call site supplies a forwarder.
  void (*flood_scoped)(const TransportKey& scope, mesh::Packet* packet, uint32_t delay_millis, uint8_t path_hash_size);
  ClientACL* acl;                      // admins, for a CLI reply sent after the command returns
};
void modBind(const ModBindings* bindings);
void modTempRadioSet(float freq, float bw, uint8_t sf, uint8_t cr, bool live);   // snapshot after each command and loop pass
uint32_t modTxDone(const mesh::Packet* packet);   // the id modSendGroup gave this packet, or 0

// Zero-hop only: upstream schedules a flood advert every 47 hours, so it is not a caller's
// to spend. The delay lets a reply go out before the radio transmits.
void     modSendZeroHopAdvert(int delay_millis);

// FEM LNA bypass, where the board declares MOBMESH_HAS_FEM_LNA. Elsewhere the
// query answers false and the setter does nothing.
bool modFemLnaAvailable();
bool modFemLnaGet();
void modFemLnaSet(bool on);
