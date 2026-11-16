#include <helpers/diag_reports/DiagLna.h>

#include <math.h>
#include <stdio.h>
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
  s.recv[LNA_ON] += on.recv;
  s.recv[LNA_OFF] += off.recv;
  s.errors[LNA_ON] += on.errors;
  s.errors[LNA_OFF] += off.errors;
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
  uint16_t n = s.floor_n[LNA_ON] < s.floor_n[LNA_OFF] ? s.floor_n[LNA_ON] : s.floor_n[LNA_OFF];
  return n > 255 ? 255 : (uint8_t)n;
}

float lnaFloorShift(const LnaStudy& s) {
  if (s.floor_n[LNA_ON] == 0 || s.floor_n[LNA_OFF] == 0) return 0;
  return (float)s.floor_sum[LNA_ON] / s.floor_n[LNA_ON] - (float)s.floor_sum[LNA_OFF] / s.floor_n[LNA_OFF];
}

static float snrLimit(uint8_t sf) {
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
    if (off_db < snrLimit(sf) + LNA_STRONG_DB) continue;
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
  if (decided < LNA_MIN_DECIDED || lnaHeardNeighbours(s) < LNA_MIN_NEIGHBOURS) return final_result ? LNA_V_NOT_ENOUGH : LNA_V_NONE;
  // Freezes only ever with the LNA on, over enough pairs, mean the LNA lets a strong neighbour lock the receiver.
  bool freeze_on = s.freezes[LNA_ON] >= 2 && s.freezes[LNA_OFF] == 0;
  if (lnaLostNeighbour(s) >= 0 && !freeze_on) return LNA_V_KEEP_ON_LOST;
  if (freeze_on) return LNA_V_OVERLOADED;
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
  if (!lnaSignificant((uint16_t)total_a, (uint16_t)(total_a + total_b))) return run;
  run.found = true;
  run.start_hour = (uint8_t)best_start;
  run.end_hour = (uint8_t)((best_start + best_len) % 24);
  return run;
}

static uint32_t runMask(const LnaRun& run) {
  if (!run.found) return 0;
  uint32_t mask = 0;
  for (int h = run.start_hour; h != run.end_hour; h = (h + 1) % 24) mask |= 1UL << h;
  return mask;
}

static int16_t rate(uint32_t recv, uint32_t errors) {
  uint32_t total = recv + errors;
  return total < 50 ? -1 : (int16_t)((uint64_t)errors * 1000 / total);
}

LnaFacts lnaFacts(const LnaStudy& s, uint8_t sf) {
  LnaFacts f;
  memset(&f, 0, sizeof(f));
  f.result = s.result;
  f.known = !s.active && s.result != LNA_V_NONE;
  f.percent_on = (int16_t)lnaPercentMore(s, LNA_ON);
  if (lnaFloorPairs(s) >= LNA_MIN_FLOOR_PAIRS) f.floor_shift = (int8_t)lroundf(lnaFloorShift(s));
  f.freezes[0] = s.freezes[0];
  f.freezes[1] = s.freezes[1];
  f.err_tenths[0] = rate(s.recv[0], s.errors[0]);
  f.err_tenths[1] = rate(s.recv[1], s.errors[1]);
  f.off_better_utc = runMask(lnaOppositeRun(s, LNA_ON));
  f.on_better_utc = runMask(lnaOppositeRun(s, LNA_OFF));
  f.pairs = s.wins[0] + s.wins[1] + s.ties;
  f.hours = s.elapsed_s / 3600;
  // Neighbours heard at least half again as often with it on, strongest gain first.
  for (int pick = 0; pick < 3; pick++) {
    int best = -1;
    float best_ratio = 1.5f;
    for (int i = 0; i < s.neighbour_count; i++) {
      const LnaNeighbour& n = s.nb[i];
      if (n.heard[LNA_ON] < 10) continue;
      bool taken = false;
      for (int k = 0; k < f.need_n; k++) taken |= f.need_id[k] == n.id && f.need_len[k] == n.id_len;
      if (taken) continue;
      float ratio = n.heard[LNA_OFF] ? (float)n.heard[LNA_ON] / n.heard[LNA_OFF] : 99.0f;
      if (ratio >= best_ratio) {
        best_ratio = ratio;
        best = i;
      }
    }
    if (best < 0) break;
    f.need_id[f.need_n] = s.nb[best].id;
    f.need_len[f.need_n] = s.nb[best].id_len;
    f.need_n++;
  }
  (void)sf;
  return f;
}

int lnaAdvice(uint8_t result) {
  switch (result) {
    case LNA_V_KEEP_ON_LOST:
    case LNA_V_HELPS: return LNA_ON;
    case LNA_V_OVERLOADED:
    case LNA_V_NO_BENEFIT: return LNA_OFF;
    default: return -1;
  }
}

static const char* stateName(uint8_t state) {
  return state == LNA_ON ? "on" : "off";
}

static void addAdvice(uint8_t live, uint8_t best, DiagOut& o) {
  if (live == best) {
    outLine(o, "Now: %s, as advised.", stateName(live));
    return;
  }
  outLine(o, "Now: %s. To test %s:", stateName(live), stateName(best));
  outLine(o, "tryset 86400 radio.fem.rxgain %s", stateName(best));
  outLine(o, "then: tryset keep");
}

static void addRunNote(const LnaStudy& s, uint8_t overall, int8_t tz_hours, DiagOut& o) {
  LnaRun run = lnaOppositeRun(s, overall);
  if (!run.found) return;
  char window[24];
  if (tz_hours == 0) {
    snprintf(window, sizeof(window), "%02d-%02d UTC", run.start_hour, run.end_hour);
  } else {
    uint8_t start = (uint8_t)((run.start_hour + tz_hours + 24) % 24);
    diagSpanText(start, (uint8_t)((run.end_hour - run.start_hour + 24) % 24), window, sizeof(window));
  }
  if (run.state == LNA_OFF) outLine(o, "Off better %s: timed source?", window);
  else outLine(o, "On better %s (quieter hours).", window);
  if (tz_hours == 0) outLine(o, "set tz for local times");
}

static void addFreezes(const LnaStudy& s, DiagOut& o) {
  uint16_t on = s.freezes[LNA_ON], off = s.freezes[LNA_OFF];
  if (on + off == 0) outLine(o, "No freezes in either state.");
  else if (off == 0) outLine(o, "%u freeze%s, all with the LNA on", on, on == 1 ? "" : "s");
  else outLine(o, "Freezes: %u on, %u off", on, off);
}

void lnaFormatResult(const LnaStudy* s, uint8_t live, uint8_t sf, int8_t tz_hours, const DiagNote* note, DiagOut& o) {
  if (s == nullptr || s->result == LNA_V_NONE) {
    outLine(o, "📶 LNA: ⚪ No Study Yet");
    outSep(o);
    outLine(o, "Start one: diag radio.lna start");
    return;
  }
  unsigned pairs = s->wins[LNA_ON] + s->wins[LNA_OFF] + s->ties;
  unsigned long hours = s->elapsed_s / 3600;
  switch ((LnaVerdict)s->result) {
    case LNA_V_NOT_WORKING:
      outLine(o, "📶 LNA: 🔴 Not Working");
      outLine(o, "Switching it moved the floor");
      outLine(o, "by %d dB (expect ~20).", (int)lroundf(lnaFloorShift(*s)));
      outSep(o);
      outLine(o, "Likely an LNA or FEM fault.");
      outLine(o, "Study stopped. Now: %s.", stateName(live));
      break;
    case LNA_V_KEEP_ON_LOST:
    case LNA_V_HELPS: {
      outLine(o, live == LNA_ON ? "📶 LNA: 🟢 Keep It On" : "📶 LNA: 🟡 Keep It On");
      outLine(o, "Studied %luh, %u pairs.", hours, pairs);
      int lost = lnaLostNeighbour(*s);
      if (lost >= 0) {
        const LnaNeighbour& n = s->nb[lost];
        if (n.id_len >= 2) outLine(o, "On hears %d%% more; %04X needs it", lnaPercentMore(*s, LNA_ON), n.id);
        else outLine(o, "On hears %d%% more; %02X needs it", lnaPercentMore(*s, LNA_ON), n.id & 0xFF);
      } else {
        outLine(o, "On hears %d%% more, %u neighbours", lnaPercentMore(*s, LNA_ON), lnaHeardNeighbours(*s));
      }
      addFreezes(*s, o);
      outSep(o);
      if (!notePrint(note, o)) addRunNote(*s, LNA_ON, tz_hours, o);
      addAdvice(live, LNA_ON, o);
      break;
    }
    case LNA_V_OVERLOADED: {
      float clarity = 0;
      bool clearer = lnaClarity(*s, sf, &clarity) && clarity <= LNA_CLEARER_DB;
      outLine(o, live == LNA_OFF ? "📶 LNA: 🟢 Overloaded (Study)" : "📶 LNA: 🔴 Overloaded");
      outLine(o, "Studied %luh, %u pairs.", hours, pairs);
      outLine(o, clearer ? "Off hears %d%% more, and clearer" : "Off hears %d%% more", lnaPercentMore(*s, LNA_OFF));
      addFreezes(*s, o);
      outSep(o);
      if (!notePrint(note, o)) addRunNote(*s, LNA_OFF, tz_hours, o);
      addAdvice(live, LNA_OFF, o);
      break;
    }
    case LNA_V_NO_BENEFIT:
      outLine(o, live == LNA_OFF ? "📶 LNA: 🟢 No Benefit Here (Study)" : "📶 LNA: 🟡 No Benefit Here");
      outLine(o, "Studied %luh, %u pairs.", hours, pairs);
      outLine(o, "On and off hear the same.");
      addFreezes(*s, o);
      outSep(o);
      if (!notePrint(note, o)) addRunNote(*s, LNA_OFF, tz_hours, o);
      addAdvice(live, LNA_OFF, o);
      break;
    default:
      outLine(o, "📶 LNA: ⚪ Not Enough Data");
      outLine(o, "%luh, %u neighbours, %u pairs.", hours, lnaHeardNeighbours(*s), pairs);
      outSep(o);
      outLine(o, "Too quiet to judge. Now: %s.", stateName(live));
      break;
  }
}

void lnaFormatProgress(const LnaStudy& s, uint8_t sf, DiagOut& o) {
  unsigned pairs = s.wins[LNA_ON] + s.wins[LNA_OFF] + s.ties;
  outLine(o, "📶 LNA: ⚪ Studying, %lu of %luh", (unsigned long)(s.elapsed_s / 3600), (unsigned long)(s.requested_s / 3600));
  outLine(o, "%u pairs, %u neighbours heard.", pairs, lnaHeardNeighbours(s));
  outSep(o);
  switch (lnaVerdict(s, sf, false)) {
    case LNA_V_KEEP_ON_LOST:
    case LNA_V_HELPS:
      outLine(o, "Leaning: on helps (+%d%%).", lnaPercentMore(s, LNA_ON));
      break;
    case LNA_V_OVERLOADED:
      outLine(o, "Leaning: overloaded (off +%d%%).", lnaPercentMore(s, LNA_OFF));
      break;
    case LNA_V_NOT_WORKING:
      outLine(o, "The LNA may not be working.");
      break;
    default:
      outLine(o, "No clear difference yet.");
      break;
  }
}

int lnaSummary(uint8_t result, int percent_on, bool active, uint16_t done_h, uint16_t of_h, bool live_on, char* out, size_t len) {
  if (active) {
    snprintf(out, len, "studying, %u of %uh", done_h, of_h);
    return DIAG_UNKNOWN;
  }
  switch (result) {
    case LNA_V_NOT_WORKING:
      snprintf(out, len, "not working");
      return DIAG_PROBLEM;
    case LNA_V_OVERLOADED:
      snprintf(out, len, live_on ? "on hurts here" : "off, as advised");
      return live_on ? DIAG_PROBLEM : DIAG_GOOD;
    case LNA_V_NO_BENEFIT:
      snprintf(out, len, live_on ? "no benefit here" : "off, as advised");
      return live_on ? DIAG_CAUTION : DIAG_GOOD;
    case LNA_V_KEEP_ON_LOST:
    case LNA_V_HELPS:
      if (live_on) snprintf(out, len, "on helps (+%d%%)", percent_on);
      else snprintf(out, len, "off, study says on");
      return live_on ? DIAG_GOOD : DIAG_CAUTION;
    case LNA_V_NOT_ENOUGH:
      snprintf(out, len, "last study too quiet");
      return DIAG_UNKNOWN;
    default:
      snprintf(out, len, "no study yet");
      return DIAG_UNKNOWN;
  }
}
