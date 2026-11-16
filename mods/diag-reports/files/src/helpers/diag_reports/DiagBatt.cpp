#include <helpers/diag_reports/DiagBatt.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

#define CELL_MIN_MV      2500
#define FLAT_MV_PER_DAY  30
#define RISE_MV          8
#define CHART_FLAT_MV    20
#define CHART_FLAT_LEVEL 3
#define NIGHT_MIN_HOURS  2
#define HOLD_MV          30            // a morning within this of the one before counts as held
#define LOSING_MV        30            // a week's fall beyond this is losing ground
#define HIST_CHART_SPAN  100           // a month of mornings wobbles by tens of mV; under this span the bars stay mid-scale
#define TEMP_WEEKS       5

static const char* const BARS[] = {"▁", "▂", "▃", "▄", "▅", "▆", "▇", "█"};

// One bar per value; 0 draws a gap. A range under `min_span` is centred on it so small wobbles stay level.
static void barLine(const uint16_t* values, int count, int min_span, bool flat, DiagOut& o) {
  int top = 0, bottom = 0xFFFF;
  for (int i = 0; i < count; i++) {
    if (values[i] == 0) continue;
    if (values[i] > top) top = values[i];
    if (values[i] < bottom) bottom = values[i];
  }
  int range = top - bottom;
  bool level_bars = flat && range < min_span;
  if (!flat && range < min_span) {
    bottom = (top + bottom) / 2 - min_span / 2;
    range = min_span;
  }
  char line[STORE_DAYS * 3 + 1];
  int len = 0;
  for (int i = 0; i < count; i++) {
    int level = level_bars ? CHART_FLAT_LEVEL : (((int)values[i] - bottom) * 7 + range / 2) / range;
    len += snprintf(line + len, sizeof(line) - len, "%s", values[i] == 0 ? " " : BARS[level]);
  }
  outLine(o, "%s", line);
}

static void adviceOrNote(const DiagNote* note, const char* advice, DiagOut& o) {
  if (!notePrint(note, o)) outLine(o, "%s", advice);
}

BattView battAnalyse(const DiagStore& s, uint32_t now_hour, int8_t tz_hours, bool calibrated) {
  BattView v;
  memset(&v, 0, sizeof(v));
  v.night_drain = -1;
  v.kind = BATT_K_WATCHING;
  v.level = DIAG_UNKNOWN;
  v.calibrated = calibrated;

  const StoreHour* oldest = nullptr;
  const StoreHour* newest = nullptr;
  const StoreHour* previous = nullptr;
  uint32_t night_drop = 0;
  uint8_t night_hours = 0;
  for (uint32_t age = 24; age-- > 0;) {
    if (now_hour < age) continue;
    const StoreHour* h = storeHourAt(s, now_hour - age);
    if (h == nullptr || !(h->flags & HOUR_BATT)) continue;
    uint8_t local = storeLocalHour(h->hour, tz_hours);
    v.hours++;
    if (oldest == nullptr) oldest = h;
    previous = newest;
    newest = h;
    if (v.hours == 1 || h->batt_lo < v.lo_mv) {
      v.lo_mv = h->batt_lo;
      v.lo_hour = local;
    }
    if (h->batt_hi > v.hi_mv) {
      v.hi_mv = h->batt_hi;
      v.hi_hour = local;
    }
    if (h->batt_last >= h->batt_first + RISE_MV) v.charge_hours++;
    // The current hour has only just begun, so it would understate the rate.
    if (age > 0 && (local >= 22 || local < 5) && h->batt_first >= h->batt_last) {
      night_drop += h->batt_first - h->batt_last;
      night_hours++;
    }
    v.chart[23 - age] = (uint16_t)((h->batt_lo + h->batt_hi) / 2);
  }
  if (newest == nullptr) return v;

  v.now_mv = newest->batt_last;
  v.span = (uint8_t)(now_hour - oldest->hour);
  v.net_mv = (int16_t)((int)v.now_mv - (int)oldest->batt_first);
  // Hour against hour, each the middle of its readings, so one noisy reading cannot make a trend.
  v.hour_mv = previous ? (int16_t)(((int)newest->batt_lo + newest->batt_hi) / 2 - ((int)previous->batt_lo + previous->batt_hi) / 2) : 0;
  if (night_hours >= NIGHT_MIN_HOURS) v.night_drain = (int16_t)(night_drop / night_hours);
  v.chart_start = storeLocalHour(now_hour + 1, tz_hours);

  if (calibrated && v.now_mv >= CELL_MIN_MV && v.now_mv <= BATT_LOW_MV) {
    v.kind = BATT_K_LOW;
    v.level = DIAG_PROBLEM;
    return v;
  }
  if (v.span < STORE_FULL_HOURS) return v;
  int per_day = (int)v.net_mv * 24 / v.span;
  if (per_day > FLAT_MV_PER_DAY) {
    v.kind = BATT_K_CHARGING;
    v.level = DIAG_GOOD;
  } else if (per_day >= -FLAT_MV_PER_DAY) {
    v.kind = BATT_K_STEADY;
    v.level = DIAG_GOOD;
  } else {
    // Two whole days that do not overlap; one bad day can still be the weather.
    const StoreBattDays& b = s.batt;
    bool pattern = b.full_days >= 2 && b.yesterday_mv - b.day_mv > FLAT_MV_PER_DAY && b.before_mv - b.yesterday_mv > FLAT_MV_PER_DAY;
    v.kind = v.charge_hours == 0 ? BATT_K_DRAINING : pattern ? BATT_K_NOT_RECOVERING : BATT_K_NET_LOSS;
    v.level = DIAG_CAUTION;
  }
  return v;
}

static const char* battKindText(uint8_t kind) {
  switch (kind) {
    case BATT_K_STEADY: return "Steady";
    case BATT_K_CHARGING: return "Charging Fine";
    case BATT_K_DRAINING: return "Draining";
    case BATT_K_NET_LOSS: return "Net Loss Today";
    case BATT_K_NOT_RECOVERING: return "Not Recovering Daily";
    case BATT_K_LOW: return "Low";
    default: return "Watching";
  }
}

void battFormat(const BattView& v, const DiagNote* note, DiagOut& o) {
  if (v.hours == 0) {
    outLine(o, "🔋 Battery: ⚪ Watching, 0 of 24h");
    return;
  }
  char now[10], lo[10], hi[10], at_lo[8], at_hi[8];
  diagVolts(v.now_mv, now, sizeof(now));
  diagVolts(v.lo_mv, lo, sizeof(lo));
  diagVolts(v.hi_mv, hi, sizeof(hi));
  diagHourText(v.lo_hour, false, at_lo, sizeof(at_lo));
  diagHourText(v.hi_hour, false, at_hi, sizeof(at_hi));

  if (v.kind == BATT_K_WATCHING) outLine(o, "🔋 Battery: ⚪ Watching, %u of 24h", v.hours);
  else outLine(o, "🔋 Battery: %s %s", diagOrb(v.level), battKindText(v.kind));
  outLine(o, "%s now%s", now, v.calibrated ? " (calibrated)" : "");
  barLine(v.chart, 24, CHART_FLAT_MV, true, o);
  char axis[28];
  memset(axis, ' ', sizeof(axis) - 1);
  int end = 0;
  for (int i = 0; i < 24; i++) {
    uint8_t local = (uint8_t)((v.chart_start + i) % 24);
    if (local % 6 != 0) continue;
    char hour[6];
    diagHourText(local, true, hour, sizeof(hour));
    memcpy(axis + i, hour, strlen(hour));
    end = i + (int)strlen(hour);
  }
  axis[end] = 0;
  outLine(o, "%s", axis);
  if (v.night_drain >= 0) outLine(o, "Night drain %d mV/h, charged %uh", v.night_drain, v.charge_hours);
  else outLine(o, "Charged %u hours", v.charge_hours);
  outLine(o, "Low %s %s, high %s %s", lo, at_lo, hi, at_hi);
  if (v.span > 0) outLine(o, "%s %d mV in %uh", v.net_mv < 0 ? "Down" : "Up", v.net_mv < 0 ? -v.net_mv : v.net_mv, v.span);

  outSep(o);
  switch (v.kind) {
    case BATT_K_LOW: adviceOrNote(note, "Charge it now: it may stop soon.", o); break;
    case BATT_K_NOT_RECOVERING: adviceOrNote(note, "Check the panel: no daily refill.", o); break;
    case BATT_K_NET_LOSS: adviceOrNote(note, "Watch it: may just be a low sun day.", o); break;
    case BATT_K_DRAINING: adviceOrNote(note, "Charge it, or add a panel.", o); break;
    case BATT_K_WATCHING: outLine(o, "Verdict after %u hours.", STORE_FULL_HOURS); break;
    default: outLine(o, "Nothing to fix."); break;
  }
}

int battSummary(const BattView& v, char* out, size_t len) {
  char now[10];
  diagVolts(v.now_mv, now, sizeof(now));
  switch (v.kind) {
    case BATT_K_WATCHING: snprintf(out, len, "%u of 24h, %s", v.hours, now); break;
    case BATT_K_STEADY: snprintf(out, len, "holding, %s", now); break;
    case BATT_K_CHARGING: snprintf(out, len, "charging, %s", now); break;
    case BATT_K_DRAINING: snprintf(out, len, "draining, %s", now); break;
    case BATT_K_NET_LOSS: snprintf(out, len, "net loss today, %s", now); break;
    case BATT_K_NOT_RECOVERING: snprintf(out, len, "not recovering, %s", now); break;
    default: snprintf(out, len, "low, %s", now); break;
  }
  return v.level;
}

HistView histAnalyse(const DiagStore& s, bool calibrated) {
  HistView v;
  memset(&v, 0, sizeof(v));
  v.kind = HIST_K_WATCHING;
  v.level = DIAG_UNKNOWN;
  v.days_to_low = -1;
  uint32_t today = s.today.day != 0 ? s.today.day : s.last_day;
  if (today < STORE_DAYS) return v;
  v.first_day = today - (STORE_DAYS - 1);
  uint16_t prev = 0, first_mv = 0, last_mv = 0;
  uint32_t first_d = 0, last_d = 0;
  for (int col = 0; col < STORE_DAYS; col++) {
    uint32_t day = v.first_day + col;
    uint16_t mv = storeUnpackMv(storeDay(s, day).morning);
    v.chart[col] = mv;
    if (mv == 0) {
      // Days before the first reading predate the watch, and today without a morning yet is only unfinished.
      if (first_mv != 0 && day != today) v.missing++;
      continue;
    }
    v.days++;
    if (prev != 0) {
      v.pairs++;
      if (mv + HOLD_MV >= prev) v.held++;
    }
    prev = mv;
    if (v.low_mv == 0 || mv < v.low_mv) {
      v.low_mv = mv;
      v.low_day = day;
    }
    if (first_mv == 0) {
      first_mv = mv;
      first_d = day;
    }
    last_mv = mv;
    last_d = day;
  }
  v.last_mv = last_mv;
  if (v.days >= 2) {
    v.net_mv = (int16_t)((int)last_mv - (int)first_mv);
    v.net_days = (uint8_t)(last_d - first_d);
  }
  if (v.days < HIST_MIN_DAYS) return v;
  if (calibrated && v.net_mv < 0 && last_mv > BATT_LOW_MV) {
    long days = (long)(last_mv - BATT_LOW_MV) * v.net_days / -v.net_mv;
    v.days_to_low = (int16_t)(days > 999 ? 999 : days);
  }
  if (calibrated && last_mv <= BATT_LOW_MV) {
    v.kind = HIST_K_LOW;
    v.level = DIAG_PROBLEM;
    return v;
  }
  // The newest morning against the latest one at least six days before it: a week's trend, not one cloudy day.
  uint16_t week_mv = 0;
  for (int col = (int)(last_d - v.first_day) - 6; col >= 0; col--) {
    if (v.chart[col] != 0) {
      week_mv = v.chart[col];
      break;
    }
  }
  bool losing = week_mv != 0 && (int)last_mv - (int)week_mv < -LOSING_MV;
  v.kind = losing ? HIST_K_LOSING : HIST_K_HOLDING;
  v.level = losing ? DIAG_CAUTION : DIAG_GOOD;
  return v;
}

void histFormat(const HistView& v, const DiagNote* note, DiagOut& o) {
  static const char* const KIND[] = {"Watching", "Holding", "Losing Ground", "Low"};
  if (v.kind == HIST_K_WATCHING) outLine(o, "🔋 30 Days: ⚪ Watching, %u of %u days", v.days, HIST_MIN_DAYS);
  else outLine(o, "🔋 30 Days: %s %s", diagOrb(v.level), KIND[v.kind]);
  if (v.days == 0) {
    outSep(o);
    outLine(o, "Verdict after %u days.", HIST_MIN_DAYS);
    return;
  }
  outLine(o, "Morning low, 30 days");
  barLine(v.chart, STORE_DAYS, HIST_CHART_SPAN, false, o);
  // A date every week; the month is named where it changes.
  char axis[STORE_DAYS + 8];
  memset(axis, ' ', sizeof(axis) - 1);
  axis[sizeof(axis) - 1] = 0;
  int end = 0;
  unsigned last_month = 0;
  for (int col = 0; col < STORE_DAYS; col += 7) {
    unsigned d, m;
    diagDateParts(v.first_day + col, &d, &m);
    char label[8];
    if (m != last_month) snprintf(label, sizeof(label), "%u %s", d, DIAG_MONTHS[m - 1]);
    else snprintf(label, sizeof(label), "%u", d);
    last_month = m;
    size_t len = strlen(label);
    if (col + len >= sizeof(axis)) break;
    memcpy(axis + col, label, len);
    end = col + (int)len;
  }
  axis[end] = 0;
  outLine(o, "%s", axis);
  if (v.pairs > 0) outLine(o, "Held %u of %u days, %u missing", v.held, v.pairs, v.missing);
  char low[10], when[12];
  diagVolts(v.low_mv, low, sizeof(low));
  diagDateText(v.low_day, when, sizeof(when));
  outLine(o, "Lowest %s on %s", low, when);
  if (v.net_days > 0) outLine(o, "%s %d mV in %u days", v.net_mv < 0 ? "Down" : "Up", v.net_mv < 0 ? -v.net_mv : v.net_mv, v.net_days);

  outSep(o);
  if ((note == nullptr || note->n == 0) && v.kind != HIST_K_WATCHING && v.kind != HIST_K_LOW && v.days_to_low >= 0 && v.net_mv < -LOSING_MV) {
    outLine(o, "At this rate 3.35V in ~%d days.", v.days_to_low);
  }
  switch (v.kind) {
    case HIST_K_LOW: adviceOrNote(note, "Charge it now: it may stop soon.", o); break;
    case HIST_K_LOSING: adviceOrNote(note, "Check the panel: not keeping up.", o); break;
    case HIST_K_WATCHING: outLine(o, "Verdict after %u days.", HIST_MIN_DAYS); break;
    default: adviceOrNote(note, "Nothing to fix.", o); break;
  }
}

TempView tempAnalyse(const DiagStore& s, bool with_today, float now_c) {
  TempView v;
  memset(&v, 0, sizeof(v));
  v.kind = TEMP_K_NONE;
  v.level = DIAG_UNKNOWN;
  v.has_now = !isnan(now_c);
  if (v.has_now) v.now_c = (int8_t)(now_c < -127 ? -127 : now_c > 127 ? 127 : lroundf(now_c));
  for (int col = 0; col < STORE_DAYS; col++) v.lows[col] = v.highs[col] = STORE_NO_TEMP;
  uint32_t today = with_today && s.today.day != 0 ? s.today.day : s.last_day;
  if (today >= STORE_DAYS) {
    v.first_day = today - (STORE_DAYS - 1);
    bool recent_cold = false, recent_hot = false;
    for (int col = 0; col < STORE_DAYS; col++) {
      uint32_t day = v.first_day + col;
      if (!with_today && day == s.today.day) continue;
      StoreDay rec = storeDay(s, day);
      if (rec.tmin == STORE_NO_TEMP) continue;
      v.lows[col] = rec.tmin;
      v.highs[col] = rec.tmax;
      if (v.days == 0 || rec.tmin < v.tmin) {
        v.tmin = rec.tmin;
        v.cold_day = day;
      }
      if (v.days == 0 || rec.tmax > v.tmax) {
        v.tmax = rec.tmax;
        v.warm_day = day;
      }
      v.days++;
      bool recent = col >= STORE_DAYS - TEMP_RECENT_DAYS;
      if (rec.tmin < TEMP_COLD_C) {
        v.cold_days++;
        recent_cold |= recent;
      }
      if (rec.tmax >= TEMP_HOT_C) {
        v.hot_days++;
        recent_hot |= recent;
      }
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

// Low and high of each calendar week (Monday start), oldest first, ending this week.
static void weekRows(const TempView& v, DiagOut& o) {
  bool titled = false;
  int32_t today = (int32_t)(v.first_day + STORE_DAYS - 1);
  int32_t monday = today - (today + 2) % 7;          // local day 1 was Thursday 1 Jan 1970
  for (int week = TEMP_WEEKS - 1; week >= 0; week--) {
    int32_t start = monday - week * 7;
    int first = (int)(start - (int32_t)v.first_day), last = first + 6;
    if (last < 0) continue;
    if (first < 0) first = 0;
    if (last > STORE_DAYS - 1) last = STORE_DAYS - 1;
    int lo = 127, hi = -128;
    for (int col = first; col <= last; col++) {
      if (v.lows[col] == STORE_NO_TEMP) continue;
      if (v.lows[col] < lo) lo = v.lows[col];
      if (v.highs[col] > hi) hi = v.highs[col];
    }
    if (hi == -128) continue;
    if (!titled) outLine(o, "Week of   Low  High °C");
    titled = true;
    char from[12];
    diagDateText((uint32_t)start, from, sizeof(from));
    outLine(o, "%-8s %4d %5d", from, lo, hi);
  }
}

void tempFormat(const TempView& v, const DiagNote* note, DiagOut& o) {
  static const char* const KIND[] = {"Not Available", "Watching", "Normal", "Below Freezing", "Running Hot"};
  outLine(o, "🌡️ CPU: %s %s", diagOrb(v.level), KIND[v.kind]);
  if (v.has_now) outLine(o, "Now %d°C", v.now_c);
  if (v.days == 0) {
    if (v.kind == TEMP_K_WATCHING) {
      outSep(o);
      outLine(o, "First day files after midnight.");
    }
    return;
  }
  weekRows(v, o);
  char cold[12], warm[12];
  diagDateText(v.cold_day, cold, sizeof(cold));
  diagDateText(v.warm_day, warm, sizeof(warm));
  outLine(o, "Coldest %d°C on %s", v.tmin, cold);
  outLine(o, "Warmest %d°C on %s", v.tmax, warm);
  if (v.cold_days > 0) outLine(o, "Below 0°C on %u day%s", v.cold_days, v.cold_days == 1 ? "" : "s");
  if (v.hot_days > 0) outLine(o, "%d°C or more on %u day%s", TEMP_HOT_C, v.hot_days, v.hot_days == 1 ? "" : "s");
  outSep(o);
  if (v.kind == TEMP_K_HOT) adviceOrNote(note, "Shade or vent the enclosure.", o);
  else if (v.kind == TEMP_K_COLD) adviceOrNote(note, "Below freezing: charging may pause.", o);
  else adviceOrNote(note, "Nothing to fix.", o);
}
