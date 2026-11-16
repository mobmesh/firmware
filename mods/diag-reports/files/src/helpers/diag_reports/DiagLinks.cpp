#include <helpers/diag_reports/DiagLinks.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SNR_SLOW_AFTER  8
#define ONE_WAY_PKTS    10
#define ONE_WAY_ECHOES  20
#define CLOCK_MIN_NB    3
#define CLOCK_OFF_S     120
#define TABLE_ROWS      8
#define PATHS_MIN_PKTS  20
#define PATHS_ONE_PCT   70
#define DROP_MIN_HOURS  2
#define LNA_ONLY_PKTS   5

float linkSnrLimit(uint8_t sf) {
  return -2.5f * ((int)sf - 4);
}

void linkInit(LinkTable& t) {
  memset(&t, 0, sizeof(t));
  t.hour_of_day = 0xFF;
}

int linkIndex(LinkTable& t, uint16_t id, uint8_t id_len, uint32_t now_ms) {
  // One node can be seen by its 1-byte and its 2-byte hash; when only one neighbour fits, both count as it.
  int short_form = -1, long_form = -1, long_forms = 0;
  uint16_t first = id_len == 2 ? id >> 8 : id;
  for (int i = 0; i < t.count; i++) {
    const LinkNeighbour& n = t.nb[i];
    if (n.id == id && n.id_len == id_len) return i;
    if (n.id_len == 1 && n.id == first) short_form = i;
    if (n.id_len == 2 && (n.id >> 8) == first) {
      long_form = i;
      long_forms++;
    }
  }
  if (id_len == 1 && long_forms == 1) return long_form;
  if (id_len == 2 && short_form >= 0 && long_forms == 0) {
    t.nb[short_form].id = id;
    t.nb[short_form].id_len = 2;
    return short_form;
  }
  int slot = t.count;
  if (t.count < LINKS_MAX) {
    t.count++;
  } else {
    slot = 0;
    for (int i = 1; i < LINKS_MAX; i++) {
      if (now_ms - t.nb[i].last_ms > now_ms - t.nb[slot].last_ms) slot = i;
    }
  }
  LinkNeighbour& n = t.nb[slot];
  memset(&n, 0, sizeof(n));
  n.id = id;
  n.id_len = id_len;
  n.last_ms = now_ms;
  return slot;
}

void linkAddPacket(LinkTable& t, int index, float snr_db, uint8_t sf, uint8_t lna, uint32_t now_ms) {
  LinkNeighbour& n = t.nb[index];
  if (n.packets < 0xFFFF) n.packets++;
  int weight = n.packets < SNR_SLOW_AFTER ? n.packets : SNR_SLOW_AFTER;
  n.snr_db += (snr_db - n.snr_db) / weight;
  n.last_ms = now_ms;
  if (lna < 2 && n.lna_pkts[lna] < 0xFFFF) n.lna_pkts[lna]++;
  if (t.hour_of_day < 24) {
    n.heard |= 1UL << t.hour_of_day;
    if (snr_db - linkSnrLimit(sf) < LINKS_CLOSE_DB) n.weak |= 1UL << t.hour_of_day;
  }
}

void linkAddEcho(LinkTable& t, int index) {
  if (t.nb[index].echoes < 0xFFFF) t.nb[index].echoes++;
  t.echoes++;
}

void linkAddClock(LinkTable& t, int index, int32_t offset_s) {
  t.nb[index].clock_off_s = offset_s;
  t.nb[index].clock_n = 1;
}

void linkRollHour(LinkTable& t, uint8_t hour_of_day) {
  t.hour_of_day = hour_of_day;
  if (hour_of_day >= 24) return;
  for (int i = 0; i < t.count; i++) {
    t.nb[i].heard &= ~(1UL << hour_of_day);
    t.nb[i].weak &= ~(1UL << hour_of_day);
  }
}

void linkClockShift(LinkTable& t, int32_t delta_s) {
  for (int i = 0; i < t.count; i++) {
    if (t.nb[i].clock_n) t.nb[i].clock_off_s -= delta_s;
  }
}

static bool fresh(const LinkNeighbour& n, uint32_t now_ms) {
  return now_ms - n.last_ms < LINKS_STALE_MS;
}

static bool judged(const LinkNeighbour& n, uint32_t now_ms) {
  return n.packets >= LINKS_MIN_PKTS && fresh(n, now_ms);
}

static float margin(const LinkNeighbour& n, uint8_t sf) {
  return n.snr_db - linkSnrLimit(sf);
}

float linkMargin(const LinkTable& t, int index, uint8_t sf) {
  return margin(t.nb[index], sf);
}

void linkLabel(const LinkTable& t, int index, char* out, size_t len) {
  const LinkNeighbour& n = t.nb[index];
  if (n.id_len == 2) snprintf(out, len, "%04X", n.id);
  else snprintf(out, len, "%02X", n.id);
}

int linkOneWay(const LinkTable& t, uint8_t sf, uint32_t now_ms) {
  if (t.echoes < ONE_WAY_ECHOES) return -1;
  for (int i = 0; i < t.count; i++) {
    const LinkNeighbour& n = t.nb[i];
    if (judged(n, now_ms) && n.packets >= ONE_WAY_PKTS && n.echoes == 0 && margin(n, sf) >= LINKS_SOLID_DB) return i;
  }
  return -1;
}

static int bits(uint32_t v) {
  int n = 0;
  for (; v; v &= v - 1) n++;
  return n;
}

int linkDropsIn(const LinkTable& t, uint32_t mask_utc, uint32_t watched_utc, uint32_t now_ms) {
  mask_utc &= 0xFFFFFF;
  // Only hours actually watched count as the other side of the comparison, so a half-day watch can still judge.
  uint32_t others = (watched_utc ? watched_utc & 0xFFFFFF : 0xFFFFFF) & ~mask_utc;
  int masked = bits(mask_utc), open = bits(others);
  if (masked < DROP_MIN_HOURS || open < DROP_MIN_HOURS) return -1;
  for (int i = 0; i < t.count; i++) {
    const LinkNeighbour& n = t.nb[i];
    if (!judged(n, now_ms)) continue;
    // Present in at least half of the other hours, yet silent or weak in two thirds of the masked ones.
    int good_open = bits(n.heard & ~n.weak & others);
    int bad_masked = masked - bits(n.heard & ~n.weak & mask_utc);
    if (good_open * 2 >= open && bad_masked * 3 >= masked * 2) return i;
  }
  return -1;
}

int linkLnaOnly(const LinkTable& t, uint32_t now_ms) {
  uint32_t off_total = 0;
  for (int i = 0; i < t.count; i++) off_total += t.nb[i].lna_pkts[0];
  // Without real time spent with the LNA off, nothing can be called LNA-only.
  if (off_total < ONE_WAY_ECHOES) return -1;
  for (int i = 0; i < t.count; i++) {
    const LinkNeighbour& n = t.nb[i];
    if (fresh(n, now_ms) && n.lna_pkts[1] >= LNA_ONLY_PKTS && n.lna_pkts[0] == 0) return i;
  }
  return -1;
}

// Sign +1 when most neighbours run ahead of this clock (it is slow), -1 when behind (it is fast), 0 otherwise.
static int clockSkew(const LinkTable& t, uint32_t now_ms, int* agree, int* total, int32_t* typical_s) {
  int32_t offs[LINKS_MAX];
  int ahead = 0, behind = 0, n = 0;
  for (int i = 0; i < t.count; i++) {
    if (!t.nb[i].clock_n || !fresh(t.nb[i], now_ms)) continue;
    int32_t off = t.nb[i].clock_off_s;
    if (off > CLOCK_OFF_S) ahead++;
    if (off < -CLOCK_OFF_S) behind++;
    n++;
  }
  *total = n;
  if (n < CLOCK_MIN_NB) return 0;
  int sign = ahead * 3 >= n * 2 ? 1 : behind * 3 >= n * 2 ? -1 : 0;
  if (sign == 0) return 0;
  int k = 0;
  for (int i = 0; i < t.count; i++) {
    if (!t.nb[i].clock_n || !fresh(t.nb[i], now_ms)) continue;
    int32_t off = t.nb[i].clock_off_s;
    if (sign > 0 ? off > CLOCK_OFF_S : off < -CLOCK_OFF_S) offs[k++] = off < 0 ? -off : off;
  }
  for (int i = 1; i < k; i++) {
    int32_t v = offs[i];
    int j = i - 1;
    while (j >= 0 && offs[j] > v) {
      offs[j + 1] = offs[j];
      j--;
    }
    offs[j + 1] = v;
  }
  *agree = k;
  *typical_s = offs[k / 2];
  return sign;
}

struct Tally {
  int judged, fresh, strong, fair, weak, hearing, worst;
};

static Tally tally(const LinkTable& t, uint8_t sf, uint32_t now_ms) {
  Tally a = {0, 0, 0, 0, 0, 0, -1};
  for (int i = 0; i < t.count; i++) {
    if (fresh(t.nb[i], now_ms)) a.fresh++;
    if (!judged(t.nb[i], now_ms)) continue;
    a.judged++;
    float m = margin(t.nb[i], sf);
    if (m < LINKS_CLOSE_DB) a.weak++;
    else if (m < LINKS_SOLID_DB) a.fair++;
    else a.strong++;
    if (t.nb[i].echoes) a.hearing++;
    if (a.worst < 0 || m < margin(t.nb[a.worst], sf)) a.worst = i;
  }
  return a;
}

LinkVerdict linkVerdict(const LinkTable& t, uint8_t sf, uint32_t now_ms) {
  Tally a = tally(t, sf, now_ms);
  if (a.judged == 0) return LINK_V_NOT_ENOUGH;
  if (a.judged >= 3 && a.weak * 3 >= a.judged * 2) return LINK_V_MOST_WEAK;
  if (linkOneWay(t, sf, now_ms) >= 0) return LINK_V_ONE_WAY;
  if (a.weak > 0) return LINK_V_CLOSE;
  if (a.fair > 0) return LINK_V_CAN_DROP;
  int agree, total;
  int32_t typical;
  if (clockSkew(t, now_ms, &agree, &total, &typical) != 0) return LINK_V_CLOCK;
  return LINK_V_SOLID;
}

bool linkWeakest(const LinkTable& t, uint8_t sf, uint32_t now_ms, float* margin_db, char* id, size_t len) {
  Tally a = tally(t, sf, now_ms);
  if (a.worst < 0) return false;
  *margin_db = margin(t.nb[a.worst], sf);
  linkLabel(t, a.worst, id, len);
  return true;
}

static void spanText(int32_t s, char* out, size_t len) {
  if (s < 7200) snprintf(out, len, "%ld min", (long)(s / 60));
  else if (s < 172800) snprintf(out, len, "%ld h", (long)(s / 3600));
  else snprintf(out, len, "%ld days", (long)(s / 86400));
}

// Judged neighbours, strongest first: status, id, SNR, margin, then the one note that matters most.
static void addTable(const LinkTable& t, uint8_t sf, uint32_t now_ms, const LinkContext* context, DiagOut& o) {
  int order[LINKS_MAX], k = 0;
  for (int i = 0; i < t.count; i++) {
    if (judged(t.nb[i], now_ms)) order[k++] = i;
  }
  for (int i = 1; i < k; i++) {
    int v = order[i];
    int j = i - 1;
    while (j >= 0 && margin(t.nb[order[j]], sf) < margin(t.nb[v], sf)) {
      order[j + 1] = order[j];
      j--;
    }
    order[j + 1] = v;
  }
  int drops = context && context->loud_utc ? linkDropsIn(t, context->loud_utc, context->watched_utc, now_ms) : -1;
  int lna_only = context && context->lna_available ? linkLnaOnly(t, now_ms) : -1;
  for (int r = 0; r < k && r < TABLE_ROWS; r++) {
    const LinkNeighbour& nb = t.nb[order[r]];
    char id[8], note[24] = "";
    linkLabel(t, order[r], id, sizeof(id));
    float m = margin(nb, sf);
    const char* mark = m >= LINKS_SOLID_DB ? "✅" : m >= LINKS_CLOSE_DB ? "\U0001F7E1" : "\U0001F534";
    if (order[r] == drops) {
      mark = "\U0001F7E1";
      snprintf(note, sizeof(note), "drops %s", context->loud_text ? context->loud_text : "in noise");
    } else if (order[r] == lna_only) snprintf(note, sizeof(note), "LNA-only");
    else if (nb.echoes) snprintf(note, sizeof(note), "hears us");
    outLine(o, "%s %-4s %+.1f %2d dB %s", mark, id, nb.snr_db, (int)lroundf(m), note);
  }
}

void linkFormat(const LinkTable& t, uint8_t sf, uint32_t now_ms, const LinkContext* context, const DiagNote* note, DiagOut& o) {
  Tally a = tally(t, sf, now_ms);
  char id[8];
  LinkVerdict verdict = linkVerdict(t, sf, now_ms);
  int drops = context && context->loud_utc && verdict != LINK_V_NOT_ENOUGH ? linkDropsIn(t, context->loud_utc, context->watched_utc, now_ms) : -1;
  if (drops >= 0 && verdict != LINK_V_MOST_WEAK) {
    linkLabel(t, drops, id, sizeof(id));
    outLine(o, "\U0001F517 Links: \U0001F7E1 %s drops in noise", id);
    addTable(t, sf, now_ms, context, o);
    outSep(o);
    if (notePrint(note, o)) return;
    outLine(o, "Cause: noise %s, not %s.", context->loud_text ? context->loud_text : "at times", id);
    outLine(o, "See: diag radio.noise");
    return;
  }
  switch (verdict) {
    case LINK_V_NOT_ENOUGH: {
      outLine(o, "\U0001F517 Links: ⚪ Not Enough Data");
      outLine(o, "%d heard, need %d packets each.", a.fresh, LINKS_MIN_PKTS);
      char row[40] = "";
      for (int i = 0; i < t.count; i++) {
        if (!fresh(t.nb[i], now_ms)) continue;
        char part[20];
        linkLabel(t, i, id, sizeof(id));
        snprintf(part, sizeof(part), "%s%s %u", row[0] ? ", " : "", id, t.nb[i].packets);
        if (strlen(row) + strlen(part) > 34) {
          outLine(o, "%s", row);
          snprintf(row, sizeof(row), "%s %u", id, t.nb[i].packets);
        } else {
          strcat(row, part);
        }
      }
      if (row[0]) outLine(o, "%s", row);
      outSep(o);
      if (notePrint(note, o)) return;
    if (notePrint(note, o)) return;
      outLine(o, "Check again later.");
      return;
    }
    case LINK_V_MOST_WEAK:
      outLine(o, "\U0001F517 Links: \U0001F534 Most Links Weak");
      addTable(t, sf, now_ms, context, o);
      outSep(o);
      if (notePrint(note, o)) return;
    if (notePrint(note, o)) return;
      outLine(o, "%d of %d under 3 dB margin: weak on", a.weak, a.judged);
      outLine(o, "all sides points here, not at them.");
      outLine(o, "Check antenna, cable, connector.");
      return;
    case LINK_V_ONE_WAY: {
      int index = linkOneWay(t, sf, now_ms);
      linkLabel(t, index, id, sizeof(id));
      outLine(o, "\U0001F517 Links: \U0001F7E1 One-Way Link");
      addTable(t, sf, now_ms, context, o);
      outSep(o);
      if (notePrint(note, o)) return;
    if (notePrint(note, o)) return;
      outLine(o, "%s heard well (%d dB margin), but", id, (int)lroundf(margin(t.nb[index], sf)));
      outLine(o, "none of %lu echoes came via it.", (unsigned long)t.echoes);
      outLine(o, "See: diag probe");
      return;
    }
    case LINK_V_CLOSE:
    case LINK_V_CAN_DROP:
      linkLabel(t, a.worst, id, sizeof(id));
      if (a.weak) outLine(o, "\U0001F517 Links: \U0001F7E1 %d Link%s Close to Dropping", a.weak, a.weak == 1 ? "" : "s");
      else outLine(o, "\U0001F517 Links: \U0001F7E1 %d Link%s Can Drop", a.fair, a.fair == 1 ? "" : "s");
      addTable(t, sf, now_ms, context, o);
      outSep(o);
      if (notePrint(note, o)) return;
    if (notePrint(note, o)) return;
      outLine(o, "%s drops first in rain or foliage.", id);
      outLine(o, "A higher antenna or a relay helps.");
      return;
    case LINK_V_CLOCK: {
      int agree, total;
      int32_t typical;
      int sign = clockSkew(t, now_ms, &agree, &total, &typical);
      char span[16];
      spanText(typical, span, sizeof(span));
      outLine(o, "\U0001F517 Links: \U0001F7E1 Our Clock Off");
      addTable(t, sf, now_ms, context, o);
      outSep(o);
      if (notePrint(note, o)) return;
    if (notePrint(note, o)) return;
      outLine(o, "%d of %d neighbours' clocks are", agree, total);
      outLine(o, "%s %s: ours is %s.", span, sign > 0 ? "ahead" : "behind", sign > 0 ? "slow" : "fast");
      outLine(o, "Sync the clock from the app.");
      return;
    }
    case LINK_V_SOLID:
      outLine(o, "\U0001F517 Links: \U0001F7E2 All Solid");
      addTable(t, sf, now_ms, context, o);
      outSep(o);
      if (notePrint(note, o)) return;
    if (notePrint(note, o)) return;
      outLine(o, "%d neighbour%s, all 6+ dB margin.", a.judged, a.judged == 1 ? "" : "s");
      return;
  }
}

int linkSummary(const LinkTable& t, uint8_t sf, uint32_t now_ms, char* out, size_t len) {
  Tally a = tally(t, sf, now_ms);
  char id[8] = "";
  if (a.worst >= 0) linkLabel(t, a.worst, id, sizeof(id));
  switch (linkVerdict(t, sf, now_ms)) {
    case LINK_V_NOT_ENOUGH:
      snprintf(out, len, "%d heard, need %d pkts", a.fresh, LINKS_MIN_PKTS);
      return DIAG_UNKNOWN;
    case LINK_V_MOST_WEAK:
      snprintf(out, len, "%d of %d weak", a.weak, a.judged);
      return DIAG_PROBLEM;
    case LINK_V_ONE_WAY:
      linkLabel(t, linkOneWay(t, sf, now_ms), id, sizeof(id));
      snprintf(out, len, "%s one-way", id);
      return DIAG_CAUTION;
    case LINK_V_CLOSE:
      snprintf(out, len, "%s weak", id);
      return DIAG_CAUTION;
    case LINK_V_CAN_DROP:
      snprintf(out, len, "%d strong, %d fair", a.strong, a.fair);
      return DIAG_CAUTION;
    default:
      snprintf(out, len, "%d strong%s", a.judged, a.hearing == a.judged ? ", all hear us" : "");
      return DIAG_GOOD;
  }
}

int linkTopShare(const LinkTable& t, uint32_t now_ms, int* top, int* neighbours) {
  uint32_t total = 0;
  *top = -1;
  *neighbours = 0;
  for (int i = 0; i < t.count; i++) {
    if (!fresh(t.nb[i], now_ms)) continue;
    total += t.nb[i].packets;
    (*neighbours)++;
    if (*top < 0 || t.nb[i].packets > t.nb[*top].packets) *top = i;
  }
  if (total < PATHS_MIN_PKTS) return -(int)total - 1;
  return (int)(t.nb[*top].packets * 100UL / total);
}

int linkPathsSummary(const LinkTable& t, uint32_t now_ms, char* out, size_t len) {
  int top, n;
  int pct = linkTopShare(t, now_ms, &top, &n);
  if (pct < 0) {
    snprintf(out, len, "%d of %d pkts", -pct - 1, PATHS_MIN_PKTS);
    return DIAG_UNKNOWN;
  }
  char id[8];
  linkLabel(t, top, id, sizeof(id));
  if (n == 1) {
    snprintf(out, len, "all via %s", id);
    return DIAG_CAUTION;
  }
  if (pct >= PATHS_ONE_PCT) {
    snprintf(out, len, "%d%% via %s", pct, id);
    return DIAG_CAUTION;
  }
  snprintf(out, len, "spread over %d", n);
  return DIAG_GOOD;
}

int linkClockSummary(const LinkTable& t, uint32_t now_ms, char* out, size_t len) {
  int agree, total;
  int32_t typical;
  int sign = clockSkew(t, now_ms, &agree, &total, &typical);
  if (total < CLOCK_MIN_NB) {
    snprintf(out, len, "%d of %d adverts", total, CLOCK_MIN_NB);
    return DIAG_UNKNOWN;
  }
  if (sign == 0) {
    snprintf(out, len, "in step");
    return DIAG_GOOD;
  }
  char span[16];
  spanText(typical, span, sizeof(span));
  snprintf(out, len, "ours %s %s", span, sign > 0 ? "slow" : "fast");
  return DIAG_CAUTION;
}

bool linkRetryParse(const char* reply, uint32_t* retried, uint32_t* won) {
  const char* r = strstr(reply, "retried=");
  const char* w = strstr(reply, "won=");
  if (!r || !w) return false;
  *retried = strtoul(r + 8, nullptr, 10);
  *won = strtoul(w + 4, nullptr, 10);
  return true;
}

int linkRetryHops(const char* reply, LinkRetryHop* out, int max) {
  int n = 0;
  const char* p = strchr(reply, '>');
  if (!p) return 0;
  p++;
  while (*p && n < max) {
    while (*p == ' ') p++;
    char* end = nullptr;
    unsigned long id = strtoul(p, &end, 16);
    if (end == p || *end != ':') break;
    int digits = (int)(end - p);
    unsigned long tried = strtoul(end + 1, &end, 10);
    if (*end != '/') break;
    unsigned long won = strtoul(end + 1, &end, 10);
    out[n].id = (uint16_t)id;
    out[n].id_len = digits >= 4 ? 2 : 1;
    out[n].retried = (uint16_t)tried;
    out[n].won = (uint16_t)won;
    n++;
    p = end;
  }
  return n;
}

int linkRetryOneWay(const LinkTable& t, const LinkRetryHop* hops, int n, uint32_t now_ms, int* hop_index) {
  int best = -1, best_hop = -1;
  float worst = 0.5f;
  for (int h = 0; h < n; h++) {
    const LinkRetryHop& r = hops[h];
    if (r.retried < LINK_RETRY_MIN) continue;
    float rate = (float)r.won / r.retried;
    if (rate >= worst) continue;
    for (int i = 0; i < t.count; i++) {
      const LinkNeighbour& nb = t.nb[i];
      if (!judged(nb, now_ms)) continue;
      // A 1-byte hop matches a 2-byte neighbour by its first byte, and the other way round.
      uint8_t a = nb.id_len == 2 ? nb.id >> 8 : nb.id, b = r.id_len == 2 ? r.id >> 8 : r.id;
      bool same = nb.id_len == r.id_len ? nb.id == r.id : a == b;
      if (!same) continue;
      best = i;
      best_hop = h;
      worst = rate;
      break;
    }
  }
  if (hop_index) *hop_index = best_hop;
  return best;
}
