#include <helpers/diag_report/BattHistory.h>
#include <helpers/diag_report/BattLog.h>
#include <helpers/diag_report/DiagSummary.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

#define HOLD_MV          30            // a morning within this of the one before counts as held
#define LOSING_MV        30            // a week's fall beyond this is losing ground
#define CHART_MIN_SPAN   100           // a month of mornings wobbles by tens of mV; under this span the bars stay low

#define ADD(...) do { if (n < max_lines) snprintf(lines[n++], HIST_LINE, __VA_ARGS__); } while (0)

static const char* orb(uint8_t level) {
  if (level == DIAG_GOOD) return "🟢";
  if (level == DIAG_CAUTION) return "🟡";
  if (level == DIAG_PROBLEM) return "🔴";
  return "⚪";
}

static uint8_t packMv(uint16_t mv) {
  if (mv < 2500) return 0;
  uint32_t v = (uint32_t)(mv - 2500 + 4) / 8;
  return v > 254 ? 254 : (uint8_t)v;
}

static uint16_t unpackMv(uint8_t v) {
  return v == HIST_NO_MV ? 0 : (uint16_t)(2500 + 8 * v);
}

static int8_t packTemp(float c) {
  if (isnan(c)) return HIST_NO_TEMP;
  long r = lroundf(c);
  return (int8_t)(r < -127 ? -127 : r > 127 ? 127 : r);
}

void histInit(BattHistory& h) {
  h.last_day = 0;
  for (int i = 0; i < HIST_DAYS; i++) h.d[i] = {HIST_NO_MV, HIST_NO_TEMP, HIST_NO_TEMP};
}

void histTodayStart(HistToday& t, uint32_t local_day) {
  t.day = local_day;
  t.morning_mv = 0;
  t.tmin = t.tmax = HIST_NO_TEMP;
}

bool histSample(HistToday& t, HistToday& finished, uint32_t local_day, uint8_t local_hour, uint16_t mv, float temp_c) {
  bool closed = false;
  if (t.day == 0) {
    histTodayStart(t, local_day);
  } else if (local_day > t.day) {
    finished = t;
    closed = true;
    histTodayStart(t, local_day);
  } else if (local_day < t.day) {
    histTodayStart(t, local_day);      // the clock moved back: nothing finished, start the day afresh
  }
  if (mv != 0 && local_hour < HIST_NOON && (t.morning_mv == 0 || mv < t.morning_mv)) t.morning_mv = mv;
  int8_t c = packTemp(temp_c);
  if (c != HIST_NO_TEMP) {
    if (t.tmin == HIST_NO_TEMP || c < t.tmin) t.tmin = c;
    if (t.tmax == HIST_NO_TEMP || c > t.tmax) t.tmax = c;
  }
  return closed;
}

void histClose(BattHistory& h, const HistToday& t) {
  if (t.day == 0 || (h.last_day != 0 && t.day <= h.last_day)) return;
  // Every slot between the last record and this one is a day nobody saw; after a long gap that is all of them.
  uint32_t from = t.day >= HIST_DAYS - 1 ? t.day - (HIST_DAYS - 1) : 1;
  if (h.last_day != 0 && h.last_day + 1 > from) from = h.last_day + 1;
  for (uint32_t day = from; day < t.day; day++) h.d[day % HIST_DAYS] = {HIST_NO_MV, HIST_NO_TEMP, HIST_NO_TEMP};
  h.d[t.day % HIST_DAYS] = {t.morning_mv ? packMv(t.morning_mv) : (uint8_t)HIST_NO_MV, t.tmin, t.tmax};
  h.last_day = t.day;
}

HistDay histDay(const BattHistory& h, const HistToday& t, uint32_t day) {
  if (t.day != 0 && day == t.day) return {t.morning_mv ? packMv(t.morning_mv) : (uint8_t)HIST_NO_MV, t.tmin, t.tmax};
  if (h.last_day != 0 && day <= h.last_day && day + HIST_DAYS > h.last_day) return h.d[day % HIST_DAYS];
  return {HIST_NO_MV, HIST_NO_TEMP, HIST_NO_TEMP};
}

HistView histAnalyse(const BattHistory& h, const HistToday& t, bool calibrated) {
  HistView v;
  memset(&v, 0, sizeof(v));
  v.kind = HIST_K_WATCHING;
  v.level = DIAG_UNKNOWN;
  uint32_t today = t.day != 0 ? t.day : h.last_day;
  if (today < HIST_DAYS) return v;
  v.first_day = today - (HIST_DAYS - 1);
  uint16_t prev = 0, first_mv = 0, last_mv = 0;
  uint32_t first_d = 0, last_d = 0;
  for (int col = 0; col < HIST_DAYS; col++) {
    uint32_t day = v.first_day + col;
    HistDay rec = histDay(h, t, day);
    uint16_t mv = unpackMv(rec.morning);
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
    if (v.low_mv == 0 || mv < v.low_mv) { v.low_mv = mv; v.low_day = day; }
    if (first_mv == 0) { first_mv = mv; first_d = day; }
    last_mv = mv;
    last_d = day;
  }
  if (v.days >= 2) {
    v.net_mv = (int16_t)((int)last_mv - (int)first_mv);
    v.net_days = (uint8_t)(last_d - first_d);
  }
  if (v.days < HIST_MIN_DAYS) return v;
  if (calibrated && last_mv <= BATT_LOW_MV) {
    v.kind = HIST_K_LOW;
    v.level = DIAG_PROBLEM;
    return v;
  }
  // The newest morning against the latest one at least six days before it: a week's trend, not one cloudy day.
  uint16_t week_mv = 0;
  for (int col = (int)(last_d - v.first_day) - 6; col >= 0; col--) {
    if (v.chart[col] != 0) { week_mv = v.chart[col]; break; }
  }
  bool losing = week_mv != 0 && (int)last_mv - (int)week_mv < -LOSING_MV;
  v.kind = losing ? HIST_K_LOSING : HIST_K_HOLDING;
  v.level = losing ? DIAG_CAUTION : DIAG_GOOD;
  return v;
}

static void volts(uint16_t mv, char* out, size_t len) {
  snprintf(out, len, "%u.%02u V", mv / 1000, (mv % 1000 + 5) / 10 >= 100 ? 99 : (mv % 1000 + 5) / 10);
}

void histDateParts(uint32_t local_day, unsigned* day, unsigned* month) {
  long z = (long)local_day - 1 + 719468;
  long era = z / 146097;
  unsigned doe = (unsigned)(z - era * 146097);
  unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  unsigned mp = (5 * doy + 2) / 153;
  *day = doy - (153 * mp + 2) / 5 + 1;
  *month = mp < 10 ? mp + 3 : mp - 9;
}

const char* const HIST_MONTHS[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                     "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

void histDateText(uint32_t local_day, char* out, size_t len) {
  unsigned d, m;
  histDateParts(local_day, &d, &m);
  snprintf(out, len, "%u %s", d, HIST_MONTHS[m - 1]);
}

// One bar per day, then a date every week; the month is named where it changes.
static int chartLines(const HistView& v, char lines[][HIST_LINE], int n, int max_lines) {
  static const char* const BARS[] = {"▁", "▂", "▃", "▄", "▅", "▆", "▇", "█"};
  int top = 0, bottom = 0xFFFF;
  for (int col = 0; col < HIST_DAYS; col++) {
    if (v.chart[col] == 0) continue;
    if (v.chart[col] > top) top = v.chart[col];
    if (v.chart[col] < bottom) bottom = v.chart[col];
  }
  int range = top - bottom;
  if (range < CHART_MIN_SPAN) {
    // Centre a quiet month, so steady mornings sit mid-scale instead of looking empty.
    bottom = (top + bottom) / 2 - CHART_MIN_SPAN / 2;
    range = CHART_MIN_SPAN;
  }
  if (n < max_lines) {
    char* out = lines[n++];
    int len = 0;
    for (int col = 0; col < HIST_DAYS; col++) {
      int level = (((int)v.chart[col] - bottom) * 7 + range / 2) / range;
      len += snprintf(out + len, HIST_LINE - len, "%s", v.chart[col] == 0 ? " " : BARS[level]);
    }
  }
  char axis[HIST_DAYS + 8];
  memset(axis, ' ', sizeof(axis) - 1);
  axis[sizeof(axis) - 1] = 0;
  int end = 0;
  unsigned last_month = 0;
  for (int col = 0; col < HIST_DAYS; col += 7) {
    unsigned d, m;
    histDateParts(v.first_day + col, &d, &m);
    char label[8];
    if (m != last_month) snprintf(label, sizeof(label), "%u %s", d, HIST_MONTHS[m - 1]);
    else snprintf(label, sizeof(label), "%u", d);
    last_month = m;
    size_t len = strlen(label);
    if (col + len >= sizeof(axis)) break;
    memcpy(axis + col, label, len);
    end = col + (int)len;
  }
  axis[end] = 0;
  if (n < max_lines) snprintf(lines[n++], HIST_LINE, "%s", axis);
  return n;
}

static const char* kindText(uint8_t kind) {
  switch (kind) {
    case HIST_K_HOLDING: return "Holding";
    case HIST_K_LOSING: return "Losing Ground";
    case HIST_K_LOW: return "Low";
    default: return "Watching";
  }
}

int histFormat(const HistView& v, char lines[][HIST_LINE], int max_lines) {
  int n = 0;
  if (v.kind == HIST_K_WATCHING) ADD("🔋 30 Days: ⚪ Watching, %u of %u days", v.days, HIST_MIN_DAYS);
  else ADD("🔋 30 Days: %s %s", orb(v.level), kindText(v.kind));
  if (v.days == 0) {
    ADD("➜ Verdict After %u Days", HIST_MIN_DAYS);
    return n;
  }
  ADD("Morning low, 30 days");
  n = chartLines(v, lines, n, max_lines);
  if (v.pairs > 0) ADD("Held %u of %u days, %u missing", v.held, v.pairs, v.missing);
  if (v.days > 0) {
    char low[10], when[12];
    volts(v.low_mv, low, sizeof(low));
    histDateText(v.low_day, when, sizeof(when));
    ADD("Lowest morning %s on %s", low, when);
  }
  if (v.net_days > 0) ADD("Net %+d mV over %u days", v.net_mv, v.net_days);
  if (v.kind == HIST_K_LOW) ADD("➜ Charge Now: it may stop soon");
  else if (v.kind == HIST_K_LOSING) ADD("➜ Check the Panel: charging is not keeping up");
  else if (v.kind == HIST_K_WATCHING) ADD("➜ Verdict After %u Days", HIST_MIN_DAYS);
  else ADD("➜ Nothing to Fix");
  return n;
}
