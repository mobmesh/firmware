#include <helpers/radio_diag/LinkReport.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SOLID_DB        6.0f
#define CLOSE_DB        3.0f
#define SNR_SLOW_AFTER  8
#define ONE_WAY_PKTS    10
#define ONE_WAY_ECHOES  20
#define CLOCK_MIN_NB    3
#define CLOCK_OFF_S     120
#define TABLE_ROWS      8
#define PATHS_MIN_PKTS  20
#define PATHS_ONE_PCT   70

float linkSnrLimit(uint8_t sf) {
  return -2.5f * ((int)sf - 4);
}

int linkIndex(LinkTable& t, uint16_t id, uint8_t id_len, uint32_t now_ms) {
  for (int i = 0; i < t.count; i++) {
    if (t.nb[i].id == id && t.nb[i].id_len == id_len) return i;
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

void linkAddPacket(LinkTable& t, int index, float snr_db, uint32_t now_ms) {
  LinkNeighbour& n = t.nb[index];
  if (n.packets < 0xFFFF) n.packets++;
  int weight = n.packets < SNR_SLOW_AFTER ? n.packets : SNR_SLOW_AFTER;
  n.snr_db += (snr_db - n.snr_db) / weight;
  n.last_ms = now_ms;
}

void linkAddEcho(LinkTable& t, int index) {
  if (t.nb[index].echoes < 0xFFFF) t.nb[index].echoes++;
  t.echoes++;
}

void linkAddClock(LinkTable& t, int index, int32_t offset_s) {
  t.nb[index].clock_off_s = offset_s;
  t.nb[index].clock_n = 1;
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

static int oneWay(const LinkTable& t, uint8_t sf, uint32_t now_ms) {
  if (t.echoes < ONE_WAY_ECHOES) return -1;
  for (int i = 0; i < t.count; i++) {
    const LinkNeighbour& n = t.nb[i];
    if (judged(n, now_ms) && n.packets >= ONE_WAY_PKTS && n.echoes == 0 && margin(n, sf) >= SOLID_DB) return i;
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

LinkVerdict linkVerdict(const LinkTable& t, uint8_t sf, uint32_t now_ms) {
  int judged_n = 0, weak = 0, can_drop = 0;
  for (int i = 0; i < t.count; i++) {
    if (!judged(t.nb[i], now_ms)) continue;
    judged_n++;
    float m = margin(t.nb[i], sf);
    if (m < CLOSE_DB) weak++;
    if (m < SOLID_DB) can_drop++;
  }
  if (judged_n == 0) return LINK_V_NOT_ENOUGH;
  if (judged_n >= 3 && weak * 3 >= judged_n * 2) return LINK_V_MOST_WEAK;
  if (oneWay(t, sf, now_ms) >= 0) return LINK_V_ONE_WAY;
  if (weak > 0) return LINK_V_CLOSE;
  if (can_drop > 0) return LINK_V_CAN_DROP;
  int agree, total;
  int32_t typical;
  if (clockSkew(t, now_ms, &agree, &total, &typical) != 0) return LINK_V_CLOCK;
  return LINK_V_SOLID;
}

static void label(const LinkNeighbour& n, char* out, size_t len) {
  if (n.id_len == 2) snprintf(out, len, "%04X", n.id);
  else snprintf(out, len, "%02X", n.id);
}

bool linkWeakest(const LinkTable& t, uint8_t sf, uint32_t now_ms, float* margin_db, char* id, size_t len) {
  int worst = -1;
  for (int i = 0; i < t.count; i++) {
    if (judged(t.nb[i], now_ms) && (worst < 0 || margin(t.nb[i], sf) < margin(t.nb[worst], sf))) worst = i;
  }
  if (worst < 0) return false;
  *margin_db = margin(t.nb[worst], sf);
  label(t.nb[worst], id, len);
  return true;
}

static void spanText(int32_t s, char* out, size_t len) {
  if (s < 7200) snprintf(out, len, "%ld min", (long)(s / 60));
  else if (s < 172800) snprintf(out, len, "%ld h", (long)(s / 3600));
  else snprintf(out, len, "%ld days", (long)(s / 86400));
}

#define ADD(...) do { if (n < max_lines) snprintf(lines[n++], LINKS_LINE, __VA_ARGS__); } while (0)

// Judged neighbours, strongest first, one line each.
static int addTable(const LinkTable& t, uint8_t sf, uint32_t now_ms, char lines[][LINKS_LINE], int n, int max_lines) {
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
  for (int r = 0; r < k && r < TABLE_ROWS; r++) {
    const LinkNeighbour& nb = t.nb[order[r]];
    char id[8];
    label(nb, id, sizeof(id));
    float m = margin(nb, sf);
    const char* mark = m >= SOLID_DB ? "✅" : m >= CLOSE_DB ? "\U0001F7E1" : "\U0001F534";
    ADD("%-4s SNR %+.1f margin %d %s%s", id, nb.snr_db, (int)lroundf(m), mark, nb.echoes ? " hears us" : "");
  }
  return n;
}

int linkFormat(const LinkTable& t, uint8_t sf, uint32_t now_ms, char lines[][LINKS_LINE], int max_lines) {
  int n = 0;
  int judged_n = 0, fresh_n = 0, weak = 0, can_drop = 0, worst = -1;
  for (int i = 0; i < t.count; i++) {
    if (fresh(t.nb[i], now_ms)) fresh_n++;
    if (!judged(t.nb[i], now_ms)) continue;
    judged_n++;
    float m = margin(t.nb[i], sf);
    if (m < CLOSE_DB) weak++;
    if (m < SOLID_DB) can_drop++;
    if (worst < 0 || m < margin(t.nb[worst], sf)) worst = i;
  }
  char id[8];
  switch (linkVerdict(t, sf, now_ms)) {
    case LINK_V_NOT_ENOUGH: {
      ADD("\U0001F517 Links: ⚪ not enough data");
      ADD("%d neighbours heard, need %d packets each.", fresh_n, LINKS_MIN_PKTS);
      char row[LINKS_LINE] = "";
      for (int i = 0; i < t.count; i++) {
        if (!fresh(t.nb[i], now_ms)) continue;
        char part[24];
        label(t.nb[i], id, sizeof(id));
        snprintf(part, sizeof(part), "%s%s %u pkts", row[0] ? ", " : "", id, t.nb[i].packets);
        if (strlen(row) + strlen(part) >= LINKS_LINE) {
          ADD("%s", row);
          snprintf(row, sizeof(row), "%s %u pkts", id, t.nb[i].packets);
        } else {
          strcat(row, part);
        }
      }
      if (row[0]) ADD("%s", row);
      ADD("Check again later.");
      return n;
    }
    case LINK_V_MOST_WEAK:
      ADD("\U0001F517 Links: \U0001F534 most links weak");
      ADD("%d neighbours, %d under 3 dB margin.", judged_n, weak);
      ADD("Weak on all sides at once points here,");
      ADD("not at them.");
      n = addTable(t, sf, now_ms, lines, n, max_lines);
      ADD("Try: check antenna, cable and connector.");
      return n;
    case LINK_V_ONE_WAY: {
      const LinkNeighbour& nb = t.nb[oneWay(t, sf, now_ms)];
      label(nb, id, sizeof(id));
      ADD("\U0001F517 Links: \U0001F7E1 one-way link");
      ADD("%s: we hear it well (margin %d), but", id, (int)lroundf(margin(nb, sf)));
      ADD("of %lu echoes of our relays, none via it.", (unsigned long)t.echoes);
      ADD("It may not hear us.");
      n = addTable(t, sf, now_ms, lines, n, max_lines);
      ADD("LNA can't fix that: check our TX power,");
      ADD("antenna and cable.");
      return n;
    }
    case LINK_V_CLOSE:
    case LINK_V_CAN_DROP:
      label(t.nb[worst], id, sizeof(id));
      if (weak) ADD("\U0001F517 Links: \U0001F7E1 %d link%s close to dropping", weak, weak == 1 ? "" : "s");
      else ADD("\U0001F517 Links: \U0001F7E1 %d link%s can drop", can_drop, can_drop == 1 ? "" : "s");
      n = addTable(t, sf, now_ms, lines, n, max_lines);
      ADD("%s drops first in rain or foliage.", id);
      ADD("Higher antenna or a relay helps most.");
      return n;
    case LINK_V_CLOCK: {
      int agree, total;
      int32_t typical;
      int sign = clockSkew(t, now_ms, &agree, &total, &typical);
      char span[16];
      spanText(typical, span, sizeof(span));
      ADD("\U0001F517 Links: \U0001F7E1 our clock off");
      ADD("%d of %d neighbours' clocks %s of us", agree, total, sign > 0 ? "ahead" : "behind");
      ADD("by about %s: ours is %s.", span, sign > 0 ? "slow" : "fast");
      n = addTable(t, sf, now_ms, lines, n, max_lines);
      ADD("Try: clock sync from the app.");
      return n;
    }
    case LINK_V_SOLID:
      ADD("\U0001F517 Links: \U0001F7E2 all solid");
      ADD("%d neighbour%s, all >= 6 dB margin.", judged_n, judged_n == 1 ? "" : "s");
      n = addTable(t, sf, now_ms, lines, n, max_lines);
      ADD("Nothing to fix.");
      return n;
  }
  return n;
}

int linkSummary(const LinkTable& t, uint8_t sf, uint32_t now_ms, char* out, size_t len) {
  int judged_n = 0, fresh_n = 0, weak = 0, can_drop = 0, hearing = 0, worst = -1;
  for (int i = 0; i < t.count; i++) {
    if (fresh(t.nb[i], now_ms)) fresh_n++;
    if (!judged(t.nb[i], now_ms)) continue;
    judged_n++;
    float m = margin(t.nb[i], sf);
    if (m < CLOSE_DB) weak++;
    if (m < SOLID_DB) can_drop++;
    if (t.nb[i].echoes) hearing++;
    if (worst < 0 || m < margin(t.nb[worst], sf)) worst = i;
  }
  char id[8] = "";
  if (worst >= 0) label(t.nb[worst], id, sizeof(id));
  switch (linkVerdict(t, sf, now_ms)) {
    case LINK_V_NOT_ENOUGH:
      snprintf(out, len, "%d heard, need %d pkts each", fresh_n, LINKS_MIN_PKTS);
      return 3;
    case LINK_V_MOST_WEAK:
      snprintf(out, len, "%d of %d weak", weak, judged_n);
      return 2;
    case LINK_V_ONE_WAY:
      label(t.nb[oneWay(t, sf, now_ms)], id, sizeof(id));
      snprintf(out, len, "one-way (%s)", id);
      return 1;
    case LINK_V_CLOSE:
      snprintf(out, len, "%d close to dropping (%s)", weak, id);
      return 1;
    case LINK_V_CAN_DROP:
      snprintf(out, len, "%d can drop (%s)", can_drop, id);
      return 1;
    default:
      snprintf(out, len, "%d solid%s", judged_n, hearing == judged_n ? ", all hear us" : "");
      return 0;
  }
}

int linkPathsSummary(const LinkTable& t, uint32_t now_ms, char* out, size_t len) {
  uint32_t total = 0;
  int top = -1, n = 0;
  for (int i = 0; i < t.count; i++) {
    if (!fresh(t.nb[i], now_ms)) continue;
    total += t.nb[i].packets;
    n++;
    if (top < 0 || t.nb[i].packets > t.nb[top].packets) top = i;
  }
  if (total < PATHS_MIN_PKTS) {
    snprintf(out, len, "%lu of %d pkts", (unsigned long)total, PATHS_MIN_PKTS);
    return 3;
  }
  char id[8];
  label(t.nb[top], id, sizeof(id));
  if (n == 1) {
    snprintf(out, len, "all via %s", id);
    return 1;
  }
  int pct = (int)(t.nb[top].packets * 100UL / total);
  if (pct >= PATHS_ONE_PCT) {
    snprintf(out, len, "%d%% via %s", pct, id);
    return 1;
  }
  snprintf(out, len, "spread over %d neighbours", n);
  return 0;
}

int linkClockSummary(const LinkTable& t, uint32_t now_ms, char* out, size_t len) {
  int agree, total;
  int32_t typical;
  int sign = clockSkew(t, now_ms, &agree, &total, &typical);
  if (total < CLOCK_MIN_NB) {
    snprintf(out, len, "%d of %d adverts", total, CLOCK_MIN_NB);
    return 3;
  }
  if (sign == 0) {
    snprintf(out, len, "agrees with neighbours");
    return 0;
  }
  char span[16];
  spanText(typical, span, sizeof(span));
  snprintf(out, len, "ours %s %s", span, sign > 0 ? "slow" : "fast");
  return 1;
}
