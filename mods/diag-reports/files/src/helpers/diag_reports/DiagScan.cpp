#include <helpers/diag_reports/DiagScan.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

#define FLOOR_RANK      6              // 25th percentile of the 26 band readings
#define BURST_DB        6.0f           // a sweep this far above its step's own average is a burst
#define QUIET_DB        4.0f
#define SQUARE_DB       2.0f
#define MAX_SQUARES     12
#define STEADY_DB       10.0f
#define WEAK_DB         6.0f
#define BURST_FINDING   3
#define BLOCKER_KHZ     1000.0f        // a blocker sits within 1 MHz of the channel

float scanBandFreq(float centre, int step) {
  return centre - 12.5f + step;
}

void scanSetWide(ScanData& d, float centre) {
  d.centre = centre;
  d.steps = SCAN_STEPS;
  d.mesh = SCAN_MESH_STEP;
  d.chan = 1;
  d.edge = 1;
  d.step_khz = 250.0f;
  d.bw_khz = 250.0f;
}

void scanSetNarrow(ScanData& d, float centre, float channel_bw_khz) {
  d.centre = centre;
  d.steps = SCAN_NARROW;
  d.mesh = SCAN_NARROW / 2;
  d.chan = 1;
  d.edge = 2;
  d.step_khz = channel_bw_khz / 2;
  d.bw_khz = channel_bw_khz;
}

static void defaults(const ScanData& d, int* steps, int* mesh) {
  *steps = d.steps ? d.steps : SCAN_STEPS;
  *mesh = d.steps ? d.mesh : SCAN_MESH_STEP;
}

void trafficPeek(TrafficCheck& t, uint16_t irq) {
  const uint16_t PREAMBLE = 0x0004, HEADER = 0x0010 | 0x0020;
  if (t.peeks < 0xFFFF) t.peeks++;
  if (t.pending) {
    if (irq & HEADER) t.pending = 0;
    else if (!(irq & PREAMBLE)) {
      t.pending = 0;
      if (t.orphans < 255) t.orphans++;
    }
  } else if ((irq & PREAMBLE) && !(irq & HEADER)) {
    t.pending = 1;
    if (t.preambles < 255) t.preambles++;
  }
}

uint8_t trafficClassify(const TrafficCheck& t, bool energy) {
  if (t.peeks == 0 && t.decoded == 0) return TRAFFIC_UNKNOWN;
  if (t.decoded > 0) return TRAFFIC_OURS;
  // Preambles at our spreading factor and bandwidth that never reached a header carry another sync word.
  if (t.orphans >= TRAFFIC_FOREIGN_MIN) return TRAFFIC_FOREIGN;
  // A preamble too few to name: LoRa of some kind, so never called interference.
  if (t.preambles > 0) return TRAFFIC_UNKNOWN;
  return energy ? TRAFFIC_NOT_LORA : TRAFFIC_QUIET;
}

const char* trafficText(uint8_t kind) {
  switch (kind) {
    case TRAFFIC_OURS: return "our mesh traffic";
    case TRAFFIC_FOREIGN: return "another LoRa network";
    case TRAFFIC_NOT_LORA: return "not LoRa: interference";
    case TRAFFIC_QUIET: return "quiet";
    default: return "";
  }
}

float scanStepFreq(const ScanData& d, int step) {
  int steps, mesh;
  defaults(d, &steps, &mesh);
  float step_khz = d.step_khz > 0 ? d.step_khz : 250.0f;
  return d.centre + (step - mesh) * step_khz / 1000.0f;
}

uint8_t scanPack(float dbm) {
  long v = lroundf(-dbm * 2);
  return (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
}

float scanUnpack(uint8_t v) {
  return -0.5f * v;
}

static float bandFloor(const ScanData& d) {
  uint8_t sorted[SCAN_BAND_STEPS];
  memcpy(sorted, d.band, sizeof(sorted));
  // Packed values grow as the signal falls, so the quietest readings sort last.
  for (int i = 1; i < SCAN_BAND_STEPS; i++) {
    uint8_t v = sorted[i];
    int j = i - 1;
    while (j >= 0 && sorted[j] < v) {
      sorted[j + 1] = sorted[j];
      j--;
    }
    sorted[j + 1] = v;
  }
  return scanUnpack(sorted[FLOOR_RANK]);
}

struct Steps {
  float avg[SCAN_STEPS], peak[SCAN_STEPS], floor;
  uint8_t hits[SCAN_STEPS];
  int steady, burst;                   // strongest steady and most-bursting step off the mesh channel
  int steps, mesh, chan, edge;
  float step_khz;
};

static void measure(const ScanData& d, Steps& s) {
  int sweeps = d.sweeps ? d.sweeps : SCAN_SWEEPS;
  defaults(d, &s.steps, &s.mesh);
  s.chan = d.steps ? d.chan : 1;
  s.edge = d.steps ? d.edge : 1;
  s.step_khz = d.step_khz > 0 ? d.step_khz : 250.0f;
  s.floor = bandFloor(d);
  for (int b = 0; b < s.steps; b++) {
    s.avg[b] = 0;
    s.peak[b] = -200;
    for (int sw = 0; sw < sweeps; sw++) {
      float v = scanUnpack(d.raw[sw][b]);
      s.avg[b] += v / sweeps;
      if (v > s.peak[b]) s.peak[b] = v;
    }
    s.hits[b] = 0;
    for (int sw = 0; sw < sweeps; sw++) {
      if (scanUnpack(d.raw[sw][b]) - s.avg[b] >= BURST_DB) s.hits[b]++;
    }
  }
  float best = -200;
  int burst_hits = 0;
  s.steady = s.burst = -1;
  for (int b = 0; b < s.steps; b++) {
    if (b >= s.mesh - s.chan && b <= s.mesh + s.chan) continue;
    if (s.avg[b] > best) {
      best = s.avg[b];
      s.steady = b;
    }
    if (s.hits[b] > burst_hits || (s.hits[b] == burst_hits && s.burst >= 0 && s.peak[b] > s.peak[s.burst])) {
      burst_hits = s.hits[b];
      s.burst = b;
    }
  }
}

static int16_t offsetKhz(const Steps& s, int step) {
  return (int16_t)lroundf((step - s.mesh) * s.step_khz);
}

static ScanFinding finding(const Steps& s) {
  ScanFinding f;
  memset(&f, 0, sizeof(f));
  f.floor_dbm = (int8_t)lroundf(s.floor);
  f.level = DIAG_GOOD;
  for (int b = s.mesh - s.chan; b <= s.mesh + s.chan; b++) if (s.hits[b] > f.mesh_bursts) f.mesh_bursts = s.hits[b];
  // The strongest steady step beside the channel and within 1 MHz is what can freeze the receiver.
  for (int b = 0; b < s.steps; b++) {
    int away = b > s.mesh ? b - s.mesh : s.mesh - b;
    if (away < s.edge || away * s.step_khz > BLOCKER_KHZ) continue;
    float up = s.avg[b] - s.floor;
    // Spill from a carrier on the channel itself is not a blocker: one must stand above the channel's own level.
    if (up >= STEADY_DB && s.avg[b] >= s.avg[s.mesh] + WEAK_DB && (!f.blocker || up > f.blocker_db)) {
      f.blocker = true;
      f.blocker_khz = offsetKhz(s, b);
      f.blocker_db = (int8_t)lroundf(up);
    }
  }
  int edge = s.avg[s.mesh - s.edge] >= s.avg[s.mesh + s.edge] ? s.mesh - s.edge : s.mesh + s.edge;
  // Where the energy within the channel peaks: exactly our frequency, or a measured offset inside it.
  int peak = s.mesh;
  for (int b = s.mesh - s.chan; b <= s.mesh + s.chan; b++) if (s.avg[b] > s.avg[peak]) peak = b;
  f.peak_khz = offsetKhz(s, peak);
  if (s.avg[s.mesh] - s.floor >= WEAK_DB || s.avg[peak] - s.floor >= WEAK_DB) {
    f.kind = SCAN_K_MESH_STEADY;
    f.level = DIAG_PROBLEM;
    f.db = (int8_t)lroundf(s.avg[peak] - s.floor);
  } else if (s.avg[edge] - s.floor >= STEADY_DB) {
    f.kind = SCAN_K_EDGE_STEADY;
    f.level = DIAG_CAUTION;
    f.offset_khz = offsetKhz(s, edge);
    f.db = (int8_t)lroundf(s.avg[edge] - s.floor);
  } else if (s.steady >= 0 && s.avg[s.steady] - s.floor >= STEADY_DB) {
    f.kind = SCAN_K_OFF_STEADY;
    f.offset_khz = offsetKhz(s, s.steady);
    f.db = (int8_t)lroundf(s.avg[s.steady] - s.floor);
  } else if (s.burst >= 0 && s.hits[s.burst] >= BURST_FINDING) {
    f.kind = SCAN_K_OFF_BURSTS;
    f.offset_khz = offsetKhz(s, s.burst);
    f.bursts = s.hits[s.burst];
  } else if (s.steady >= 0 && s.avg[s.steady] - s.floor >= WEAK_DB) {
    f.kind = SCAN_K_WEAK;
    f.offset_khz = offsetKhz(s, s.steady);
    f.db = (int8_t)lroundf(s.avg[s.steady] - s.floor);
  }
  return f;
}

ScanFinding scanAnalyse(const ScanData& d) {
  Steps s;
  measure(d, s);
  ScanFinding f = finding(s);
  f.peak_mhz = d.centre + f.peak_khz / 1000.0f;
  bool energy = f.kind == SCAN_K_MESH_STEADY || f.mesh_bursts >= 3;
  f.traffic = trafficClassify(d.traffic, energy);
  return f;
}

// "250k" or "1.5M" from a distance in kHz.
static void offsetText(int16_t khz, char* out, size_t len) {
  int a = khz < 0 ? -khz : khz;
  if (a < 1000) snprintf(out, len, "%dk", a);
  else if (a % 1000 == 0) snprintf(out, len, "%dM", a / 1000);
  else snprintf(out, len, "%d.%dM", a / 1000, a % 1000 / 100);
}

int scanSummary(const ScanFinding& f, char* out, size_t len) {
  char off[10];
  offsetText(f.offset_khz, off, sizeof(off));
  switch (f.kind) {
    case SCAN_K_MESH_STEADY:
      if (f.peak_khz == 0) snprintf(out, len, "on our freq, %d dB", f.db);
      else snprintf(out, len, "%+dk off ours, %d dB", f.peak_khz, f.db);
      break;
    case SCAN_K_EDGE_STEADY: snprintf(out, len, "strong %s off ch", off); break;
    case SCAN_K_OFF_STEADY: snprintf(out, len, "steady %s off, rejected", off); break;
    case SCAN_K_OFF_BURSTS: snprintf(out, len, "bursts %s off, rejected", off); break;
    default: snprintf(out, len, "nothing close"); break;
  }
  return f.level;
}

void scanFormat(const ScanData& d, const DiagNote* note, DiagOut& o) {
  static const char* const squares[] = {"\U0001F7E9", "\U0001F7E8", "\U0001F7E7", "\U0001F7E5"};
  static const char* const dots[] = {"⚫", "\U0001F7E1", "\U0001F7E0", "\U0001F534"};
  Steps s;
  measure(d, s);
  if (d.lna == SCAN_LNA_NA) outLine(o, "near %.3f ±4M 6sw fl%d", d.centre, (int)lroundf(s.floor));
  else outLine(o, "near %.3f ±4M 6sw fl%d LNA %s", d.centre, (int)lroundf(s.floor), d.lna ? "on" : "off");
  outLine(o, "⚫\U0001F7E1\U0001F7E0\U0001F534 bursts/6 ■ avg >4dB 2dB");
  int lo = (int)floorf(d.centre - 4.0f), hi = (int)floorf(d.centre + 4.0f), mesh_row = (int)floorf(d.centre);
  for (int m = lo; m <= hi; m++) {
    float v = -200;
    int h = 0;
    bool seen = false;
    for (int b = 0; b < s.steps; b++) {
      float f = scanStepFreq(d, b);
      if (f < m || f >= m + 1) continue;
      seen = true;
      if (s.avg[b] > v) v = s.avg[b];
      if (s.hits[b] > h) h = s.hits[b];
    }
    if (!seen) continue;
    float above = v - s.floor;
    int count = above < QUIET_DB ? 0 : 1 + (int)((above - QUIET_DB) / SQUARE_DB);
    if (count > MAX_SQUARES) count = MAX_SQUARES;
    char line[96];
    int w = snprintf(line, sizeof(line), "%d %s ", m, dots[h == 0 ? 0 : h <= 2 ? 1 : h <= 4 ? 2 : 3]);
    if (count == 0) w += snprintf(line + w, sizeof(line) - w, "·");
    for (int k = 0; k < count; k++) w += snprintf(line + w, sizeof(line) - w, "%s", squares[k / 3 > 3 ? 3 : k / 3]);
    if (count > 0) w += snprintf(line + w, sizeof(line) - w, " %d", (int)lroundf(v));
    if (m == mesh_row) snprintf(line + w, sizeof(line) - w, " ◂mesh");
    outLine(o, "%s", line);
  }

  bool any = false;
  float best = s.steady >= 0 ? s.avg[s.steady] : -200;
  int burst_hits = s.burst >= 0 ? s.hits[s.burst] : 0;
  if (s.avg[s.mesh] - s.floor >= WEAK_DB) {
    outLine(o, "⚠️ mesh channel steady %d dB", (int)lroundf(s.avg[s.mesh] - s.floor));
    any = true;
  }
  if (s.steady >= 0 && best - s.floor >= STEADY_DB) {
    outLine(o, "⚠️ steady %.2f (%+.2f) %d dB", scanStepFreq(d, s.steady), offsetKhz(s, s.steady) / 1000.0f, (int)lroundf(best - s.floor));
    any = true;
  }
  if (s.burst >= 0 && burst_hits >= BURST_FINDING) {
    outLine(o, "⚠️ bursts %.2f (%+.2f) %d/6 pk%d", scanStepFreq(d, s.burst), offsetKhz(s, s.burst) / 1000.0f, burst_hits,
            (int)lroundf(s.peak[s.burst]));
    any = true;
  }
  ScanFinding f = finding(s);
  // The activity checks and the decoder say what the energy on the channel was, instead of a guess.
  uint8_t traffic = trafficClassify(d.traffic, f.mesh_bursts >= BURST_FINDING || f.kind == SCAN_K_MESH_STEADY);
  if (f.mesh_bursts >= BURST_FINDING) {
    if (traffic != TRAFFIC_UNKNOWN && traffic != TRAFFIC_QUIET) outLine(o, "mesh ch bursts %d/6: %s", f.mesh_bursts, trafficText(traffic));
    else outLine(o, "mesh ch bursts %d/6", f.mesh_bursts);
  } else if (f.kind == SCAN_K_MESH_STEADY && traffic != TRAFFIC_UNKNOWN && traffic != TRAFFIC_QUIET) {
    outLine(o, "On channel: %s", trafficText(traffic));
  }
  if (d.traffic.peeks) outLine(o, "LoRa %u preambles, %u no header, %u decoded", d.traffic.preambles, d.traffic.orphans, d.traffic.decoded);
  if (!any && s.steady >= 0 && best - s.floor >= WEAK_DB) {
    outLine(o, "weak steady %.2f (%+.2f) %d dB", scanStepFreq(d, s.steady), offsetKhz(s, s.steady) / 1000.0f, (int)lroundf(best - s.floor));
    any = true;
  }
  if (!any) outLine(o, "Nearby Band Clear");
  if (note && note->n) {
    outSep(o);
    notePrint(note, o);
  }
}
