#include <helpers/diag_reports/DiagTrend.h>
#include <helpers/diag_reports/DiagOut.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

TrendUsual trendUsual(const DiagStore& s, uint32_t now_hour, int8_t tz_hours) {
  TrendUsual u;
  memset(&u, 0, sizeof(u));
  for (uint32_t age = 1; age <= 24 && age < now_hour; age++) {
    const StoreHour* h = storeHourAt(s, now_hour - age);
    if (!h || h->floor_dbm == STORE_NO_FLOOR || h->freezes) continue;
    uint8_t local = storeLocalHour(h->hour, tz_hours);
    const StoreBase& b = s.base[local];
    if (b.n < TREND_MIN_DAYS || b.floor_dbm == STORE_NO_FLOOR) continue;
    u.ready = true;
    int excess = h->floor_dbm - b.floor_dbm;
    if (age == 1) u.now_db = (int8_t)excess;
    int bar = 2 * b.spread > TREND_ABOVE_DB ? 2 * b.spread : TREND_ABOVE_DB;
    if (excess >= bar) {
      u.above |= 1UL << local;
      if (excess > u.worst_db) {
        u.worst_db = (int8_t)excess;
        u.worst_hour = local;
      }
    }
  }
  return u;
}

TrendNb trendWorstNb(const DiagStore& s, uint32_t day) {
  TrendNb best;
  memset(&best, 0, sizeof(best));
  if (s.nb_day == 0 || day < s.nb_day) return best;
  for (int i = 0; i < STORE_NB_MAX; i++) {
    const StoreNbHistory& h = s.nb[i];
    if (!h.id_len) continue;
    // Days back from the newest recorded: 0-2 recent, 3-9 before.
    float recent = 0, before = 0, sx = 0, sy = 0, sxx = 0, sxy = 0;
    int nr = 0, nb = 0, n = 0;
    uint32_t first_low = 0;
    for (int back = 0; back < STORE_NB_DAYS; back++) {
      if (s.nb_day < (uint32_t)back + 1) break;
      uint32_t d = s.nb_day - back;
      int8_t v = h.snr[d % STORE_NB_DAYS];
      if (v == STORE_NO_SNR) continue;
      if (back < 3) {
        recent += v;
        nr++;
      } else if (back < 10) {
        before += v;
        nb++;
      }
      float x = -(float)back;
      sx += x;
      sy += v;
      sxx += x * x;
      sxy += x * v;
      n++;
    }
    if (nr < 2 || nb < 3) continue;
    recent /= nr;
    before /= nb;
    int drop = (int)lroundf(before - recent);
    if (drop < TREND_FADE_DB || (best.found && drop <= best.drop_db)) continue;
    // The drop began on the first day that fell below the earlier level by half the drop.
    for (int back = 9; back >= 0; back--) {
      if (s.nb_day < (uint32_t)back + 1) continue;
      uint32_t d = s.nb_day - back;
      int8_t v = h.snr[d % STORE_NB_DAYS];
      if (v != STORE_NO_SNR && v <= before - drop / 2.0f) {
        first_low = d;
        break;
      }
    }
    best.found = true;
    best.id = h.id;
    best.id_len = h.id_len;
    best.drop_db = (int8_t)drop;
    best.since_day = first_low;
    float den = n * sxx - sx * sx;
    best.slope_db_day = den != 0 ? (n * sxy - sx * sy) / den : 0;
  }
  return best;
}

int trendDaysToLimit(float margin_db, float slope_db_day) {
  if (slope_db_day >= -0.1f || margin_db <= 0) return -1;
  float days = margin_db / -slope_db_day;
  return days > 365 ? 365 : (int)lroundf(days);
}

static uint8_t pct(uint32_t part, uint32_t all) {
  return all ? (uint8_t)(part * 100 / all) : 0;
}

TrendShares trendShares(const DiagStore& s, uint32_t now_hour, int8_t tz_hours, uint32_t loud, uint32_t freeze, uint32_t busy) {
  TrendShares t;
  memset(&t, 0, sizeof(t));
  uint32_t e_noise = 0, e_freeze = 0, e_busy = 0, hours = 0, h_noise = 0, h_freeze = 0, h_busy = 0;
  for (uint32_t age = 1; age <= 24 && age < now_hour; age++) {
    const StoreHour* h = storeHourAt(s, now_hour - age);
    if (!h || !(h->flags & HOUR_COUNTS)) continue;
    uint32_t bit = 1UL << storeLocalHour(h->hour, tz_hours);
    hours++;
    t.errors += h->errors;
    if (loud & bit) { e_noise += h->errors; h_noise++; }
    if (freeze & bit) { e_freeze += h->errors; h_freeze++; }
    if (busy & bit) { e_busy += h->errors; h_busy++; }
  }
  t.noise = pct(e_noise, t.errors);
  t.freeze = pct(e_freeze, t.errors);
  t.busy = pct(e_busy, t.errors);
  t.noise_hours = (uint8_t)pct(h_noise, hours);
  t.freeze_hours = (uint8_t)pct(h_freeze, hours);
  t.busy_hours = (uint8_t)pct(h_busy, hours);
  return t;
}

uint8_t trendWeekday(uint32_t local_day) {
  // Local day 1 was Thursday 1 Jan 1970.
  return (uint8_t)((local_day + 2) % 7);
}

TrendSchedule trendSchedule(const DiagStore& s, uint32_t today) {
  TrendSchedule r;
  memset(&r, 0, sizeof(r));
  uint8_t wd_loud[24] = {}, we_loud[24] = {};
  uint8_t wd = 0, we = 0;
  for (uint32_t back = 1; back <= STORE_MASK_DAYS && back < today; back++) {
    uint32_t day = today - back;
    uint32_t m = storeLoudMask(s, day);
    if (m == 0xFFFFFF) continue;
    bool weekend = trendWeekday(day) >= 5;
    (weekend ? we : wd)++;
    for (int h = 0; h < 24; h++) {
      if (!(m >> h & 1)) continue;
      if (weekend) we_loud[h]++;
      else wd_loud[h]++;
    }
  }
  r.days_seen = wd + we;
  if (wd < TREND_MIN_DAYS) return r;
  uint32_t daily = 0, weekdays = 0;
  for (int h = 0; h < 24; h++) {
    bool wd_hit = wd_loud[h] * 4 >= wd * 3;
    bool we_hit = we >= 1 && we_loud[h] * 4 >= we * 3;
    bool we_quiet = we >= 1 && we_loud[h] * 4 <= we;
    if (wd_hit && (we == 0 || we_hit)) daily |= 1UL << h;
    else if (wd_hit && we_quiet) weekdays |= 1UL << h;
  }
  if (weekdays) {
    r.found = true;
    r.weekdays_only = true;
    r.hours = weekdays | daily;
  } else if (daily) {
    r.found = true;
    r.hours = daily;
  }
  return r;
}

void trendHealth(const DiagStore& s, uint32_t now_hour, int8_t tz_hours, char* out, size_t len) {
  int w = 0;
  TrendUsual u = trendUsual(s, now_hour, tz_hours);
  if (u.ready) w += snprintf(out + w, len - w, "floor %s", u.now_db >= TREND_ABOVE_DB ? "above usual" : "usual");
  uint32_t day = storeLocalDay(now_hour, tz_hours);
  TrendNb nb = trendWorstNb(s, day);
  if (s.nb_day) w += snprintf(out + w, len - w, "%slinks %s", w ? ", " : "", nb.found ? "fading" : "steady");
  // A week of morning lows gives the battery's daily drift.
  uint16_t first = 0, last = 0;
  int span = 0;
  for (int back = 7; back >= 0; back--) {
    if (day < (uint32_t)back + 1) continue;
    uint16_t mv = storeUnpackMv(storeDay(s, day - back).morning);
    if (!mv) continue;
    if (!first) {
      first = mv;
      span = back;
    }
    last = mv;
  }
  if (first && span >= 3) w += snprintf(out + w, len - w, "%sbatt %+d mV/day", w ? ", " : "", ((int)last - (int)first) / span);
  if (w == 0) snprintf(out, len, "%s", "");
}
