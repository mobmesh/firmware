#include <helpers/radio_diag/RfScan.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

#define FLOOR_RANK      6             // 25th percentile of the 26 band readings
#define BURST_DB        6.0f          // a sweep this far above its step's own average is a burst
#define QUIET_DB        4.0f
#define SQUARE_DB       2.0f
#define MAX_SQUARES     12
#define STEADY_DB       10.0f
#define WEAK_DB         6.0f
#define BURST_FINDING   3
#define MESH_FIRST      15            // steps 15-17 are the mesh channel itself
#define MESH_LAST       17
#define MESH_STEP       16

float rfScanBandFreq(float centre, int step) {
  return centre - 12.5f + step;
}

float rfScanStepFreq(float centre, int step) {
  return centre - 4.0f + step * 0.25f;
}

static float bandFloor(const RfScanData& d) {
  float sorted[RFSCAN_BAND_STEPS];
  memcpy(sorted, d.band, sizeof(sorted));
  for (int i = 1; i < RFSCAN_BAND_STEPS; i++) {
    float v = sorted[i];
    int j = i - 1;
    while (j >= 0 && sorted[j] > v) {
      sorted[j + 1] = sorted[j];
      j--;
    }
    sorted[j + 1] = v;
  }
  return sorted[FLOOR_RANK];
}

#define ADD(...) do { if (n < max_lines) snprintf(lines[n++], RFSCAN_LINE, __VA_ARGS__); } while (0)

static void analyse(const RfScanData& d, float avg[], float peak[], uint8_t hits[]) {
  for (int b = 0; b < RFSCAN_STEPS; b++) {
    avg[b] = 0;
    peak[b] = -200;
    for (int sw = 0; sw < RFSCAN_SWEEPS; sw++) {
      avg[b] += d.raw[sw][b] / RFSCAN_SWEEPS;
      if (d.raw[sw][b] > peak[b]) peak[b] = d.raw[sw][b];
    }
    hits[b] = 0;
    for (int sw = 0; sw < RFSCAN_SWEEPS; sw++) {
      if (d.raw[sw][b] - avg[b] >= BURST_DB) hits[b]++;
    }
  }
}

// Strongest steady step and most-bursting step off the mesh channel itself.
static void strongest(const float avg[], const float peak[], const uint8_t hits[], int* steady, int* burst) {
  float best_steady = -200;
  int burst_hits = 0;
  *steady = -1;
  *burst = -1;
  for (int b = 0; b < RFSCAN_STEPS; b++) {
    if (b >= MESH_FIRST && b <= MESH_LAST) continue;
    if (avg[b] > best_steady) {
      best_steady = avg[b];
      *steady = b;
    }
    if (hits[b] > burst_hits || (hits[b] == burst_hits && *burst >= 0 && peak[b] > peak[*burst])) {
      burst_hits = hits[b];
      *burst = b;
    }
  }
}

int rfScanSummary(const RfScanData& d, char* out, size_t len) {
  float avg[RFSCAN_STEPS], peak[RFSCAN_STEPS];
  uint8_t hits[RFSCAN_STEPS];
  analyse(d, avg, peak, hits);
  float fl = bandFloor(d);
  int steady, burst;
  strongest(avg, peak, hits, &steady, &burst);
  if (avg[MESH_STEP] - fl >= WEAK_DB) {
    snprintf(out, len, "mesh channel steady %d dB", (int)lroundf(avg[MESH_STEP] - fl));
    return 2;
  }
  // Steps 250 kHz either side of the channel; anything further out the receiver rejects.
  int edge = avg[MESH_FIRST] >= avg[MESH_LAST] ? MESH_FIRST : MESH_LAST;
  if (avg[edge] - fl >= STEADY_DB) {
    snprintf(out, len, "steady %.2f (%+.2f) %d dB", rfScanStepFreq(d.centre, edge), -4.0f + edge * 0.25f,
             (int)lroundf(avg[edge] - fl));
    return 1;
  }
  if (steady >= 0 && avg[steady] - fl >= STEADY_DB) {
    snprintf(out, len, "off-channel steady %.2f %d dB", rfScanStepFreq(d.centre, steady), (int)lroundf(avg[steady] - fl));
    return 0;
  }
  if (burst >= 0 && hits[burst] >= BURST_FINDING) {
    snprintf(out, len, "off-channel bursts %.2f %d/6", rfScanStepFreq(d.centre, burst), hits[burst]);
    return 0;
  }
  if (steady >= 0 && avg[steady] - fl >= WEAK_DB) {
    snprintf(out, len, "clear, weak %.2f %d dB", rfScanStepFreq(d.centre, steady), (int)lroundf(avg[steady] - fl));
    return 0;
  }
  snprintf(out, len, "band clear");
  return 0;
}

int rfScanFormat(const RfScanData& d, char lines[][RFSCAN_LINE], int max_lines) {
  static const char* squares[] = {"\U0001F7E9", "\U0001F7E8", "\U0001F7E7", "\U0001F7E5"};
  static const char* dots[] = {"⚫", "\U0001F7E1", "\U0001F7E0", "\U0001F534"};
  float avg[RFSCAN_STEPS], peak[RFSCAN_STEPS];
  uint8_t hits[RFSCAN_STEPS];
  analyse(d, avg, peak, hits);
  float fl = bandFloor(d);
  const char* lna = d.lna == RFSCAN_LNA_NA ? "n/a" : d.lna ? "on" : "off";

  int n = 0;
  ADD("near %.3f ±4M 6sw fl%d LNA %s", d.centre, (int)lroundf(fl), lna);
  ADD("⚫\U0001F7E1\U0001F7E0\U0001F534 bursts/6 ■ avg >4dB 2dB");
  int lo = (int)floorf(d.centre - 4.0f), hi = (int)floorf(d.centre + 4.0f), mesh_row = (int)floorf(d.centre);
  for (int m = lo; m <= hi; m++) {
    float v = -200;
    int h = 0;
    bool seen = false;
    for (int b = 0; b < RFSCAN_STEPS; b++) {
      float f = rfScanStepFreq(d.centre, b);
      if (f < m || f >= m + 1) continue;
      seen = true;
      if (avg[b] > v) v = avg[b];
      if (hits[b] > h) h = hits[b];
    }
    if (!seen || n >= max_lines) continue;
    float above = v - fl;
    int count = above < QUIET_DB ? 0 : 1 + (int)((above - QUIET_DB) / SQUARE_DB);
    if (count > MAX_SQUARES) count = MAX_SQUARES;
    char* line = lines[n++];
    int w = snprintf(line, RFSCAN_LINE, "%d %s ", m, dots[h == 0 ? 0 : h <= 2 ? 1 : h <= 4 ? 2 : 3]);
    if (count == 0) w += snprintf(line + w, RFSCAN_LINE - w, "·");
    for (int k = 0; k < count; k++) w += snprintf(line + w, RFSCAN_LINE - w, "%s", squares[k / 3 > 3 ? 3 : k / 3]);
    if (count > 0) w += snprintf(line + w, RFSCAN_LINE - w, " %d", (int)lroundf(v));
    if (m == mesh_row) snprintf(line + w, RFSCAN_LINE - w, " ◂mesh");
  }

  bool any = false;
  int steady, burst;
  strongest(avg, peak, hits, &steady, &burst);
  float best_steady = steady >= 0 ? avg[steady] : -200;
  int burst_hits = burst >= 0 ? hits[burst] : 0;
  if (avg[MESH_STEP] - fl >= WEAK_DB) {
    ADD("⚠️ mesh channel steady %d dB", (int)lroundf(avg[MESH_STEP] - fl));
    any = true;
  }
  if (steady >= 0 && best_steady - fl >= STEADY_DB) {
    ADD("⚠️ steady %.2f (%+.2f) %d dB", rfScanStepFreq(d.centre, steady), -4.0f + steady * 0.25f,
        (int)lroundf(best_steady - fl));
    any = true;
  }
  if (burst >= 0 && burst_hits >= BURST_FINDING) {
    ADD("⚠️ bursts %.2f (%+.2f) %d/6 pk%d", rfScanStepFreq(d.centre, burst), -4.0f + burst * 0.25f,
        burst_hits, (int)lroundf(peak[burst]));
    any = true;
  }
  int mesh_hits = hits[MESH_STEP];
  if (hits[MESH_FIRST] > mesh_hits) mesh_hits = hits[MESH_FIRST];
  if (hits[MESH_LAST] > mesh_hits) mesh_hits = hits[MESH_LAST];
  if (mesh_hits >= BURST_FINDING) ADD("mesh ch bursts %d/6 (mesh traffic?)", mesh_hits);
  if (!any && steady >= 0 && best_steady - fl >= WEAK_DB) {
    ADD("weak steady %.2f (%+.2f) %d dB", rfScanStepFreq(d.centre, steady), -4.0f + steady * 0.25f,
        (int)lroundf(best_steady - fl));
    any = true;
  }
  if (!any) ADD("nearby band clear");
  return n;
}
