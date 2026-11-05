#include <helpers/diag_report/BattLog.h>
#include <helpers/diag_report/DiagSummary.h>
#include <helpers/diag_report/NoiseLog.h>

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define CELL_MIN_MV     2500
#define FLAT_MV_PER_DAY 30
#define RISE_MV         8
#define CHART_FLAT_MV   20
#define CHART_FLAT_LEVEL 3
#define NIGHT_MIN_HOURS 2

#define ADD(...) do { if (n < max_lines) snprintf(lines[n++], BATT_LINE, __VA_ARGS__); } while (0)

static const char* orb(uint8_t level) {
  if (level == DIAG_GOOD) return "🟢";
  if (level == DIAG_CAUTION) return "🟡";
  if (level == DIAG_PROBLEM) return "🔴";
  return "⚪";
}

void battSample(BattLog& log, uint32_t hour, int8_t tz_hours, uint16_t mv) {
  uint32_t day = (hour + 24 + tz_hours) / 24;
  if (day != log.day) {
    bool whole = log.day + 1 == day && hour - log.day_hour >= BATT_FULL_HOURS;
    log.full_days = !whole ? 0 : log.full_days < 2 ? log.full_days + 1 : 2;
    log.before_mv = log.yesterday_mv;
    log.yesterday_mv = log.day_mv;
    log.day = day;
    log.day_hour = hour;
    log.day_mv = mv;
  }
  BattHour& h = log.h[hour % BATT_HOURS];
  if (h.hour != hour) {
    h.hour = hour;
    h.lo = h.hi = h.first = mv;
  }
  if (mv < h.lo) h.lo = mv;
  if (mv > h.hi) h.hi = mv;
  h.last = mv;
}

static uint8_t localHour(uint32_t hour, int8_t tz_hours) {
  return (uint8_t)((hour % 24 + 24 + tz_hours % 24) % 24);
}

BattView battAnalyse(const BattLog& log, uint32_t now_hour, int8_t tz_hours, bool calibrated) {
  BattView v;
  memset(&v, 0, sizeof(v));
  v.night_drain = -1;
  v.kind = BATT_K_WATCHING;
  v.level = DIAG_UNKNOWN;

  const BattHour* oldest = nullptr;
  const BattHour* newest = nullptr;
  uint32_t night_drop = 0;
  uint8_t night_hours = 0;
  for (uint32_t age = BATT_HOURS; age-- > 0;) {
    if (now_hour < age) continue;
    const BattHour& h = log.h[(now_hour - age) % BATT_HOURS];
    if (h.hour != now_hour - age || h.hour == 0) continue;
    uint8_t local = localHour(h.hour, tz_hours);
    v.hours++;
    if (oldest == nullptr) oldest = &h;
    newest = &h;
    if (v.hours == 1 || h.lo < v.lo_mv) {
      v.lo_mv = h.lo;
      v.lo_hour = local;
    }
    if (h.hi > v.hi_mv) {
      v.hi_mv = h.hi;
      v.hi_hour = local;
    }
    if (h.last >= h.first + RISE_MV) v.charge_hours++;
    // The current hour has only just begun, so it would understate the rate.
    if (age > 0 && (local >= 22 || local < 5) && h.first >= h.last) {
      night_drop += h.first - h.last;
      night_hours++;
    }
  }
  if (newest == nullptr) return v;

  v.now_mv = newest->last;
  v.span = (uint8_t)(now_hour - oldest->hour);
  v.net_mv = (int16_t)v.now_mv - (int16_t)oldest->first;
  if (night_hours >= NIGHT_MIN_HOURS) v.night_drain = (int16_t)(night_drop / night_hours);

  v.chart_start = localHour(now_hour + 1, tz_hours);
  for (uint32_t age = 0; age < BATT_HOURS; age++) {
    if (now_hour < age) continue;
    const BattHour& h = log.h[(now_hour - age) % BATT_HOURS];
    if (h.hour != now_hour - age || h.hour == 0) continue;
    v.chart[BATT_HOURS - 1 - age] = (uint16_t)((h.lo + h.hi) / 2);
  }

  if (calibrated && v.now_mv >= CELL_MIN_MV && v.now_mv <= BATT_LOW_MV) {
    v.kind = BATT_K_LOW;
    v.level = DIAG_PROBLEM;
    return v;
  }
  if (v.span < BATT_FULL_HOURS) return v;
  int per_day = (int)v.net_mv * 24 / v.span;
  if (per_day > FLAT_MV_PER_DAY) {
    v.kind = BATT_K_CHARGING;
    v.level = DIAG_GOOD;
  } else if (per_day >= -FLAT_MV_PER_DAY) {
    v.kind = BATT_K_STEADY;
    v.level = DIAG_GOOD;
  } else {
    // Two whole days that do not overlap; one bad day can still be the weather.
    bool pattern = log.full_days >= 2 && log.yesterday_mv - log.day_mv > FLAT_MV_PER_DAY
                   && log.before_mv - log.yesterday_mv > FLAT_MV_PER_DAY;
    v.kind = v.charge_hours == 0 ? BATT_K_DRAINING : pattern ? BATT_K_NOT_RECOVERING : BATT_K_NET_LOSS;
    v.level = DIAG_CAUTION;
  }
  return v;
}

static const char* kindText(uint8_t kind) {
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

static void volts(uint16_t mv, char* out, size_t len) {
  snprintf(out, len, "%u.%02u V", mv / 1000, (mv % 1000 + 5) / 10 >= 100 ? 99 : (mv % 1000 + 5) / 10);
}

// One bar per hour, then the hours. A small range stays level, so the bars never make noise look like a trend.
static int chartLines(const BattView& v, char lines[][BATT_LINE], int n, int max_lines) {
  static const char* const BARS[] = {"▁", "▂", "▃", "▄", "▅", "▆", "▇", "█"};
  int top = 0, bottom = 0xFFFF;
  for (int col = 0; col < BATT_HOURS; col++) {
    if (v.chart[col] == 0) continue;
    if (v.chart[col] > top) top = v.chart[col];
    if (v.chart[col] < bottom) bottom = v.chart[col];
  }
  int range = top - bottom;
  if (n < max_lines) {
    char* out = lines[n++];
    int len = 0;
    for (int col = 0; col < BATT_HOURS; col++) {
      int level = range < CHART_FLAT_MV ? CHART_FLAT_LEVEL : (((int)v.chart[col] - bottom) * 7 + range / 2) / range;
      len += snprintf(out + len, BATT_LINE - len, "%s", v.chart[col] == 0 ? " " : BARS[level]);
    }
  }
  char axis[BATT_HOURS + 4];
  memset(axis, ' ', sizeof(axis) - 1);
  axis[sizeof(axis) - 1] = 0;
  int end = 0;
  for (int i = 0; i < BATT_HOURS; i++) {
    uint8_t local = (uint8_t)((v.chart_start + i) % 24);
    if (local % 6 != 0) continue;
    char hour[6];
    noiseHourText(local, true, hour, sizeof(hour));
    memcpy(axis + i, hour, strlen(hour));
    end = i + (int)strlen(hour);
  }
  axis[end] = 0;
  if (n < max_lines) snprintf(lines[n++], BATT_LINE, "%s", axis);
  return n;
}

int battFormat(const BattView& v, char lines[][BATT_LINE], int max_lines) {
  int n = 0;
  if (v.hours == 0) {
    ADD("🔋 Battery: ⚪ Watching, 0 of 24h");
    return n;
  }
  char now[10], lo[10], hi[10], at_lo[8], at_hi[8];
  volts(v.now_mv, now, sizeof(now));
  volts(v.lo_mv, lo, sizeof(lo));
  volts(v.hi_mv, hi, sizeof(hi));
  noiseHourText(v.lo_hour, false, at_lo, sizeof(at_lo));
  noiseHourText(v.hi_hour, false, at_hi, sizeof(at_hi));

  if (v.kind == BATT_K_WATCHING) ADD("🔋 Battery: ⚪ Watching, %u of 24h", v.hours);
  else ADD("🔋 Battery: %s %s", orb(v.level), kindText(v.kind));
  ADD("Now %s", now);

  n = chartLines(v, lines, n, max_lines);
  if (v.night_drain >= 0) ADD("Night drain %d mV/h, charged %u hours", v.night_drain, v.charge_hours);
  else ADD("Charged %u hours", v.charge_hours);
  ADD("Low %s at %s, high %s at %s", lo, at_lo, hi, at_hi);
  if (v.span > 0) ADD("Net %+d mV over %uh", v.net_mv, v.span);

  if (v.kind == BATT_K_LOW) ADD("➜ Charge Now: it may stop soon");
  else if (v.kind == BATT_K_NOT_RECOVERING) ADD("➜ Check the Panel: it isn't refilling each day");
  else if (v.kind == BATT_K_NET_LOSS) ADD("➜ Watch It: may just be low sun day.");
  else if (v.kind == BATT_K_DRAINING) ADD("➜ Charge It: or add a panel");
  else if (v.kind == BATT_K_WATCHING) ADD("➜ Verdict After %u Hours", BATT_FULL_HOURS);
  else ADD("➜ Nothing to Fix");
  return n;
}

int battSummary(const BattView& v, char* out, size_t len) {
  if (v.kind == BATT_K_WATCHING) snprintf(out, len, "%u of 24h", v.hours);
  else snprintf(out, len, "%s", kindText(v.kind));
  // The diag summary keeps its rows in lower case; only the report's own header is title case.
  for (char* c = out; *c; c++) if (*c >= 'A' && *c <= 'Z') *c += 'a' - 'A';
  return v.level;
}
