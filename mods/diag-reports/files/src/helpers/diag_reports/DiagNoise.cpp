#include <helpers/diag_reports/DiagNoise.h>

#include <stdio.h>
#include <string.h>

#define LOUD_DB        3
#define FLOOR_RUN_DB   1
#define BLOCK_HOURS    4
#define ERR_MIN_PKTS   20
#define ERR_RATIO      2
#define ERR_POINTS     30              // tenths of a percent
#define SCATTER_RUNS   3
#define MESH_RATIO     2

static bool inWindow(const StoreHour& h, uint32_t now_hour) {
  return h.hour != 0 && h.hour < now_hour && now_hour - h.hour <= 24;
}

bool diagLongestRun(const bool flags[24], uint8_t* start, uint8_t* len, int* runs) {
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
      *start = (uint8_t)s;
    }
  }
  *len = (uint8_t)best;
  return true;
}

static uint16_t rateTenths(uint32_t recv, uint32_t errors) {
  uint32_t total = recv + errors;
  return total ? (uint16_t)((uint64_t)errors * 1000 / total) : 0;
}

NoiseView noiseAnalyse(const DiagStore& s, uint32_t now_hour, int8_t tz_hours, uint8_t lna, float margin_db) {
  NoiseView v;
  memset(&v, 0, sizeof(v));
  v.level = DIAG_UNKNOWN;
  bool have[24] = {}, counted[24] = {}, froze[24] = {};
  int8_t floor_at[24] = {};
  uint16_t recv[24] = {}, errs[24] = {};
  uint8_t froze_n[24] = {};
  for (int i = 0; i < STORE_HOURS; i++) {
    const StoreHour& h = s.h[i];
    if (!inWindow(h, now_hour)) continue;
    int local = storeLocalHour(h.hour, tz_hours);
    if (h.freezes) {
      // A frozen receiver reads a false floor, so the hour is labelled and left out of the noise figures.
      froze[local] = true;
      v.freeze_mask |= 1UL << local;
      froze_n[local] = h.freezes;
      v.freezes = (uint8_t)(v.freezes + h.freezes < 255 ? v.freezes + h.freezes : 255);
      continue;
    }
    if (h.floor_dbm == STORE_NO_FLOOR || h.lna != lna) continue;
    have[local] = true;
    v.have_mask |= 1UL << local;
    floor_at[local] = h.floor_dbm;
    if (h.flags & HOUR_COUNTS) {
      counted[local] = true;
      recv[local] = h.recv;
      errs[local] = h.errors;
    }
    if (v.hours == 0 || h.floor_dbm < v.floor_dbm) v.floor_dbm = h.floor_dbm;
    v.hours++;
  }
  int runs;
  diagLongestRun(froze, &v.freeze_start, &v.freeze_len, &runs);
  if (v.hours == 0) {
    if (v.freezes) v.kind = NOISE_K_FREEZES;
    return v;
  }

  bool quiet[24], loud[24], drop[24];
  v.have_margin = margin_db > NOISE_NO_MARGIN / 2;
  for (int l = 0; l < 24; l++) {
    int rise = have[l] ? floor_at[l] - v.floor_dbm : 0;
    quiet[l] = have[l] && rise <= FLOOR_RUN_DB;
    loud[l] = have[l] && rise >= LOUD_DB;
    drop[l] = loud[l] && v.have_margin && rise > margin_db;
    if (loud[l]) v.loud_mask |= 1UL << l;
    if (have[l] && rise > v.max_rise) v.max_rise = (int8_t)rise;
  }
  diagLongestRun(quiet, &v.floor_start, &v.floor_len, &runs);
  bool any_loud = diagLongestRun(loud, &v.loud_start, &v.loud_len, &runs);
  v.scattered = runs >= SCATTER_RUNS;
  v.drops = diagLongestRun(drop, &v.drop_start, &v.drop_len, &runs);
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
      k.freezes = (uint8_t)(k.freezes + froze_n[l]);
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
  for (int b = 0; b < NOISE_BLOCKS; b++) {
    NoiseBlock& k = v.block[b];
    if (!k.data) {
      k.level = k.freezes ? DIAG_CAUTION : DIAG_UNKNOWN;
      if (k.freezes && (v.level == DIAG_UNKNOWN || v.level < DIAG_CAUTION)) v.level = DIAG_CAUTION;
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
    else if (block_loud || k.err_high || k.freezes) k.level = DIAG_CAUTION;
    else k.level = DIAG_GOOD;
    if (block_loud && k.err_high) loss = true;
    if (block_loud && b_recv[b] + b_errs[b] >= ERR_MIN_PKTS) judged_loud = true;
    if (k.err_high) v.err_mask |= 0xFUL << (b * BLOCK_HOURS);
    if (k.err_high && !err_any) {
      err_any = true;
      v.err_start = (uint8_t)(b * BLOCK_HOURS);
    }
    if (v.level == DIAG_UNKNOWN || k.level > v.level) v.level = k.level;
  }

  if (!any_loud) v.kind = v.freezes ? NOISE_K_FREEZES : err_any ? NOISE_K_ERRORS : NOISE_K_QUIET;
  else if (loss) v.kind = NOISE_K_LOUD_LOSS;
  else if (v.drops) v.kind = NOISE_K_LOUD_DROP;
  else if (judged_loud) v.kind = NOISE_K_LOUD_OK;
  else v.kind = NOISE_K_LOUD;
  if (v.hours < NOISE_MIN_HOURS) v.level = DIAG_UNKNOWN;
  return v;
}

void noiseMarkBusy(NoiseView& v) {
  if (!v.loud_len || v.kind == NOISE_K_LOUD_LOSS) return;
  v.kind = NOISE_K_BUSY;
  if (v.level == DIAG_PROBLEM) v.level = DIAG_CAUTION;
}

void noiseLoudText(const NoiseView& v, char* out, size_t len) {
  if (v.scattered) snprintf(out, len, "at times");
  else if (v.loud_len == 24) snprintf(out, len, "all day");
  else diagSpanText(v.loud_start, v.loud_len, out, len);
}

static const char* effectText(uint8_t kind) {
  switch (kind) {
    case NOISE_K_LOUD_LOSS: return ", Losing Packets";
    case NOISE_K_LOUD_DROP: return ", Links Drop";
    case NOISE_K_LOUD_OK: return ", No Loss";
    default: return "";
  }
}

// The loud text in title case for a header.
static void loudTitle(const NoiseView& v, char* out, size_t len) {
  noiseLoudText(v, out, len);
  if (out[0] == 'a') {
    out[0] = 'A';
    char* word = strchr(out, ' ');
    if (word && word[1]) word[1] = (char)(word[1] - 'a' + 'A');
  }
}

static void header(const NoiseView& v, char* out, size_t len) {
  char loud[24];
  if (v.hours < NOISE_MIN_HOURS && v.kind != NOISE_K_FREEZES) snprintf(out, len, "⚪ Watching, %u of 24h", v.hours);
  else if (v.kind == NOISE_K_QUIET) snprintf(out, len, "%s Quiet Site", diagOrb(v.level));
  else if (v.kind == NOISE_K_ERRORS) snprintf(out, len, "%s Errors, Not Noise", diagOrb(v.level));
  else if (v.kind == NOISE_K_FREEZES) snprintf(out, len, "%s Freezes, Not Noise", diagOrb(DIAG_CAUTION));
  else if (v.kind == NOISE_K_BUSY) {
    loudTitle(v, loud, sizeof(loud));
    snprintf(out, len, "%s Busy %s, Not Noise", diagOrb(v.level), loud);
  }
  else {
    loudTitle(v, loud, sizeof(loud));
    snprintf(out, len, "%s Loud %s%s", diagOrb(v.level), loud, effectText(v.kind));
  }
}

void noiseFormat(const NoiseView& v, const char* weakest, const StoreCapture* capture, int8_t tz_hours, const DiagNote* note, DiagOut& o) {
  char text[64], span[20], at[8];
  if (v.hours == 0 && v.freezes == 0) {
    outLine(o, "\U0001F50A Noise: ⚪ Watching, 0 of 24h");
    outLine(o, "The first hour closes on the hour.");
    return;
  }
  header(v, text, sizeof(text));
  outLine(o, "\U0001F50A Noise: %s", text);

  if (v.hours) {
    if (v.floor_len == 24) snprintf(span, sizeof(span), "steady");
    else if (v.floor_len == 1) diagHourText(v.floor_start, false, span, sizeof(span));
    else diagSpanText(v.floor_start, v.floor_len, span, sizeof(span));
    outLine(o, "Floor %d dBm, %s%s", v.floor_dbm, v.floor_len == 24 ? "" : "quietest ", span);
  }
  for (int b = 0; b < NOISE_BLOCKS; b++) {
    const NoiseBlock& k = v.block[b];
    char a[6], z[6], label[12];
    diagHourText((uint8_t)(b * BLOCK_HOURS), true, a, sizeof(a));
    diagHourText((uint8_t)((b + 1) * BLOCK_HOURS), true, z, sizeof(z));
    snprintf(label, sizeof(label), "%s-%s", a, z);
    int w = 0;
    if (!k.data) w = snprintf(text, sizeof(text), k.freezes ? "" : "no data");
    else if (k.lo == k.hi) w = snprintf(text, sizeof(text), "+%d dB", k.hi);
    else w = snprintf(text, sizeof(text), "+%d to +%d dB", k.lo, k.hi);
    if (k.err_high) w += snprintf(text + w, sizeof(text) - w, "  %u%% err", (unsigned)((k.err_tenths + 5) / 10));
    if (k.freezes) snprintf(text + w, sizeof(text) - w, "%sfroze %ux", w ? ", " : "", k.freezes);
    outLine(o, "%s %-7s %s", diagOrb(k.level), label, text);
  }

  bool watching = v.hours < NOISE_MIN_HOURS;
  outSep(o);
  if (notePrint(note, o)) {
    if (v.hours < 24) outLine(o, "%u of 24h watched so far.", v.hours);
    return;
  }
  if (capture && capture->at) {
    uint32_t local = capture->at + (int32_t)tz_hours * 3600;
    const char* what = capture->kind ? "steady carrier, not packets" : "packet bursts, a busy mesh";
    outLine(o, "Caught %lu:%02lu: %s", (unsigned long)(local / 3600 % 24), (unsigned long)(local / 60 % 60), what);
    outLine(o, "(%d to %d dBm over 2 s).", capture->a, capture->b);
  }
  if (v.freezes) {
    if (v.freeze_len == 1) diagHourText(v.freeze_start, false, span, sizeof(span));
    else diagSpanText(v.freeze_start, v.freeze_len, span, sizeof(span));
    outLine(o, "Receiver froze %ux, %s: not noise.", v.freezes, span);
    outLine(o, "See: diag");
  }
  if (v.loud_len) {
    if (v.mesh_known) outLine(o, v.mesh_busy ? "Busy mesh hours." : "Not mesh traffic.");
    if (v.drops) {
      diagSpanText(v.drop_start, v.drop_len, span, sizeof(span));
      outLine(o, "%s drops below its limit %s.", weakest, span);
    } else if (v.have_margin) {
      outLine(o, "Links keep %d+ dB margin through it.", v.spare_db);
    }
    diagHourText(v.loud_start, false, at, sizeof(at));
    outLine(o, "Try: diag radio.rfscan at %s.", at);
  } else if (v.kind == NOISE_K_ERRORS) {
    diagHourText(v.err_start, false, at, sizeof(at));
    outLine(o, "Packets fail with no noise rise:");
    outLine(o, "a colliding node or strong signal.");
    outLine(o, "Try: diag radio.rfscan at %s.", at);
  } else if (!watching && !v.freezes) {
    outLine(o, "Nothing to fix.");
  }
  if (v.hours < 24) outLine(o, "%u of 24h watched so far.", v.hours);
}

int noiseSummary(const NoiseView& v, char* out, size_t len) {
  char loud[24];
  if (v.kind == NOISE_K_FREEZES) {
    snprintf(out, len, "Freezes, Not Noise");
    return DIAG_CAUTION;
  }
  if (v.hours < NOISE_MIN_HOURS) {
    snprintf(out, len, "watching, %u of 24h", v.hours);
    return DIAG_UNKNOWN;
  }
  switch (v.kind) {
    case NOISE_K_QUIET:
      snprintf(out, len, "Quiet Site");
      break;
    case NOISE_K_ERRORS:
      snprintf(out, len, "Errors, Not Noise");
      break;
    case NOISE_K_BUSY:
      loudTitle(v, loud, sizeof(loud));
      snprintf(out, len, "busy %s, mesh", loud);
      break;
    default:
      loudTitle(v, loud, sizeof(loud));
      snprintf(out, len, "Loud %s", loud);
      break;
  }
  return v.level;
}

bool noiseCounts(const DiagStore& s, uint32_t now_hour, uint32_t* recv, uint32_t* errors) {
  bool any = false;
  *recv = *errors = 0;
  for (int i = 0; i < STORE_HOURS; i++) {
    const StoreHour& h = s.h[i];
    if (!inWindow(h, now_hour) || !(h.flags & HOUR_COUNTS)) continue;
    *recv += h.recv;
    *errors += h.errors;
    any = true;
  }
  return any;
}
