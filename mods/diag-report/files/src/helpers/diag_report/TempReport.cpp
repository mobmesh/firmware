#include <helpers/diag_report/TempReport.h>
#include <helpers/diag_report/DiagSummary.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

#define ADD(...) do { if (n < max_lines) snprintf(lines[n++], TEMP_LINE, __VA_ARGS__); } while (0)

static const char* orb(uint8_t level) {
  if (level == DIAG_GOOD) return "🟢";
  if (level == DIAG_CAUTION) return "🟡";
  if (level == DIAG_PROBLEM) return "🔴";
  return "⚪";
}

TempView tempAnalyse(const BattHistory& h, const HistToday& t, float now_c) {
  TempView v;
  memset(&v, 0, sizeof(v));
  v.kind = TEMP_K_NONE;
  v.level = DIAG_UNKNOWN;
  v.has_now = !isnan(now_c);
  if (v.has_now) v.now_c = (int8_t)(now_c < -127 ? -127 : now_c > 127 ? 127 : lroundf(now_c));
  for (int col = 0; col < HIST_DAYS; col++) v.lows[col] = v.highs[col] = HIST_NO_TEMP;
  uint32_t today = t.day != 0 ? t.day : h.last_day;
  if (today >= HIST_DAYS) {
    v.first_day = today - (HIST_DAYS - 1);
    bool recent_cold = false, recent_hot = false;
    for (int col = 0; col < HIST_DAYS; col++) {
      uint32_t day = v.first_day + col;
      HistDay rec = histDay(h, t, day);
      if (rec.tmin == HIST_NO_TEMP) continue;
      v.lows[col] = rec.tmin;
      v.highs[col] = rec.tmax;
      if (v.days == 0 || rec.tmin < v.tmin) { v.tmin = rec.tmin; v.cold_day = day; }
      if (v.days == 0 || rec.tmax > v.tmax) { v.tmax = rec.tmax; v.warm_day = day; }
      v.days++;
      bool recent = col >= HIST_DAYS - TEMP_RECENT_DAYS;
      if (rec.tmin < TEMP_COLD_C) { v.cold_days++; recent_cold |= recent; }
      if (rec.tmax >= TEMP_HOT_C) { v.hot_days++; recent_hot |= recent; }
    }
    if (v.days > 0) {
      v.kind = recent_hot ? TEMP_K_HOT : recent_cold ? TEMP_K_COLD : TEMP_K_NORMAL;
      v.level = v.kind == TEMP_K_NORMAL ? DIAG_GOOD : DIAG_CAUTION;
      return v;
    }
  }
  if (v.has_now) v.kind = TEMP_K_WATCHING;
  return v;
}

// Low and high of each calendar week (Monday start), oldest first, up to TEMP_WEEKS rows ending this week.
static int weekRows(const TempView& v, char lines[][TEMP_LINE], int n, int max_lines) {
  int first_row = n;
  int32_t today = (int32_t)(v.first_day + HIST_DAYS - 1);
  int32_t monday = today - (today + 2) % 7;          // local day 1 was Thursday 1 Jan 1970
  for (int week = TEMP_WEEKS - 1; week >= 0; week--) {
    int32_t start = monday - week * 7;
    int first = (int)(start - (int32_t)v.first_day), last = first + 6;
    if (last < 0) continue;
    if (first < 0) first = 0;
    if (last > HIST_DAYS - 1) last = HIST_DAYS - 1;
    int lo = 127, hi = -128;
    for (int col = first; col <= last; col++) {
      if (v.lows[col] == HIST_NO_TEMP) continue;
      if (v.lows[col] < lo) lo = v.lows[col];
      if (v.highs[col] > hi) hi = v.highs[col];
    }
    if (hi == -128 || n >= max_lines) continue;
    if (n == first_row) ADD("Week of   Low  High °C");
    char from[12];
    histDateText((uint32_t)start, from, sizeof(from));
    ADD("%-8s %4d %5d", from, lo, hi);
  }
  return n;
}

static const char* kindText(uint8_t kind) {
  switch (kind) {
    case TEMP_K_NORMAL: return "Normal";
    case TEMP_K_COLD: return "Below Freezing";
    case TEMP_K_HOT: return "Running Hot";
    case TEMP_K_WATCHING: return "Watching";
    default: return "Not Available";
  }
}

int tempFormat(const TempView& v, char lines[][TEMP_LINE], int max_lines) {
  int n = 0;
  ADD("🌡️ CPU: %s %s", orb(v.level), kindText(v.kind));
  if (v.has_now) ADD("Now %d °C", v.now_c);
  if (v.days == 0) {
    if (v.kind == TEMP_K_WATCHING) ADD("➜ First Day After Midnight");
    return n;
  }
  n = weekRows(v, lines, n, max_lines);
  char cold[12], warm[12];
  histDateText(v.cold_day, cold, sizeof(cold));
  histDateText(v.warm_day, warm, sizeof(warm));
  ADD("Coldest %d °C on %s, warmest %d °C on %s", v.tmin, cold, v.tmax, warm);
  if (v.cold_days > 0) ADD("Below 0 °C on %u day%s: charging may pause", v.cold_days, v.cold_days == 1 ? "" : "s");
  if (v.hot_days > 0) ADD("%d °C or more on %u day%s", TEMP_HOT_C, v.hot_days, v.hot_days == 1 ? "" : "s");
  if (v.kind == TEMP_K_HOT) ADD("➜ Shade It: heat shortens battery life");
  else if (v.kind == TEMP_K_COLD) ADD("➜ Expect Slow Charging: the pack is below freezing");
  else ADD("➜ Nothing to Fix");
  return n;
}
