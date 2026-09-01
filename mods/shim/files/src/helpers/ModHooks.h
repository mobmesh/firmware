#pragma once

#include <stdint.h>

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

// Every mod CLI command, dispatched from MyMesh before upstream's own chain runs. Version
// and build date are passed because only the call site can see the example's macros.
bool modHandleCliCommand(uint32_t sender_timestamp, char* command, char* reply,
                         const char* fw_version, const char* fw_build_date);

// The reverse direction. Bodies name the concrete `board` and `rtc_clock`, so an
// upstream refactor breaks this one file instead of silently unhooking a mod.
bool     modBoardRadioInit();
void     modBoardReboot();                     // does not return
uint16_t modBoardBattMilliVolts();
void     modBoardDeepSleep(uint32_t secs);     // does not return
void     modBoardInhibitSleep(bool inhibit);
uint32_t modClockGet();
void     modClockSet(uint32_t epoch);

// FEM LNA bypass, where the board declares MOBMESH_HAS_FEM_LNA. Elsewhere the
// query answers false and the setter does nothing.
bool modFemLnaAvailable();
bool modFemLnaGet();
void modFemLnaSet(bool on);
