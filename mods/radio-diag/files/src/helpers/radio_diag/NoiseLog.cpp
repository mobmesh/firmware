#include <helpers/radio_diag/NoiseLog.h>
#include <helpers/radio_diag/DiagSummary.h>

#include <stdio.h>
#include <string.h>

#define LOUD_DB        3
#define FLOOR_RUN_DB   1
#define BLOCK_HOURS    4
#define ERR_MIN_PKTS   20
#define ERR_RATIO      2
#define ERR_POINTS     30              // tenths of a percent
#define MIN_HOURS      6
#define SCATTER_RUNS   3
#define MESH_RATIO     2

void noiseAddHour(NoiseLog& log, const NoiseHour& hour) {
  log.h[hour.hour % NOISE_HOURS] = hour;
}

static bool usable(const NoiseHour& h, uint32_t now_hour) {
  return h.hour != 0 && h.hour < now_hour && now_hour - h.hour <= NOISE_HOURS;
}

// Longest run of set flags around the 24-hour circle; false when none is set.
static bool longestRun(const bool flags[24], uint8_t* start, uint8_t* len, int* runs) {
  int all = 0;
  for (int i = 0; i < 24; i++) all += flags[i];
  *runs = 0;
  if (all == 0) return false;
  if (all == 24) {
    *start = 0;
    *len = 24;
    *runs = 1;
    return true;
  }
  int best = 0;
  for (int s = 0; s < 24; s++) {
    if (!flags[s] || flags[(s + 23) % 24]) continue;
    (*runs)++;
    int n = 0;
    while (flags[(s + n) % 24]) n++;
    if (n > best) {
      best = n;
      *start = s;
    }
  }
  *len = best;
  return true;
}

static uint16_t rateTenths(uint32_t recv, uint32_t errors) {
  uint32_t total = recv + errors;
  return total ? (uint16_t)((uint64_t)errors * 1000 / total) : 0;
}

NoiseView noiseAnalyse(const NoiseLog& log, uint32_t now_hour, int8_t tz_hours, uint8_t lna, float margin_db) {
  NoiseView v;
  memset(&v, 0, sizeof(v));
  v.level = DIAG_UNKNOWN;
  bool have[24] = {};
  int8_t floor_at[24] = {};
  uint32_t recv[24] = {}, errs[24] = {};
  bool counted[24] = {};
  for (int i = 0; i < NOISE_HOURS; i++) {
    const NoiseHour& h = log.h[i];
    if (!usable(h, now_hour) || h.lna != lna) continue;
    int local = (int)(((int64_t)h.hour + tz_hours) % 24 + 24) % 24;
    have[local] = true;
    floor_at[local] = h.floor_dbm;
    if (h.counts_valid) {
      counted[local] = true;
      recv[local] = h.recv;
      errs[local] = h.errors;
    }
    if (v.hours == 0 || h.floor_dbm < v.floor_dbm) v.floor_dbm = h.floor_dbm;
    v.hours++;
  }
  if (v.hours == 0) return v;

  bool quiet[24], loud[24], drop[24];
  v.have_margin = margin_db > NOISE_NO_MARGIN / 2;
  for (int l = 0; l < 24; l++) {
    int rise = have[l] ? floor_at[l] - v.floor_dbm : 0;
    quiet[l] = have[l] && rise <= FLOOR_RUN_DB;
    loud[l] = have[l] && rise >= LOUD_DB;
    drop[l] = loud[l] && v.have_margin && rise > margin_db;
    if (have[l] && rise > v.max_rise) v.max_rise = (int8_t)rise;
  }
  int runs;
  longestRun(quiet, &v.floor_start, &v.floor_len, &runs);
  bool any_loud = longestRun(loud, &v.loud_start, &v.loud_len, &runs);
  v.scattered = runs >= SCATTER_RUNS;
  v.drops = longestRun(drop, &v.drop_start, &v.drop_len, &runs);
  if (v.have_margin) {
    float spare = margin_db - v.max_rise;
    v.spare_db = spare < 0 ? 0 : (int8_t)spare;
  }

  uint32_t loud_pkts = 0, loud_n = 0, quiet_pkts = 0, quiet_n = 0;
  for (int l = 0; l < 24; l++) {
    if (!counted[l]) continue;
    if (loud[l]) {
      loud_pkts += recv[l];
      loud_n++;
    } else {
      quiet_pkts += recv[l];
      quiet_n++;
    }
  }
  if (loud_n && quiet_n) {
    v.mesh_known = true;
    v.mesh_busy = loud_pkts * quiet_n >= MESH_RATIO * quiet_pkts * loud_n && loud_pkts > 0;
  }

  // Normal failed-decode rate: the quietest block with enough packets, since every site has some.
  uint32_t b_recv[NOISE_BLOCKS] = {}, b_errs[NOISE_BLOCKS] = {};
  int normal = -1;
  for (int b = 0; b < NOISE_BLOCKS; b++) {
    NoiseBlock& k = v.block[b];
    for (int l = b * BLOCK_HOURS; l < (b + 1) * BLOCK_HOURS; l++) {
      if (!have[l]) continue;
      int8_t rise = (int8_t)(floor_at[l] - v.floor_dbm);
      if (!k.data || rise < k.lo) k.lo = rise;
      if (!k.data || rise > k.hi) k.hi = rise;
      k.data = true;
      b_recv[b] += recv[l];
      b_errs[b] += errs[l];
    }
    k.err_tenths = rateTenths(b_recv[b], b_errs[b]);
    if (b_recv[b] + b_errs[b] < ERR_MIN_PKTS) continue;
    const NoiseBlock& best = v.block[normal < 0 ? b : normal];
    if (normal < 0 || k.hi < best.hi || (k.hi == best.hi && k.err_tenths < best.err_tenths)) normal = b;
  }

  bool loss = false, judged_loud = false, err_any = false;
  v.err_start = 0;
  for (int b = 0; b < NOISE_BLOCKS; b++) {
    NoiseBlock& k = v.block[b];
    if (!k.data) {
      k.level = DIAG_UNKNOWN;
      continue;
    }
    if (normal >= 0 && b != normal && b_recv[b] + b_errs[b] >= ERR_MIN_PKTS) {
      uint16_t base = v.block[normal].err_tenths;
      k.err_high = k.err_tenths >= ERR_RATIO * base && k.err_tenths >= base + ERR_POINTS;
    }
    bool block_drop = false;
    for (int l = b * BLOCK_HOURS; l < (b + 1) * BLOCK_HOURS; l++) block_drop |= drop[l];
    bool block_loud = k.hi >= LOUD_DB;
    if (block_drop || (block_loud && k.err_high)) k.level = DIAG_PROBLEM;
    else if (block_loud || k.err_high) k.level = DIAG_CAUTION;
    else k.level = DIAG_GOOD;
    if (block_loud && k.err_high) loss = true;
    if (block_loud && b_recv[b] + b_errs[b] >= ERR_MIN_PKTS) judged_loud = true;
    if (k.err_high && !err_any) {
      err_any = true;
      v.err_start = (uint8_t)(b * BLOCK_HOURS);
    }
    if (v.level == DIAG_UNKNOWN || k.level > v.level) v.level = k.level;
  }

  if (!any_loud) v.kind = err_any ? NOISE_K_ERRORS : NOISE_K_QUIET;
  else if (loss) v.kind = NOISE_K_LOUD_LOSS;
  else if (v.drops) v.kind = NOISE_K_LOUD_DROP;
  else if (judged_loud) v.kind = NOISE_K_LOUD_OK;
  else v.kind = NOISE_K_LOUD;
  if (v.hours < MIN_HOURS) v.level = DIAG_UNKNOWN;
  return v;
}

void noiseHourText(uint8_t hour, bool brief, char* out, size_t len) {
  hour %= 24;
  int h12 = hour % 12 == 0 ? 12 : hour % 12;
  if (brief) snprintf(out, len, "%d%c", h12, hour < 12 ? 'a' : 'p');
  else snprintf(out, len, "%d%s", h12, hour < 12 ? "am" : "pm");
}

static void spanText(uint8_t start, uint8_t len, char* out, size_t size) {
  char a[8], b[8];
  noiseHourText(start, false, a, sizeof(a));
  noiseHourText((uint8_t)((start + len) % 24), false, b, sizeof(b));
  snprintf(out, size, "%s-%s", a, b);
}

static const char* orb(uint8_t level) {
  switch (level) {
    case DIAG_GOOD: return "\U0001F7E2";
    case DIAG_CAUTION: return "\U0001F7E1";
    case DIAG_PROBLEM: return "\U0001F534";
    default: return "⚪";
  }
}

// "8am-5pm", or "at times" when the loud hours are scattered.
static void loudText(const NoiseView& v, char* out, size_t len) {
  if (v.scattered) snprintf(out, len, "at times");
  else if (v.loud_len == 24) snprintf(out, len, "all day");
  else spanText(v.loud_start, v.loud_len, out, len);
}

static const char* effectText(uint8_t kind) {
  switch (kind) {
    case NOISE_K_LOUD_LOSS: return ", losing packets";
    case NOISE_K_LOUD_DROP: return ", links drop";
    case NOISE_K_LOUD_OK: return ", no loss";
    default: return "";
  }
}

#define ADD(...) do { if (n < max_lines) snprintf(lines[n++], NOISE_LINE, __VA_ARGS__); } while (0)

int noiseFormat(const NoiseView& v, const char* weakest, char lines[][NOISE_LINE], int max_lines) {
  int n = 0;
  if (v.hours == 0) {
    ADD("\U0001F50A Noise: ⚪ watching, 0 of 24h");
    ADD("The first hour closes on the hour.");
    return n;
  }
  char loud[24], span[16], at[8];
  loudText(v, loud, sizeof(loud));
  if (v.hours < MIN_HOURS) ADD("\U0001F50A Noise: ⚪ watching, %u of 24h", v.hours);
  else if (v.kind == NOISE_K_QUIET) ADD("\U0001F50A Noise: %s quiet site", orb(v.level));
  else if (v.kind == NOISE_K_ERRORS) ADD("\U0001F50A Noise: %s errors, not noise", orb(v.level));
  else ADD("\U0001F50A Noise: %s loud %s%s", orb(v.level), loud, effectText(v.kind));

  char floor_when[20] = "";
  if (v.floor_len == 24) snprintf(floor_when, sizeof(floor_when), ", steady");
  else if (v.floor_len == 1) {
    noiseHourText(v.floor_start, false, at, sizeof(at));
    snprintf(floor_when, sizeof(floor_when), " (%s)", at);
  } else {
    spanText(v.floor_start, v.floor_len, span, sizeof(span));
    snprintf(floor_when, sizeof(floor_when), " (%s)", span);
  }
  const char* source = "";
  if (v.loud_len && v.mesh_known) source = v.mesh_busy ? " Busy mesh hours." : " Not mesh traffic.";
  ADD("Floor %d%s.%s", v.floor_dbm, floor_when, source);

  for (int b = 0; b < NOISE_BLOCKS; b++) {
    const NoiseBlock& k = v.block[b];
    char a[6], z[6], label[12], db[20], err[16] = "";
    noiseHourText((uint8_t)(b * BLOCK_HOURS), true, a, sizeof(a));
    noiseHourText((uint8_t)((b + 1) * BLOCK_HOURS), true, z, sizeof(z));
    snprintf(label, sizeof(label), "%s-%s", a, z);
    if (!k.data) snprintf(db, sizeof(db), "no data");
    else if (k.lo == k.hi) snprintf(db, sizeof(db), "+%d dB", k.hi);
    else snprintf(db, sizeof(db), "+%d to +%d dB", k.lo, k.hi);
    if (k.err_high) snprintf(err, sizeof(err), "  %u%% err", (unsigned)((k.err_tenths + 5) / 10));
    ADD("%s %-7s %s%s", orb(k.level), label, db, err);
  }

  if (v.loud_len) {
    if (v.drops) {
      spanText(v.drop_start, v.drop_len, span, sizeof(span));
      ADD("%s drops below its limit %s.", weakest, span);
    } else if (v.have_margin) {
      ADD("Links keep %d+ dB margin through it.", v.spare_db);
    }
    noiseHourText(v.loud_start, false, at, sizeof(at));
    ADD("Try: diag rfscan at %s to find it.", at);
  } else if (v.kind == NOISE_K_ERRORS) {
    noiseHourText(v.err_start, false, at, sizeof(at));
    ADD("Packets failing without a noise rise:");
    ADD("a colliding node or a strong signal.");
    ADD("Try: diag rfscan at %s.", at);
  } else if (v.hours >= MIN_HOURS) {
    ADD("Nothing to fix.");
  }
  if (v.hours >= MIN_HOURS && v.hours < NOISE_HOURS) ADD("%u of 24h watched so far.", v.hours);
  return n;
}

int noiseSummary(const NoiseView& v, char* out, size_t len) {
  if (v.hours < MIN_HOURS) {
    snprintf(out, len, "watching, %u of 24h", v.hours);
    return DIAG_UNKNOWN;
  }
  char loud[24], at[8];
  switch (v.kind) {
    case NOISE_K_QUIET:
      snprintf(out, len, "quiet, floor %d", v.floor_dbm);
      break;
    case NOISE_K_ERRORS:
      noiseHourText(v.err_start, false, at, sizeof(at));
      snprintf(out, len, "errors from %s, no noise rise", at);
      break;
    default:
      loudText(v, loud, sizeof(loud));
      snprintf(out, len, "+%d dB %s%s", v.max_rise, loud, effectText(v.kind));
      break;
  }
  return v.level;
}

bool noiseCounts(const NoiseLog& log, uint32_t now_hour, uint32_t* recv, uint32_t* errors) {
  bool any = false;
  *recv = *errors = 0;
  for (int i = 0; i < NOISE_HOURS; i++) {
    const NoiseHour& h = log.h[i];
    if (!usable(h, now_hour) || !h.counts_valid) continue;
    *recv += h.recv;
    *errors += h.errors;
    any = true;
  }
  return any;
}
