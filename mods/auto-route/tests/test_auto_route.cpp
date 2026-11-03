#include <helpers/auto_route/AutoRouteCore.h>

#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)
#define NOW 100000U

static ArHash h(uint8_t a, uint8_t b = 0, uint8_t width = 2) {
  uint8_t bytes[3] = {a, b, 0};
  return arHash(bytes, width);
}

static bool chosen(const ArTables& t, const ArHash& failing, const ArHash& next, const ArHash& self, uint8_t* first) {
  ArHash out;
  if (!arChoose(t, failing, next, self, NOW + 10, &out)) return false;
  *first = out.b[0];
  return true;
}

int main() {
  static ArTables t;
  uint8_t via = 0;
  const ArHash self = h(0x10, 1), bad = h(0x20, 1), next = h(0x30, 1), a = h(0x40, 1), b = h(0x50, 1);

  CHECK(arMatch(h(1, 2), h(1, 2)));
  CHECK(!arMatch(h(1, 2), h(1, 3)));
  CHECK(arMatch(h(1, 2), h(1, 0, 1)));
  CHECK(!arMatch(h(1, 2), h(2, 0, 1)));

  // No evidence: no detour
  CHECK(!chosen(t, bad, next, self, &via));

  // One reverse sighting is not enough, and a neighbour must have been heard
  arSawHop(t, next, a, NOW);
  CHECK(!chosen(t, bad, next, self, &via));
  arSawHop(t, next, a, NOW);
  CHECK(!chosen(t, bad, next, self, &via));
  arHeard(t, a, 20, NOW);
  CHECK(chosen(t, bad, next, self, &via) && via == 0x40);

  // Best signal wins among armed candidates
  arSawHop(t, b, next, NOW);
  arHeard(t, b, 40, NOW);
  CHECK(chosen(t, bad, next, self, &via) && via == 0x50);

  // Never the failing hop, self or the target
  static ArTables u;
  arSawHop(u, bad, next, NOW);
  arHeard(u, bad, 40, NOW);
  CHECK(!chosen(u, bad, next, self, &via));
  memset(&u, 0, sizeof(u));
  arSawHop(u, self, next, NOW);
  arHeard(u, self, 40, NOW);
  CHECK(!chosen(u, bad, next, self, &via));

  // A one-byte via cannot fill a two-byte path entry
  memset(&u, 0, sizeof(u));
  arSawHop(u, h(0x60, 0, 1), h(0x30, 0, 1), NOW);
  arHeard(u, h(0x60, 0, 1), 40, NOW);
  CHECK(!chosen(u, bad, next, self, &via));

  // Three-byte hashes
  memset(&u, 0, sizeof(u));
  arSawHop(u, h(0x70, 7, 3), h(0x30, 1, 3), NOW);
  arHeard(u, h(0x70, 7, 3), 12, NOW);
  CHECK(chosen(u, h(0x20, 1, 3), h(0x30, 1, 3), h(0x10, 1, 3), &via) && via == 0x70);

  // A last hop names its destination in one byte, which still finds a wider entry
  CHECK(chosen(t, bad, h(0x30, 0, 1), self, &via));

  // Old evidence expires
  ArHash out;
  CHECK(!arChoose(t, bad, next, self, NOW + AR_FRESH_MS + 2, &out));

  // A full table evicts its oldest entry
  memset(&u, 0, sizeof(u));
  for (int i = 0; i < AR_LINKS + 4; i++) arSawOrigin(u, h((uint8_t)(i + 1)), h(0xEE), NOW + i * 10);
  bool first_left = false;
  for (int i = 0; i < AR_LINKS; i++) first_left = first_left || (u.links[i].at != 0 && u.links[i].via.b[0] == 1);
  CHECK(!first_left);

  CHECK(arWait(100, 0, 0) == 500);
  CHECK(arWait(100, 2, 50) == 750);

  static ArSlot slots[AR_SLOTS];
  uint8_t key[AR_KEY_LEN] = {1, 2, 3, 4, 5, 6, 7, 8}, other[AR_KEY_LEN] = {9, 2, 3, 4, 5, 6, 7, 8};
  CHECK(arClaim(slots, key) == 0);
  memcpy(slots[0].key, key, AR_KEY_LEN);
  slots[0].active = true;
  slots[0].pc = 2;
  CHECK(arClaim(slots, key) == -1);
  CHECK(arClaim(slots, other) == 1);

  // An echo must show a shorter path, and the same packet
  CHECK(arClearEcho(slots, key, 2) == 0);
  CHECK(arClearEcho(slots, other, 1) == 0);
  CHECK(arClearEcho(slots, key, 1) == 1 && !slots[0].active);

  slots[0].active = true;
  CHECK(arClearReply(slots, other) == 0);
  CHECK(arClearReply(slots, key) == 1 && !slots[0].active);

  for (int i = 0; i < AR_SLOTS; i++) slots[i].active = true, slots[i].key[0] = (uint8_t)(100 + i);
  CHECK(arClaim(slots, key) == -1);

  printf(failures ? "%d FAILED\n" : "OK\n", failures);
  return failures ? 1 : 0;
}
