// Host-test stand-in for the shim's hook surface: only the reverse hooks try-settings
// calls. The harness provides the bodies, so a case can script upstream's answers.
#pragma once
#include <stdint.h>

struct ModCliContext {
  uint32_t sender_timestamp;
  const char* fw_version;
  const char* fw_build_date;
};

uint32_t modClockGet();
void     modClockSet(uint32_t epoch);
void     modCliDispatch(uint32_t sender_timestamp, char* command, char* reply);
bool     modTempRadioGet(float* freq, float* bw, uint8_t* sf, uint8_t* cr);
