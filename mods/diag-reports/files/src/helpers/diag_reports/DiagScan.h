#pragma once

#include <helpers/diag_reports/DiagOut.h>

// Interference scan arithmetic and text; the sweep itself runs in the integration. Pure.

#define SCAN_BAND_STEPS 26             // 1 MHz apart around the mesh frequency, for the floor
#define SCAN_STEPS      33             // wide: mesh frequency +/-4 MHz in 250 kHz steps
#define SCAN_NARROW     9              // narrow: +/-2 channel bandwidths in half-bandwidth steps
#define SCAN_SWEEPS     6              // two runs of three, back on the mesh between them
#define SCAN_MESH_STEP  16
#define SCAN_LNA_NA     -1

// What was on the channel during a scan or burst, from LoRa activity detection and the decoder.
enum TrafficKind : uint8_t {
  TRAFFIC_UNKNOWN,                     // no activity check ran
  TRAFFIC_QUIET,                       // no LoRa, no energy
  TRAFFIC_OURS,                        // LoRa that decoded: this mesh
  TRAFFIC_FOREIGN,                     // LoRa at the node's settings that never decoded: another network
  TRAFFIC_NOT_LORA,                    // energy with no LoRa: interference
};

struct TrafficCheck {
  uint16_t peeks;                      // IRQ flag reads during the window
  uint8_t preambles, orphans;          // preambles seen, and those that ended with no header: another sync word
  uint16_t decoded, errors;            // packets decoded and failed during the window
  uint8_t pending;                     // a preamble is up and no header has followed yet
};

#define TRAFFIC_FOREIGN_MIN 3          // orphan preambles in one capture before it counts toward another network

// One read of the radio's IRQ flags; preamble, header-valid and header-error bits as on the SX126x.
void trafficPeek(TrafficCheck& t, uint16_t irq);
uint8_t trafficClassify(const TrafficCheck& t, bool energy);
const char* trafficText(uint8_t kind);

struct ScanData {
  float centre;
  uint8_t band[SCAN_BAND_STEPS];       // half-dB steps below 0 dBm
  uint8_t raw[SCAN_SWEEPS][SCAN_STEPS];
  int8_t lna;                          // 1 on, 0 off, SCAN_LNA_NA where the board cannot switch it
  uint8_t sweeps;                      // sweeps taken so far
  uint8_t steps;                       // steps per sweep
  uint8_t mesh;                        // the step on the mesh frequency
  uint8_t chan;                        // steps either side still inside the channel
  uint8_t edge;                        // steps out to "beside the channel"
  float step_khz;
  float bw_khz;                        // receiver bandwidth for each reading
  TrafficCheck traffic;                // checked on the channel before and between the halves
};

// The manual scan: +/-4 MHz in 250 kHz steps, for the chart.
void scanSetWide(ScanData& d, float centre);
// Automatic scans: +/-2 channel bandwidths around the mesh frequency, in half-bandwidth steps.
void scanSetNarrow(ScanData& d, float centre, float channel_bw_khz);

enum ScanKind : uint8_t {
  SCAN_K_CLEAR,
  SCAN_K_MESH_STEADY,                  // energy on the mesh channel itself
  SCAN_K_EDGE_STEADY,                  // a steady signal 250 kHz beside the channel
  SCAN_K_OFF_STEADY,                   // steady and strong, further out
  SCAN_K_OFF_BURSTS,
  SCAN_K_WEAK,                         // 6-10 dB over the floor
};

struct ScanFinding {
  uint8_t kind;                        // ScanKind
  uint8_t level;                       // DIAG_PROBLEM on the channel, DIAG_CAUTION beside it, else DIAG_GOOD
  int16_t offset_khz;                  // distance of the finding from the mesh frequency
  int8_t db;                           // over the floor
  int8_t floor_dbm;
  uint8_t bursts;                      // sweeps out of 6 for a burst finding
  uint8_t mesh_bursts;                 // sweeps with a burst on the mesh channel
  bool blocker;                        // a steady signal 250 kHz to 1 MHz away, 10 dB or more up: enough to freeze the receiver
  int16_t blocker_khz;
  int8_t blocker_db;
  float peak_mhz;                      // where the energy on the channel peaks
  int16_t peak_khz;                    // that peak against the node's frequency; 0 = exactly on it
  uint8_t traffic;                     // TrafficKind on the channel during the scan
};

float scanBandFreq(float centre, int step);
float scanStepFreq(const ScanData& d, int step);
uint8_t scanPack(float dbm);
float scanUnpack(uint8_t v);
ScanFinding scanAnalyse(const ScanData& d);
// One-line form for the diag screen; returns the finding's level.
int scanSummary(const ScanFinding& f, char* out, size_t len);
void scanFormat(const ScanData& d, const DiagNote* note, DiagOut& o);
