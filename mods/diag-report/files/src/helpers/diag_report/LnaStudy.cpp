#include <helpers/diag_report/LnaStudy.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LNA_MIN_DECIDED     30
#define LNA_MIN_NO_BENEFIT  60
#define LNA_MIN_NEIGHBOURS  2
#define LNA_MIN_FLOOR_PAIRS 3
#define LNA_ALIVE_DB        10.0f
#define LNA_HELPS_PERCENT   10
#define LNA_CLEARER_DB      -1.0f
#define LNA_STRONG_DB       10.0f
#define LNA_STRONG_SAMPLES  3
#define LNA_LOST_HEARD      3
#define LNA_MIN_RUN_HOURS   4
#define LNA_EARLY_STOP_S    86400UL
#define LNA_SIGMA_FACTOR    0.82f

int lnaNeighbourIndex(LnaStudy& s, uint16_t id, uint8_t id_len) {
  for (int i = 0; i < s.neighbour_count; i++) {
    if (s.nb[i].id == id && s.nb[i].id_len == id_len) return i;
  }
  if (s.neighbour_count >= LNA_MAX_NEIGHBOURS) return -1;
  LnaNeighbour& n = s.nb[s.neighbour_count];
  memset(&n, 0, sizeof(n));
  n.id = id;
  n.id_len = id_len;
  return s.neighbour_count++;
}

int lnaPairWinner(const LnaBlock& on, const LnaBlock& off, uint8_t neighbours) {
  float score = 0;
  bool attributed = false;
  for (int i = 0; i < neighbours && i < LNA_MAX_NEIGHBOURS; i++) {
    int total = on.heard[i] + off.heard[i];
    if (total == 0) continue;
    attributed = true;
    score += (float)(on.heard[i] - off.heard[i]) / total;
  }
  if (!attributed) score = (float)on.unique - (float)off.unique;
  if (score > 0.001f) return 1;
  if (score < -0.001f) return -1;
  return 0;
}

void lnaAddPair(LnaStudy& s, const LnaBlock& on, const LnaBlock& off, int hour, int day) {
  int winner = lnaPairWinner(on, off, s.neighbour_count);
  if (winner > 0) s.wins[LNA_ON]++;
  else if (winner < 0) s.wins[LNA_OFF]++;
  else s.ties++;
  s.unique[LNA_ON] += on.unique;
  s.unique[LNA_OFF] += off.unique;
  if (on.floor_valid && off.floor_valid) {
    s.floor_sum[LNA_ON] += on.floor_dbm;
    s.floor_sum[LNA_OFF] += off.floor_dbm;
    s.floor_n[LNA_ON]++;
    s.floor_n[LNA_OFF]++;
  }
  for (int i = 0; i < s.neighbour_count; i++) {
    s.nb[i].heard[LNA_ON] += on.heard[i];
    s.nb[i].heard[LNA_OFF] += off.heard[i];
    s.nb[i].snr_q[LNA_ON] += on.snr_q[i];
    s.nb[i].snr_q[LNA_OFF] += off.snr_q[i];
    s.nb[i].snr_n[LNA_ON] += on.snr_n[i];
    s.nb[i].snr_n[LNA_OFF] += off.snr_n[i];
  }
  if (winner != 0 && hour >= 0 && hour < 24 && day >= 0) {
    if (day >= LNA_MAX_DAYS) day = LNA_MAX_DAYS - 1;
    uint8_t& cell = s.hour_wins[day][hour][winner > 0 ? LNA_ON : LNA_OFF];
    if (cell < 255) cell++;
  }
}

bool lnaSignificant(uint16_t wins, uint16_t decided) {
  if (decided == 0) return false;
  return wins >= decided / 2.0f + LNA_SIGMA_FACTOR * sqrtf((float)decided);
}

uint8_t lnaFloorPairs(const LnaStudy& s) {
  return s.floor_n[LNA_ON] < s.floor_n[LNA_OFF] ? s.floor_n[LNA_ON] : s.floor_n[LNA_OFF];
}

float lnaFloorShift(const LnaStudy& s) {
  if (s.floor_n[LNA_ON] == 0 || s.floor_n[LNA_OFF] == 0) return 0;
  return (float)s.floor_sum[LNA_ON] / s.floor_n[LNA_ON] - (float)s.floor_sum[LNA_OFF] / s.floor_n[LNA_OFF];
}

float lnaSnrLimit(uint8_t sf) {
  return -2.5f * ((int)sf - 4);
}

bool lnaClarity(const LnaStudy& s, uint8_t sf, float* delta_db) {
  float sum = 0;
  int strong = 0;
  for (int i = 0; i < s.neighbour_count; i++) {
    const LnaNeighbour& n = s.nb[i];
    if (n.snr_n[LNA_ON] < LNA_STRONG_SAMPLES || n.snr_n[LNA_OFF] < LNA_STRONG_SAMPLES) continue;
    float on_db = n.snr_q[LNA_ON] / 4.0f / n.snr_n[LNA_ON];
    float off_db = n.snr_q[LNA_OFF] / 4.0f / n.snr_n[LNA_OFF];
    if (off_db < lnaSnrLimit(sf) + LNA_STRONG_DB) continue;
    sum += on_db - off_db;
    strong++;
  }
  if (strong == 0) return false;
  *delta_db = sum / strong;
  return true;
}

int lnaLostNeighbour(const LnaStudy& s) {
  for (int i = 0; i < s.neighbour_count; i++) {
    if (s.nb[i].heard[LNA_ON] >= LNA_LOST_HEARD && s.nb[i].heard[LNA_OFF] == 0) return i;
  }
  return -1;
}

uint8_t lnaHeardNeighbours(const LnaStudy& s) {
  uint8_t count = 0;
  for (int i = 0; i < s.neighbour_count; i++) {
    if (s.nb[i].heard[LNA_ON] + s.nb[i].heard[LNA_OFF] > 0) count++;
  }
  return count;
}

int lnaPercentMore(const LnaStudy& s, uint8_t state) {
  uint32_t a = s.unique[state], b = s.unique[state ^ 1];
  if (b == 0) return a > 0 ? 100 : 0;
  return (int)(((int64_t)a - (int64_t)b) * 100 / (int64_t)b);
}

LnaVerdict lnaVerdict(const LnaStudy& s, uint8_t sf, bool final_result) {
  uint16_t decided = s.wins[LNA_ON] + s.wins[LNA_OFF];
  if (lnaFloorPairs(s) >= LNA_MIN_FLOOR_PAIRS && lnaFloorShift(s) < LNA_ALIVE_DB) return LNA_V_NOT_WORKING;
  if (decided < LNA_MIN_DECIDED || lnaHeardNeighbours(s) < LNA_MIN_NEIGHBOURS) {
    return final_result ? LNA_V_NOT_ENOUGH : LNA_V_NONE;
  }
  if (lnaLostNeighbour(s) >= 0) return LNA_V_KEEP_ON_LOST;
  float clarity = 0;
  bool strong = lnaClarity(s, sf, &clarity);
  if (lnaSignificant(s.wins[LNA_OFF], decided) || (strong && clarity <= LNA_CLEARER_DB)) return LNA_V_OVERLOADED;
  if (lnaSignificant(s.wins[LNA_ON], decided) && lnaPercentMore(s, LNA_ON) >= LNA_HELPS_PERCENT) return LNA_V_HELPS;
  if (decided >= LNA_MIN_NO_BENEFIT) return LNA_V_NO_BENEFIT;
  return final_result ? LNA_V_NOT_ENOUGH : LNA_V_NONE;
}

bool lnaShouldStop(const LnaStudy& s, uint8_t sf) {
  LnaVerdict v = lnaVerdict(s, sf, false);
  if (v == LNA_V_NOT_WORKING) return true;
  uint32_t earliest = s.requested_s < LNA_EARLY_STOP_S ? s.requested_s : LNA_EARLY_STOP_S;
  if (s.elapsed_s < earliest) return false;
  return v == LNA_V_KEEP_ON_LOST || v == LNA_V_OVERLOADED || v == LNA_V_HELPS;
}

LnaRun lnaOppositeRun(const LnaStudy& s, uint8_t overall_state) {
  LnaRun run = {false, 0, 0, (uint8_t)(overall_state ^ 1)};
  uint8_t opp = overall_state ^ 1;
  bool better[24];
  int any = 0;
  for (int h = 0; h < 24; h++) {
    int a = 0, b = 0;
    for (int d = 0; d < LNA_MAX_DAYS; d++) {
      a += s.hour_wins[d][h][opp];
      b += s.hour_wins[d][h][overall_state];
    }
    better[h] = a > b;
    any += better[h];
  }
  if (any == 0 || any == 24) return run;
  int best_len = 0, best_start = 0;
  for (int start = 0; start < 24; start++) {
    if (!better[start] || better[(start + 23) % 24]) continue;
    int len = 0;
    while (len < 24 && better[(start + len) % 24]) len++;
    if (len > best_len) {
      best_len = len;
      best_start = start;
    }
  }
  if (best_len < LNA_MIN_RUN_HOURS) return run;
  int total_a = 0, total_b = 0;
  for (int d = 0; d < LNA_MAX_DAYS; d++) {
    int a = 0, b = 0;
    for (int k = 0; k < best_len; k++) {
      int h = (best_start + k) % 24;
      a += s.hour_wins[d][h][opp];
      b += s.hour_wins[d][h][overall_state];
    }
    if (a + b > 0 && a < b) return run;
    total_a += a;
    total_b += b;
  }
  if (!lnaSignificant(total_a, total_a + total_b)) return run;
  run.found = true;
  run.start_hour = best_start;
  run.end_hour = (best_start + best_len) % 24;
  return run;
}

static void hourLabel(int hour, char* out, size_t len) {
  int h12 = hour % 12 == 0 ? 12 : hour % 12;
  snprintf(out, len, "%d%s", h12, hour < 12 ? "am" : "pm");
}

static void windowLabel(const LnaRun& run, int8_t tz_hours, char* out, size_t len) {
  if (tz_hours == 0) {
    snprintf(out, len, "%02d-%02d UTC", run.start_hour, run.end_hour);
    return;
  }
  char a[8], b[8];
  hourLabel((run.start_hour + tz_hours + 24) % 24, a, sizeof(a));
  hourLabel((run.end_hour + tz_hours + 24) % 24, b, sizeof(b));
  snprintf(out, len, "%s-%s", a, b);
}

static void neighbourLabel(const LnaNeighbour& n, char* out, size_t len) {
  if (n.id_len >= 2) snprintf(out, len, "%04x", n.id);
  else snprintf(out, len, "%02x", n.id & 0xFF);
}

static const char* stateName(uint8_t state) {
  return state == LNA_ON ? "on" : "off";
}

#define ADD(...) do { if (n < max_lines) snprintf(lines[n++], 48, __VA_ARGS__); } while (0)

static void addAdvice(uint8_t live, uint8_t best, char lines[][48], int max_lines, int& n) {
  if (live == best) {
    ADD("Now: %s, as advised. Nothing to change.", stateName(live));
    return;
  }
  ADD("Now: %s. To test %s:", stateName(live), stateName(best));
  ADD("tryset 86400 radio.fem.rxgain %s", stateName(best));
  ADD("then: tryset keep");
}

static void addRunNote(const LnaStudy& s, uint8_t overall, int8_t tz_hours, char lines[][48], int max_lines, int& n) {
  LnaRun run = lnaOppositeRun(s, overall);
  if (!run.found) return;
  char window[24];
  windowLabel(run, tz_hours, window, sizeof(window));
  if (run.state == LNA_OFF) {
    ADD("Off better %s:", window);
    ADD("a source on a schedule?");
  } else {
    ADD("On better %s", window);
    ADD("(the quieter hours).");
  }
  if (tz_hours == 0) ADD("set diag.tz for local times");
}

int lnaFormatResult(const LnaStudy& s, uint8_t live, uint8_t sf, int8_t tz_hours, char lines[][48], int max_lines) {
  int n = 0;
  uint16_t pairs = s.wins[LNA_ON] + s.wins[LNA_OFF] + s.ties;
  unsigned long hours = s.elapsed_s / 3600;
  switch ((LnaVerdict)s.result) {
    case LNA_V_NOT_WORKING:
      ADD("📶 LNA: 🔴 Not Working");
      ADD("Switching it changed the floor");
      ADD("by %d dB (expect ~20).", (int)lroundf(lnaFloorShift(s)));
      ADD("Likely FEM/LNA fault.");
      ADD("Study stopped. Now: %s.", stateName(live));
      break;
    case LNA_V_KEEP_ON_LOST:
    case LNA_V_HELPS: {
      ADD(live == LNA_ON ? "📶 LNA: 🟢 Keep It On" : "📶 LNA: 🟡 Keep It On");
      ADD("Studied %luh, %u pairs.", hours, pairs);
      ADD("On hears %d%% more packets", lnaPercentMore(s, LNA_ON));
      int lost = lnaLostNeighbour(s);
      if (lost >= 0) {
        char id[8];
        neighbourLabel(s.nb[lost], id, sizeof(id));
        ADD("from %u neighbours, and %s", lnaHeardNeighbours(s), id);
        ADD("is only heard with it on.");
      } else {
        ADD("from %u neighbours.", lnaHeardNeighbours(s));
      }
      addRunNote(s, LNA_ON, tz_hours, lines, max_lines, n);
      addAdvice(live, LNA_ON, lines, max_lines, n);
      break;
    }
    case LNA_V_OVERLOADED: {
      float clarity = 0;
      bool clearer = lnaClarity(s, sf, &clarity) && clarity <= LNA_CLEARER_DB;
      ADD(live == LNA_OFF ? "📶 LNA: 🟢 Overloaded (Study)" : "📶 LNA: 🔴 Overloaded");
      ADD("Studied %luh, %u pairs.", hours, pairs);
      ADD(clearer ? "Off hears %d%% more; clearer" : "Off hears %d%% more.", lnaPercentMore(s, LNA_OFF));
      if (clearer) ADD("signals with it off.");
      addRunNote(s, LNA_OFF, tz_hours, lines, max_lines, n);
      addAdvice(live, LNA_OFF, lines, max_lines, n);
      break;
    }
    case LNA_V_NO_BENEFIT:
      ADD(live == LNA_OFF ? "📶 LNA: 🟢 No Benefit Here (Study)" : "📶 LNA: 🟡 No Benefit Here");
      ADD("Studied %luh, %u pairs.", hours, pairs);
      ADD("On and off hear the same.");
      ADD("Off saves ~9 mA.");
      addRunNote(s, LNA_OFF, tz_hours, lines, max_lines, n);
      addAdvice(live, LNA_OFF, lines, max_lines, n);
      break;
    case LNA_V_NOT_ENOUGH:
      ADD("📶 LNA: ⚪ Not Enough Data");
      ADD("%luh, %u neighbours, %u pairs.", hours, lnaHeardNeighbours(s), pairs);
      ADD("Too quiet to judge.");
      ADD("Now: %s. Try again later.", stateName(live));
      break;
    default:
      ADD("📶 LNA: No Study Yet.");
      ADD("Start one: diag radio.lna start");
      break;
  }
  return n;
}

int lnaFormatProgress(const LnaStudy& s, uint8_t sf, char lines[][48], int max_lines) {
  int n = 0;
  uint16_t pairs = s.wins[LNA_ON] + s.wins[LNA_OFF] + s.ties;
  ADD("📶 LNA: ⚪ Studying, %lu of %luh",
      (unsigned long)(s.elapsed_s / 3600), (unsigned long)(s.requested_s / 3600));
  ADD("%u pairs, %u neighbours heard.", pairs, lnaHeardNeighbours(s));
  switch (lnaVerdict(s, sf, false)) {
    case LNA_V_KEEP_ON_LOST:
    case LNA_V_HELPS:
      ADD("Leaning: on helps (+%d%%).", lnaPercentMore(s, LNA_ON));
      break;
    case LNA_V_OVERLOADED:
      ADD("Leaning: overloaded (off +%d%%).", lnaPercentMore(s, LNA_OFF));
      break;
    case LNA_V_NOT_WORKING:
      ADD("LNA may not be working.");
      break;
    default:
      ADD("No clear difference yet.");
      break;
  }
  ADD("Not sure yet.");
  return n;
}

bool lnaParseTz(const char* text, int8_t* hours) {
  while (*text == ' ') text++;
  int sign = 1;
  if (*text == '+' || *text == '-') {
    if (*text == '-') sign = -1;
    text++;
  }
  if (*text < '0' || *text > '9') return false;
  int value = 0;
  while (*text >= '0' && *text <= '9') {
    value = value * 10 + (*text - '0');
    if (value > 99) return false;
    text++;
  }
  if (*text == 'h' || *text == 'H') text++;
  while (*text == ' ') text++;
  if (*text != 0) return false;
  value *= sign;
  if (value < -12 || value > 14) return false;
  *hours = (int8_t)value;
  return true;
}
