#include <helpers/radio_diag/BattLog.h>
#include <helpers/radio_diag/DiagSummary.h>
#include <helpers/radio_diag/NoiseLog.h>

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define CELL_MIN_MV     2500
#define CELL_MAX_MV     4400
#define FLAT_MV_PER_DAY 30
#define RISE_MV         8
#define CHART_FLAT_MV   20
#define CHART_PREFIX    "24h "
#define CHART_FLAT_LEVEL 3
#define LOW_PERCENT     10
#define LOW_HOURS       24
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
    log.have_yesterday = log.day + 1 == day && hour - log.day_hour >= BATT_FULL_HOURS;
    log.yesterday_mv = log.day_mv;
    log.today_mv = mv;
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

int battCharge(uint16_t mv) {
  static const uint16_t CURVE[][2] = {{2500, 0}, {3000, 10}, {3300, 40}, {3500, 70}, {3600, 120}, {3700, 300},
                                      {3800, 500}, {3900, 650}, {4000, 800}, {4100, 900}, {4200, 1000}};
  const int points = sizeof(CURVE) / sizeof(CURVE[0]);
  if (mv <= CURVE[0][0]) return 0;
  for (int i = 1; i < points; i++) {
    if (mv <= CURVE[i][0]) {
      int span = CURVE[i][0] - CURVE[i - 1][0];
      return CURVE[i - 1][1] + (mv - CURVE[i - 1][0]) * (CURVE[i][1] - CURVE[i - 1][1]) / span;
    }
  }
  return 1000;
}

static uint32_t min32(uint32_t a, uint32_t b) {
  return a < b ? a : b;
}

static uint8_t localHour(uint32_t hour, int8_t tz_hours) {
  return (uint8_t)((hour % 24 + 24 + tz_hours % 24) % 24);
}

BattView battAnalyse(const BattLog& log, uint32_t now_hour, int8_t tz_hours, bool calibrated, uint16_t floor_mv) {
  BattView v;
  memset(&v, 0, sizeof(v));
  v.percent = -1;
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

  v.estimates = calibrated && v.now_mv >= CELL_MIN_MV && v.now_mv <= CELL_MAX_MV;
  bool full = v.span >= BATT_FULL_HOURS;
  if (v.estimates) {
    int floor_charge = battCharge(floor_mv);
    int usable = battCharge(v.now_mv) - floor_charge;
    if (usable < 0) usable = 0;
    v.percent = (int8_t)((usable * 100 + (1000 - floor_charge) / 2) / (1000 - floor_charge));
    // Charge above the floor over the average daily loss across yesterday and today.
    if (log.have_yesterday && now_hour >= log.day_hour) {
      uint32_t today_hours = now_hour - log.day_hour;
      int loss = battCharge(log.yesterday_mv) - battCharge(log.today_mv) + battCharge(log.day_mv) - battCharge(v.now_mv);
      if (loss > 0) v.hours_left = (uint16_t)min32((uint32_t)usable * (24 + today_hours) / loss, 0xFFFF);
    }
    if (v.percent <= LOW_PERCENT || (v.hours_left > 0 && v.hours_left < LOW_HOURS)) {
      v.kind = BATT_K_LOW;
      v.level = DIAG_PROBLEM;
      return v;
    }
  }
  if (!full) return v;
  int per_day = (int)v.net_mv * 24 / v.span;
  if (per_day > FLAT_MV_PER_DAY) {
    v.kind = BATT_K_CHARGING;
    v.level = DIAG_GOOD;
    v.hours_left = 0;
  } else if (per_day >= -FLAT_MV_PER_DAY) {
    v.kind = BATT_K_STEADY;
    v.level = DIAG_GOOD;
    v.hours_left = 0;
  } else {
    v.kind = v.charge_hours > 0 ? BATT_K_NOT_RECOVERING : BATT_K_DRAINING;
    v.level = DIAG_CAUTION;
  }
  return v;
}

static void leftText(const BattView& v, char* out, size_t len) {
  if (v.hours_left == 0) out[0] = 0;
  else if (v.hours_left < 48) snprintf(out, len, ", about %u hours left", v.hours_left);
  else if (v.hours_left < 24 * 14) snprintf(out, len, ", about %u days left", (v.hours_left + 12) / 24);
  else snprintf(out, len, ", over 2 weeks left");
}

static const char* kindText(uint8_t kind) {
  switch (kind) {
    case BATT_K_STEADY: return "steady";
    case BATT_K_CHARGING: return "charging fine";
    case BATT_K_DRAINING: return "draining";
    case BATT_K_NOT_RECOVERING: return "not recovering";
    case BATT_K_LOW: return "low";
    default: return "watching";
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
    int len = snprintf(out, BATT_LINE, "%s", CHART_PREFIX);
    for (int col = 0; col < BATT_HOURS; col++) {
      int level = range < CHART_FLAT_MV ? CHART_FLAT_LEVEL : (((int)v.chart[col] - bottom) * 7 + range / 2) / range;
      len += snprintf(out + len, BATT_LINE - len, "%s", v.chart[col] == 0 ? " " : BARS[level]);
    }
  }
  const int indent = sizeof(CHART_PREFIX) - 1;
  char axis[sizeof(CHART_PREFIX) + BATT_HOURS + 3];
  memset(axis, ' ', sizeof(axis) - 1);
  axis[sizeof(axis) - 1] = 0;
  int end = 0;
  for (int i = 0; i < BATT_HOURS; i++) {
    uint8_t local = (uint8_t)((v.chart_start + i) % 24);
    if (local % 6 != 0) continue;
    char hour[6];
    noiseHourText(local, true, hour, sizeof(hour));
    memcpy(axis + indent + i, hour, strlen(hour));
    end = indent + i + (int)strlen(hour);
  }
  axis[end] = 0;
  if (n < max_lines) snprintf(lines[n++], BATT_LINE, "%s", axis);
  return n;
}

int battFormat(const BattView& v, char lines[][BATT_LINE], int max_lines) {
  int n = 0;
  if (v.hours == 0) {
    ADD("🔋 Battery: ⚪ watching, 0 of 24h");
    return n;
  }
  char left[28], now[10], lo[10], hi[10], at_lo[8], at_hi[8];
  leftText(v, left, sizeof(left));
  volts(v.now_mv, now, sizeof(now));
  volts(v.lo_mv, lo, sizeof(lo));
  volts(v.hi_mv, hi, sizeof(hi));
  noiseHourText(v.lo_hour, false, at_lo, sizeof(at_lo));
  noiseHourText(v.hi_hour, false, at_hi, sizeof(at_hi));

  if (v.kind == BATT_K_WATCHING) ADD("🔋 Battery: ⚪ watching, %u of 24h", v.hours);
  else ADD("🔋 Battery: %s %s%s", orb(v.level), kindText(v.kind), left);
  if (v.percent >= 0) ADD("Now %s, about %d%%", now, v.percent);
  else ADD("Now %s, trend only on this board", now);

  ADD("Low %s at %s, high %s at %s", lo, at_lo, hi, at_hi);
  n = chartLines(v, lines, n, max_lines);
  if (v.night_drain >= 0) ADD("Night drain %d mV/h, charged %u hours", v.night_drain, v.charge_hours);
  else ADD("Charged %u hours", v.charge_hours);
  if (v.span > 0) ADD("Net %+d mV over %uh", v.net_mv, v.span);

  if (v.kind == BATT_K_LOW) ADD("➜ Charge it now, or it may stop soon");
  else if (v.kind == BATT_K_NOT_RECOVERING) ADD("➜ Check the panel: it isn't refilling each day");
  else if (v.kind == BATT_K_DRAINING) ADD("➜ Charge it, or add a panel");
  else if (v.kind == BATT_K_WATCHING) ADD("➜ A verdict comes after %u hours", BATT_FULL_HOURS);
  else ADD("➜ Nothing to fix");
  return n;
}

int battSummary(const BattView& v, char* out, size_t len) {
  char left[28];
  leftText(v, left, sizeof(left));
  if (v.kind == BATT_K_WATCHING) snprintf(out, len, "%u of 24h", v.hours);
  else if (v.hours_left > 0) snprintf(out, len, "%s%s", kindText(v.kind), left);
  else snprintf(out, len, "%s", kindText(v.kind));
  return v.level;
}
