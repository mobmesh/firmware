#pragma once

#include <stddef.h>
#include <stdint.h>

// Pure rfscan arithmetic and text; the sweep itself runs in the integration through the shim's scan hooks.

#define RFSCAN_BAND_STEPS 26          // 1 MHz apart around the mesh frequency, for the floor
#define RFSCAN_STEPS      33          // mesh frequency +/-4 MHz in 250 kHz steps
#define RFSCAN_SWEEPS     6           // two runs of three, back on the mesh between them
#define RFSCAN_LINE       96
#define RFSCAN_LNA_NA     -1

struct RfScanData {
  float centre;
  float band[RFSCAN_BAND_STEPS];
  float raw[RFSCAN_SWEEPS][RFSCAN_STEPS];
  int8_t lna;                         // 1 on, 0 off, RFSCAN_LNA_NA where the board cannot switch it
};

float rfScanBandFreq(float centre, int step);
float rfScanStepFreq(float centre, int step);
// One-line finding for the diag summary: 2 warning, 1 caution, 0 clear.
int rfScanSummary(const RfScanData& d, char* out, size_t len);
int rfScanFormat(const RfScanData& d, char lines[][RFSCAN_LINE], int max_lines);
