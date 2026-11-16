#include <helpers/diag_reports/DiagLinks.h>

#include "check.h"

#define NOW 3600000UL
#define SF  7

static DiagOut out;

static bool has(const char* text) { return strstr(out.buf, text) != nullptr; }

int main() {
  static LinkTable t;
  char text[64];

  // A 1-byte hash folds into the one 2-byte neighbour it starts, in either arrival order.
  linkInit(t);
  int a = linkIndex(t, 0xA6B2, 2, NOW);
  CHECK(linkIndex(t, 0xA6, 1, NOW) == a && t.count == 1);
  int c = linkIndex(t, 0xC5, 1, NOW);
  CHECK(linkIndex(t, 0xC5FB, 2, NOW) == c && t.count == 2 && t.nb[c].id == 0xC5FB && t.nb[c].id_len == 2);
  // Two 2-byte neighbours sharing a first byte make the short form ambiguous: it stays its own entry.
  linkIndex(t, 0xA601, 2, NOW);
  int amb = linkIndex(t, 0xA6, 1, NOW);
  CHECK(t.count == 4 && amb == 3 && t.nb[amb].id_len == 1);
  CHECK(linkIndex(t, 0xA6B2, 2, NOW) == a);

  // A full table replaces the stalest neighbour.
  linkInit(t);
  for (int i = 0; i < LINKS_MAX; i++) linkAddPacket(t, linkIndex(t, (uint16_t)(0x1000 + i * 0x101), 2, NOW), 5, SF, 1, NOW + i);
  int slot = linkIndex(t, 0x9999, 2, NOW + 100);
  CHECK(t.count == LINKS_MAX && slot == 0 && t.nb[0].id == 0x9999 && t.nb[0].packets == 0);

  // Hourly masks: heard everywhere but hours 8-16, where it is weak or silent.
  linkInit(t);
  int drop = linkIndex(t, 0xC5FB, 2, NOW), solid = linkIndex(t, 0x4EA1, 2, NOW);
  uint32_t loud = 0;
  for (uint8_t h = 0; h < 24; h++) {
    linkRollHour(t, h);
    bool noisy = h >= 8 && h < 17;
    if (noisy) loud |= 1UL << h;
    linkAddPacket(t, solid, 9.0f, SF, 1, NOW);
    if (!noisy) linkAddPacket(t, drop, 4.0f, SF, 1, NOW);
    else if (h % 3 == 0) linkAddPacket(t, drop, -6.0f, SF, 1, NOW);
  }
  CHECK(linkDropsIn(t, loud, 0, NOW) == drop);
  CHECK(linkDropsIn(t, 0, 0, NOW) == -1 && linkDropsIn(t, 1UL << 3, 0, NOW) == -1);
  // A mask of hours the link came through is not a drop.
  CHECK(linkDropsIn(t, 0x3UL, 0, NOW) == -1);
  // A half-day watch judges against the hours it saw: heard in hours 0-7, lost in 8-11, nothing known after.
  linkInit(t);
  drop = linkIndex(t, 0xC5FB, 2, NOW);
  solid = linkIndex(t, 0x4EA1, 2, NOW);
  for (uint8_t h = 0; h < 12; h++) {
    linkRollHour(t, h);
    linkAddPacket(t, solid, 9.0f, SF, 1, NOW);
    if (h < 8) linkAddPacket(t, drop, 4.0f, SF, 1, NOW);
  }
  CHECK(linkDropsIn(t, 0xF00UL, 0xFFFUL, NOW) == drop);
  CHECK(linkDropsIn(t, 0xF00UL, 0, NOW) == -1);
  LinkContext context = {loud, 0, "8am-5pm", true};
  outReset(out);
  linkFormat(t, SF, NOW, &context, nullptr, out);
  CHECK(has("Links: \U0001F7E1 C5FB drops in noise") && has("drops 8am-5pm") && has("Cause: noise 8am-5pm, not C5FB."));
  CHECK(has("~\n") && !out.truncated);
  // Rolling into an hour clears that hour's bits only.
  linkRollHour(t, 3);
  CHECK(!(t.nb[drop].heard & (1UL << 3)) && (t.nb[drop].heard & (1UL << 4)));
  // With the clock unset nothing is recorded by hour.
  linkInit(t);
  linkAddPacket(t, linkIndex(t, 0x4EA1, 2, NOW), 9.0f, SF, 1, NOW);
  CHECK(t.nb[0].heard == 0 && t.nb[0].packets == 1);

  // LNA-only: heard with the LNA on, never off, once the off state has real traffic.
  linkInit(t);
  int far = linkIndex(t, 0xA6B2, 2, NOW), near = linkIndex(t, 0x4EA1, 2, NOW);
  for (int i = 0; i < 12; i++) linkAddPacket(t, far, -4.0f, SF, 1, NOW);
  for (int i = 0; i < 12; i++) linkAddPacket(t, near, 9.0f, SF, 1, NOW);
  CHECK(linkLnaOnly(t, NOW) == -1);
  for (int i = 0; i < 25; i++) linkAddPacket(t, near, 9.0f, SF, 0, NOW);
  CHECK(linkLnaOnly(t, NOW) == far);
  context = {0, 0, nullptr, true};
  outReset(out);
  linkFormat(t, SF, NOW, &context, nullptr, out);
  CHECK(has("LNA-only"));
  // On a board with no switchable LNA the note never appears.
  context.lna_available = false;
  outReset(out);
  linkFormat(t, SF, NOW, &context, nullptr, out);
  CHECK(!has("LNA"));
  outReset(out);
  linkFormat(t, SF, NOW, nullptr, nullptr, out);
  CHECK(!has("LNA"));

  // Setting the clock re-bases stored advert offsets, so a stale skew does not linger.
  linkInit(t);
  for (int i = 0; i < 3; i++) {
    int index = linkIndex(t, (uint16_t)(0x2000 + i * 0x111), 2, NOW);
    for (int p = 0; p < 6; p++) linkAddPacket(t, index, 9.0f, SF, 1, NOW);
    linkAddClock(t, index, 22377600 + i);
  }
  CHECK(linkClockSummary(t, NOW, text, sizeof(text)) == DIAG_CAUTION);
  CHECK_STR(text, "ours 259 days slow");
  linkClockShift(t, 22377600);
  CHECK(linkClockSummary(t, NOW, text, sizeof(text)) == DIAG_GOOD);
  CHECK_STR(text, "in step");

  // Stale neighbours leave the verdict and the paths share.
  linkInit(t);
  int old = linkIndex(t, 0x1111, 2, 0);
  for (int p = 0; p < 30; p++) linkAddPacket(t, old, -6.0f, SF, 1, 0);
  int live = linkIndex(t, 0x2222, 2, LINKS_STALE_MS + 5);
  for (int p = 0; p < 30; p++) linkAddPacket(t, live, 9.0f, SF, 1, LINKS_STALE_MS + 5);
  CHECK(linkVerdict(t, SF, LINKS_STALE_MS + 10) == LINK_V_SOLID);
  CHECK(linkPathsSummary(t, LINKS_STALE_MS + 10, text, sizeof(text)) == DIAG_CAUTION);
  CHECK_STR(text, "all via 2222");
  // direct-retry's hop records: a judged neighbour whose resends mostly fail is named.
  LinkRetryHop hops[LINK_RETRY_HOPS] = {{0xA6B2, 2, 12, 2}, {0xC5, 1, 3, 0}, {0x4EA1, 2, 20, 19}};
  int hn = 3;
  static LinkTable rt;
  linkInit(rt);
  int ra = linkIndex(rt, 0xA6B2, 2, NOW), rc = linkIndex(rt, 0xC5FB, 2, NOW);
  for (int k = 0; k < 10; k++) {
    linkAddPacket(rt, ra, 5, 8, 2, NOW);
    linkAddPacket(rt, rc, 5, 8, 2, NOW);
  }
  int hop = -1;
  CHECK(linkRetryOneWay(rt, hops, hn, NOW, &hop) == ra && hop == 0);
  // Too few resends, or half or more confirmed, names no one; a 1-byte hop matches its 2-byte neighbour.
  LinkRetryHop few[2] = {{0xA6B2, 2, 4, 0}, {0xC5, 1, 6, 1}};
  CHECK(linkRetryOneWay(rt, few, 2, NOW, &hop) == rc && hop == 1);
  LinkRetryHop half[1] = {{0xA6B2, 2, 10, 5}};
  CHECK(linkRetryOneWay(rt, half, 1, NOW, &hop) == -1 && hop == -1);
  DONE();
}
