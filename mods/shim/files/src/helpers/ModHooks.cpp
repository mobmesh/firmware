#include <helpers/ModHooks.h>
#include <target.h>   // radio_init(), board, rtc_clock

bool modRadioInit(const char* build_id) {
  return radio_init();
}

void modLoop() {
}

bool modWantsPowerSaving() {
  return false;
}

// Reached through the concrete `board`, not a mesh::MainBoard*, so a virtual that
// upstream moves down into variant code still resolves here.
void     modBoardReboot()                  { board.reboot(); }
uint16_t modBoardBattMilliVolts()          { return board.getBattMilliVolts(); }
void     modBoardDeepSleep(uint32_t secs)  { board.enterDeepSleep(secs); }
void     modBoardInhibitSleep(bool inhibit) { board.setInhibitSleep(inhibit); }
uint32_t modClockGet()                     { return rtc_clock.getCurrentTime(); }
void     modClockSet(uint32_t epoch)       { rtc_clock.setCurrentTime(epoch); }

#ifdef MOBMESH_HAS_FEM_LNA
// Through the FEM object, not the board's own wrappers: upstream made those private on
// dev, while LoRaFEMControl's public API is identical on both refs.
bool modFemLnaAvailable()  { return board.loRaFEMControl.isLnaCanControl(); }
bool modFemLnaGet()        { return board.loRaFEMControl.isLNAEnabled(); }
void modFemLnaSet(bool on) {
  board.loRaFEMControl.setLNAEnable(on);
  board.loRaFEMControl.setRxModeEnable();   // re-arm RX, as upstream's own setter does
}
#else
bool modFemLnaAvailable()  { return false; }
bool modFemLnaGet()        { return false; }
void modFemLnaSet(bool on) { }
#endif
