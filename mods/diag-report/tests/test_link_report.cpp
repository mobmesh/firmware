#include <helpers/diag_report/LinkReport.h>

#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

#define NOW 1000000UL
#define SF  7

static void feed(LinkTable& t, uint16_t id, int packets, float snr) {
  int i = linkIndex(t, id, 1, NOW);
  for (int k = 0; k < packets; k++) linkAddPacket(t, i, snr, NOW);
}

static bool any(char lines[][LINKS_LINE], int n, const char* text) {
  for (int i = 0; i < n; i++) if (strstr(lines[i], text)) return true;
  return false;
}

int main() {
  char lines[16][LINKS_LINE];
  LinkTable t;

  CHECK(linkSnrLimit(7) == -7.5f);
  CHECK(linkSnrLimit(12) == -20.0f);

  memset(&t, 0, sizeof(t));
  CHECK(linkVerdict(t, SF, NOW) == LINK_V_NOT_ENOUGH);
  feed(t, 0xC5, 3, 5);
  CHECK(linkVerdict(t, SF, NOW) == LINK_V_NOT_ENOUGH);
  int n = linkFormat(t, SF, NOW, lines, 16);
  CHECK(any(lines, n, "C5 3 pkts"));

  memset(&t, 0, sizeof(t));
  feed(t, 0xC5, 6, 9.5f);
  feed(t, 0x7A, 6, 2.0f);
  CHECK(linkVerdict(t, SF, NOW) == LINK_V_SOLID);
  n = linkFormat(t, SF, NOW, lines, 16);
  CHECK(any(lines, n, "All Solid"));
  CHECK(any(lines, n, "C5   SNR +9.5 margin 17"));
  CHECK(strstr(lines[2], "C5") != nullptr);    // strongest first

  feed(t, 0xE0, 6, -3.5f);
  CHECK(linkVerdict(t, SF, NOW) == LINK_V_CAN_DROP);
  feed(t, 0x3B, 6, -6.0f);
  CHECK(linkVerdict(t, SF, NOW) == LINK_V_CLOSE);
  n = linkFormat(t, SF, NOW, lines, 16);
  CHECK(any(lines, n, "1 Link Close to Dropping"));
  CHECK(any(lines, n, "3B drops first"));

  memset(&t, 0, sizeof(t));
  feed(t, 0x01, 6, -6);
  feed(t, 0x02, 6, -5);
  feed(t, 0x03, 6, 5);
  CHECK(linkVerdict(t, SF, NOW) == LINK_V_MOST_WEAK);

  memset(&t, 0, sizeof(t));
  feed(t, 0x7A, 12, 2);
  feed(t, 0xC5, 6, 9);
  for (int k = 0; k < 19; k++) linkAddEcho(t, 1);
  CHECK(linkVerdict(t, SF, NOW) == LINK_V_SOLID);
  linkAddEcho(t, 1);
  CHECK(linkVerdict(t, SF, NOW) == LINK_V_ONE_WAY);
  n = linkFormat(t, SF, NOW, lines, 16);
  CHECK(any(lines, n, "7A: we hear it well (margin 10)"));
  CHECK(any(lines, n, "hears us"));

  memset(&t, 0, sizeof(t));
  for (uint16_t id = 1; id <= 3; id++) {
    feed(t, id, 6, 5);
    linkAddClock(t, id - 1, -1684800000L);
  }
  CHECK(linkVerdict(t, SF, NOW) == LINK_V_CLOCK);
  n = linkFormat(t, SF, NOW, lines, 16);
  CHECK(any(lines, n, "3 of 3 neighbours' clocks behind"));
  CHECK(any(lines, n, "ours is fast"));
  CHECK(any(lines, n, "19500 days"));

  CHECK(linkVerdict(t, SF, NOW + LINKS_STALE_MS) == LINK_V_NOT_ENOUGH);

  memset(&t, 0, sizeof(t));
  for (uint16_t id = 0; id < LINKS_MAX; id++) {
    int i = linkIndex(t, id, 1, NOW + id);
    linkAddPacket(t, i, 0, NOW + id);
  }
  int slot = linkIndex(t, 0x99, 1, NOW + 100);
  CHECK(slot == 0 && t.nb[0].id == 0x99 && t.count == LINKS_MAX);

  for (int i = 0; i < 16; i++) CHECK(strlen(lines[i]) < LINKS_LINE);
  printf(failures ? "%d FAILED\n" : "OK\n", failures);
  return failures ? 1 : 0;
}
