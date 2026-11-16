#include <helpers/diag_reports/DiagStore.h>

#include <math.h>
#include <string.h>

static const StoreDay NO_DAY = {STORE_NO_MV, STORE_NO_TEMP, STORE_NO_TEMP, 0, STORE_NO_LOAD};

void storeInit(DiagStore& s) {
  memset(&s, 0, sizeof(s));
  for (int i = 0; i < 24; i++) s.base[i].floor_dbm = STORE_NO_FLOOR;
  for (int i = 0; i < STORE_NB_MAX; i++) memset(s.nb[i].snr, STORE_NO_SNR, sizeof(s.nb[i].snr));
  s.last_temp = STORE_NO_TEMP;
  for (int i = 0; i < STORE_REBOOTS; i++) s.reboot[i].temp_before = STORE_NO_TEMP;
  for (int i = 0; i < STORE_DAYS; i++) s.d[i] = NO_DAY;
}

uint32_t storeLocalDay(uint32_t hour, int8_t tz_hours) {
  return (hour + 24 + tz_hours) / 24;
}

uint8_t storeLocalHour(uint32_t hour, int8_t tz_hours) {
  return (uint8_t)((hour + 24 + tz_hours) % 24);
}

StoreHour& storeHourSlot(DiagStore& s, uint32_t hour) {
  StoreHour& h = s.h[hour % STORE_HOURS];
  if (h.hour != hour) {
    memset(&h, 0, sizeof(h));
    h.hour = hour;
    h.floor_dbm = STORE_NO_FLOOR;
    h.temp_max = STORE_NO_TEMP;
  }
  return h;
}

const StoreHour* storeHourAt(const DiagStore& s, uint32_t hour) {
  const StoreHour& h = s.h[hour % STORE_HOURS];
  return hour != 0 && h.hour == hour ? &h : nullptr;
}

static uint16_t clamp16(uint32_t v) {
  return v > 0xFFFF ? 0xFFFF : (uint16_t)v;
}

void storeNoiseHour(DiagStore& s, uint32_t hour, int8_t floor_dbm, uint8_t lna, bool counts_valid, uint32_t recv, uint32_t errors) {
  StoreHour& h = storeHourSlot(s, hour);
  h.floor_dbm = floor_dbm;
  h.lna = lna;
  if (counts_valid) {
    h.flags |= HOUR_COUNTS;
    h.recv = clamp16(recv);
    h.errors = clamp16(errors);
  }
}

uint8_t storePackMv(uint16_t mv) {
  if (mv < 2500) return 0;
  uint32_t v = (uint32_t)(mv - 2500 + 4) / 8;
  return v > 254 ? 254 : (uint8_t)v;
}

uint16_t storeUnpackMv(uint8_t v) {
  return v == STORE_NO_MV ? 0 : (uint16_t)(2500 + 8 * v);
}

static int8_t packTemp(float c) {
  if (isnan(c)) return STORE_NO_TEMP;
  long r = lroundf(c);
  return (int8_t)(r < -127 ? -127 : r > 127 ? 127 : r);
}

static void todayStart(StoreToday& t, uint32_t day) {
  t.day = day;
  t.morning_mv = 0;
  t.tmin = t.tmax = STORE_NO_TEMP;
  t.freezes = 0;
  t.load_n = 0;
  t.load_sum = 0;
}

static StoreDay todayRecord(const StoreToday& t) {
  uint8_t load = t.load_n ? (uint8_t)(t.load_sum / t.load_n) : (uint8_t)STORE_NO_LOAD;
  return {t.morning_mv ? storePackMv(t.morning_mv) : (uint8_t)STORE_NO_MV, t.tmin, t.tmax, t.freezes, load};
}

// Files a finished day, marking every day skipped since the last one as unseen.
static void fileDay(DiagStore& s, const StoreToday& t) {
  if (t.day == 0 || (s.last_day != 0 && t.day <= s.last_day)) return;
  uint32_t from = t.day >= STORE_DAYS - 1 ? t.day - (STORE_DAYS - 1) : 1;
  if (s.last_day != 0 && s.last_day + 1 > from) from = s.last_day + 1;
  for (uint32_t day = from; day < t.day; day++) s.d[day % STORE_DAYS] = NO_DAY;
  s.d[t.day % STORE_DAYS] = todayRecord(t);
  s.last_day = t.day;
}

// Starts today, or turns the day; a clock that moved back starts the day afresh and files nothing.
static void turnDay(DiagStore& s, uint32_t day) {
  StoreToday& t = s.today;
  if (t.day == day) return;
  if (t.day != 0 && day > t.day) fileDay(s, t);
  todayStart(t, day);
}

void storeSample(DiagStore& s, uint32_t hour, int8_t tz_hours, uint16_t mv, float temp_c) {
  uint32_t day = storeLocalDay(hour, tz_hours);
  turnDay(s, day);
  StoreToday& t = s.today;
  if (mv != 0 && storeLocalHour(hour, tz_hours) < STORE_NOON && (t.morning_mv == 0 || mv < t.morning_mv)) t.morning_mv = mv;
  int8_t c = packTemp(temp_c);
  if (c != STORE_NO_TEMP) {
    if (t.tmin == STORE_NO_TEMP || c < t.tmin) t.tmin = c;
    if (t.tmax == STORE_NO_TEMP || c > t.tmax) t.tmax = c;
    StoreHour& th = storeHourSlot(s, hour);
    if (th.temp_max == STORE_NO_TEMP || c > th.temp_max) th.temp_max = c;
    if (s.last_temp == STORE_NO_TEMP || c > s.last_temp || storeHourAt(s, hour - 1) == nullptr) s.last_temp = c;
  }
  if (mv) s.last_mv = mv;
  if (mv == 0) return;

  StoreBattDays& b = s.batt;
  if (day != b.day) {
    bool whole = b.day + 1 == day && hour - b.day_hour >= STORE_FULL_HOURS;
    b.full_days = !whole ? 0 : b.full_days < 2 ? b.full_days + 1 : 2;
    b.before_mv = b.yesterday_mv;
    b.yesterday_mv = b.day_mv;
    b.day = day;
    b.day_hour = hour;
    b.day_mv = mv;
  }
  StoreHour& h = storeHourSlot(s, hour);
  if (!(h.flags & HOUR_BATT)) {
    h.flags |= HOUR_BATT;
    h.batt_lo = h.batt_hi = h.batt_first = mv;
  }
  if (mv < h.batt_lo) h.batt_lo = mv;
  if (mv > h.batt_hi) h.batt_hi = mv;
  h.batt_last = mv;
}

void storeFreeze(DiagStore& s, uint32_t epoch, int8_t tz_hours) {
  if (s.guard.freezes < 0xFFFF) s.guard.freezes++;
  s.guard.last_freeze = epoch;
  uint32_t hour = epoch / 3600;
  if (hour == 0) return;
  StoreHour& h = storeHourSlot(s, hour);
  if (h.freezes < 0xFF) h.freezes++;
  turnDay(s, storeLocalDay(hour, tz_hours));
  if (s.today.freezes < 0xFF) s.today.freezes++;
}

void storeLoadHour(DiagStore& s, uint32_t hour, int8_t tz_hours, uint8_t load_pct) {
  if (load_pct > 100) load_pct = 100;
  storeHourSlot(s, hour).load_pct = load_pct;
  turnDay(s, storeLocalDay(hour, tz_hours));
  if (s.today.load_n < 24) {
    s.today.load_sum = (uint16_t)(s.today.load_sum + load_pct);
    s.today.load_n++;
  }
}

static uint32_t recurHours(const StoreRecur& r) {
  return r.hours[0] | ((uint32_t)r.hours[1] << 8) | ((uint32_t)r.hours[2] << 16);
}

void storeRecurSet(DiagStore& s, uint32_t day, uint8_t cause, uint32_t hours) {
  if (day == 0 || (s.recur_day != 0 && day < s.recur_day)) return;
  uint32_t from = day >= STORE_RECUR_DAYS ? day - (STORE_RECUR_DAYS - 1) : 1;
  if (s.recur_day != 0 && s.recur_day + 1 > from) from = s.recur_day + 1;
  for (uint32_t d = from; d < day; d++) s.recur[d % STORE_RECUR_DAYS] = {0, {0, 0, 0}};
  s.recur[day % STORE_RECUR_DAYS] = {cause, {(uint8_t)hours, (uint8_t)(hours >> 8), (uint8_t)(hours >> 16)}};
  uint32_t covered = s.recur_day == 0 ? 1 : s.recur_n + (day - s.recur_day);
  s.recur_n = (uint8_t)(covered > STORE_RECUR_DAYS ? STORE_RECUR_DAYS : covered);
  s.recur_day = day;
}

int storeRecurCount(const DiagStore& s, uint32_t day, uint8_t cause, uint32_t hours, int* recorded) {
  int seen = 0;
  *recorded = 0;
  for (uint32_t back = 0; back < STORE_RECUR_DAYS; back++) {
    if (day < back + 1 || s.recur_day == 0) break;
    uint32_t d = day - back;
    if (d > s.recur_day || d + s.recur_n <= s.recur_day) continue;
    const StoreRecur& r = s.recur[d % STORE_RECUR_DAYS];
    (*recorded)++;
    if (r.cause != cause || cause == 0) continue;
    // A cause with no hour pattern recurs by its id alone.
    if (hours == 0 || recurHours(r) == 0 || (recurHours(r) & hours)) seen++;
  }
  return seen;
}

void storeRebootSeen(DiagStore& s, uint32_t now_epoch) {
  if (s.last_seen == 0 && s.last_mv == 0 && s.last_temp == STORE_NO_TEMP) return;
  for (int i = STORE_REBOOTS - 1; i > 0; i--) s.reboot[i] = s.reboot[i - 1];
  s.reboot[0] = {now_epoch, s.last_seen, s.last_temp, s.reboot_planned, s.last_mv};
  s.reboot_planned = 0;
}

void storeStrong(DiagStore& s, uint32_t epoch) {
  if (s.guard.strong < 0xFFFF) s.guard.strong++;
  s.guard.last_strong = epoch;
  uint32_t hour = epoch / 3600;
  if (hour == 0) return;
  StoreHour& h = storeHourSlot(s, hour);
  if (h.strong < 0xFF) h.strong++;
}

void storeLoraSeen(DiagStore& s, uint32_t epoch, bool foreign, bool ours) {
  uint32_t hour = epoch / 3600;
  if (hour == 0 || (!foreign && !ours)) return;
  StoreHour& h = storeHourSlot(s, hour);
  h.lora |= (foreign ? 1 : 0) | (ours ? 2 : 0);
}

void storeGuardOff(DiagStore& s, uint32_t epoch, uint8_t before, uint8_t after) {
  if (s.guard.off < 0xFFFF) s.guard.off++;
  s.guard.last_off = epoch;
  s.guard.off_before = before;
  s.guard.off_after = after;
  uint32_t hour = epoch / 3600;
  if (hour == 0) return;
  StoreHour& h = storeHourSlot(s, hour);
  if (h.guard_off < 0xFF) h.guard_off++;
}

StoreDay storeDay(const DiagStore& s, uint32_t day) {
  if (s.today.day != 0 && day == s.today.day) return todayRecord(s.today);
  if (s.last_day != 0 && day <= s.last_day && day + STORE_DAYS > s.last_day) return s.d[day % STORE_DAYS];
  return NO_DAY;
}

void histoReset(DbmHistogram& g) {
  memset(&g, 0, sizeof(g));
}

void histoAdd(DbmHistogram& g, int8_t dbm) {
  int index = dbm > 0 ? 0 : -dbm > 127 ? 127 : -dbm;
  if (g.bin[index] < 0xFF) g.bin[index]++;
  g.count++;
}

// Matches a sorted array's element at count / 2: the upper middle of an even count.
int8_t histoMedian(const DbmHistogram& g) {
  uint16_t total = 0;
  for (int i = 0; i < 128; i++) total += g.bin[i];
  if (total == 0) return STORE_NO_FLOOR;
  uint16_t rank = total / 2, seen = 0;
  for (int i = 127; i >= 0; i--) {
    seen += g.bin[i];
    if (seen > rank) return (int8_t)-i;
  }
  return 0;
}

void storeBaseHour(DiagStore& s, uint8_t local_hour, int8_t floor_dbm, bool counts, uint32_t recv, uint32_t errors, uint8_t load_pct) {
  if (floor_dbm == STORE_NO_FLOOR || local_hour >= 24) return;
  StoreBase& b = s.base[local_hour];
  int n = b.n < STORE_BASE_DAYS ? b.n + 1 : STORE_BASE_DAYS;
  if (b.n == 0 || b.floor_dbm == STORE_NO_FLOOR) {
    b.floor_dbm = floor_dbm;
    b.spread = 0;
    b.load_pct = load_pct;
    b.err_tenths = 0;
  } else {
    // A running mean over the last week; the spread follows how far each day lands from it.
    int diff = floor_dbm - b.floor_dbm;
    b.floor_dbm = (int8_t)(b.floor_dbm + diff / n);
    int dev = diff < 0 ? -diff : diff;
    b.spread = (uint8_t)(b.spread + (dev - b.spread) / n);
    b.load_pct = (uint8_t)(b.load_pct + ((int)load_pct - b.load_pct) / n);
  }
  uint32_t total = recv + errors;
  if (counts && total >= 20) {
    int rate = (int)((uint64_t)errors * 1000 / total);
    b.err_tenths = b.n == 0 ? (uint16_t)rate : (uint16_t)(b.err_tenths + (rate - (int)b.err_tenths) / n);
  }
  b.n = (uint8_t)n;
}

void storeNbTurn(DiagStore& s, uint32_t day) {
  if (day == 0 || (s.nb_day != 0 && day <= s.nb_day)) return;
  uint32_t from = s.nb_day && day - s.nb_day < STORE_NB_DAYS ? s.nb_day + 1 : day - (STORE_NB_DAYS - 1);
  for (uint32_t d = from; d <= day; d++) {
    for (int i = 0; i < STORE_NB_MAX; i++) s.nb[i].snr[d % STORE_NB_DAYS] = STORE_NO_SNR;
  }
  s.nb_day = day;
}

void storeNbDay(DiagStore& s, uint32_t day, uint16_t id, uint8_t id_len, int8_t snr_db) {
  storeNbTurn(s, day);
  if (day != s.nb_day) return;
  int slot = -1, stalest = 0, stalest_days = -1;
  for (int i = 0; i < STORE_NB_MAX; i++) {
    StoreNbHistory& h = s.nb[i];
    if (h.id_len && h.id == id && h.id_len == id_len) {
      slot = i;
      break;
    }
    // The slot heard least recently is the one to give up.
    int days = 0;
    for (; days < STORE_NB_DAYS; days++) {
      if (h.id_len && h.snr[(day - days) % STORE_NB_DAYS] != STORE_NO_SNR) break;
    }
    if (days > stalest_days) {
      stalest_days = days;
      stalest = i;
    }
  }
  if (slot < 0) {
    slot = stalest;
    s.nb[slot].id = id;
    s.nb[slot].id_len = id_len;
    memset(s.nb[slot].snr, STORE_NO_SNR, sizeof(s.nb[slot].snr));
  }
  s.nb[slot].snr[day % STORE_NB_DAYS] = snr_db == STORE_NO_SNR ? -127 : snr_db;
}

void storeLoudDay(DiagStore& s, uint32_t day, uint32_t loud_mask) {
  if (day == 0 || (s.loud_day != 0 && day < s.loud_day)) return;
  uint32_t from = s.loud_day && day - s.loud_day < STORE_MASK_DAYS ? s.loud_day + 1 : day - (STORE_MASK_DAYS - 1);
  for (uint32_t d = from; d < day; d++) memset(s.loud_days[d % STORE_MASK_DAYS], 0xFF, 3);
  uint8_t* m = s.loud_days[day % STORE_MASK_DAYS];
  m[0] = (uint8_t)loud_mask;
  m[1] = (uint8_t)(loud_mask >> 8);
  m[2] = (uint8_t)(loud_mask >> 16);
  s.loud_day = day;
}

// 0xFFFFFF marks a day nobody watched; callers treat it as unknown.
uint32_t storeLoudMask(const DiagStore& s, uint32_t day) {
  if (s.loud_day == 0 || day > s.loud_day || day + STORE_MASK_DAYS <= s.loud_day) return 0xFFFFFF;
  const uint8_t* m = s.loud_days[day % STORE_MASK_DAYS];
  return m[0] | ((uint32_t)m[1] << 8) | ((uint32_t)m[2] << 16);
}

void storeRetryAdd(DiagStore& s, uint32_t day, uint32_t retried, uint32_t won) {
  if (day == 0 || (retried == 0 && won == 0)) return;
  int i = day % STORE_RETRY_DAYS;
  StoreRetry& r = s.retry;
  // A clock set back never overwrites a newer day.
  if (r.day[i] > day) return;
  if (r.day[i] != day) {
    r.day[i] = day;
    r.retried[i] = r.won[i] = 0;
  }
  r.retried[i] = (uint16_t)(r.retried[i] + retried > 0xFFFF ? 0xFFFF : r.retried[i] + retried);
  r.won[i] = (uint16_t)(r.won[i] + won > 0xFFFF ? 0xFFFF : r.won[i] + won);
}

void storeRetrySum(const DiagStore& s, uint32_t today, int back_from, int back_to, uint32_t* retried, uint32_t* won) {
  *retried = *won = 0;
  for (int b = back_from; b <= back_to && b < STORE_RETRY_DAYS; b++) {
    if (today < (uint32_t)b) break;
    uint32_t day = today - b;
    int i = day % STORE_RETRY_DAYS;
    if (s.retry.day[i] != day) continue;
    *retried += s.retry.retried[i];
    *won += s.retry.won[i];
  }
}
