#pragma once

#include <stdint.h>

// One hook surface for every mod, so upstream's main.cpp carries one-line calls instead
// of each mod's code inline, and two mods never edit the same region of it.

bool modRadioInit(const char* build_id);   // wraps upstream's radio_init()
void modLoop();                            // called first in loop()
bool modWantsPowerSaving();                // OR'd with the operator's powersaving_enabled

// The reverse direction. Bodies name the concrete `board` and `rtc_clock`, so an
// upstream refactor breaks this one file instead of silently unhooking a mod.
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
