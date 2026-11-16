#include <helpers/direct_retry/DirectRetryCore.h>

#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

int main() {
  CHECK(drWait(100, 0, 0) == 500);
  CHECK(drWait(100, 2, 50) == 750);

  static DrSlot slots[DR_SLOTS];
  uint8_t key[DR_KEY_LEN] = {1, 2, 3, 4, 5, 6, 7, 8}, other[DR_KEY_LEN] = {9, 2, 3, 4, 5, 6, 7, 8};
  CHECK(drClaim(slots, key) == 0);
  memcpy(slots[0].key, key, DR_KEY_LEN);
  slots[0].active = true;
  slots[0].pc = 2;
  CHECK(drClaim(slots, key) == -1);
  CHECK(drClaim(slots, other) == 1);

  // An echo must show a shorter path, and the same packet
  CHECK(drClearEcho(slots, key, 2) == 0);
  CHECK(drClearEcho(slots, other, 1) == 0);
  CHECK(drClearEcho(slots, key, 1) == 1 && !slots[0].active);

  slots[0].active = true;
  CHECK(drClearReply(slots, other) == 0);
  CHECK(drClearReply(slots, key) == 1 && !slots[0].active);
  // A confirmation after a resend counts as won; one on the first send does not.
  uint8_t won = 0;
  slots[0].active = true;
  slots[0].sent = 0;
  CHECK(drClearReply(slots, key, &won) == 1 && won == 0);
  slots[0].active = true;
  slots[0].sent = 1;
  CHECK(drClearEcho(slots, key, 1, &won) == 1 && won == 1);

  // Next hops keep their own record; a full table gives up the hop with the least history.
  static DrHop hops[DR_HOPS];
  uint8_t a[2] = {0xA6, 0xB2}, b[2] = {0xC5, 0xFB};
  int ia = drHopIndex(hops, a, 2);
  CHECK(drHopIndex(hops, a, 2) == ia && drHopIndex(hops, b, 2) != ia && drHopIndex(hops, a, 1) != ia);
  for (int i = 0; i < DR_HOPS; i++) hops[i].retried = (uint16_t)(10 + i);
  hops[3].retried = 1;
  uint8_t c[2] = {0x11, 0x22};
  CHECK(drHopIndex(hops, c, 2) == 3 && hops[3].retried == 0 && hops[3].id[0] == 0x11);

  for (int i = 0; i < DR_SLOTS; i++) slots[i].active = true, slots[i].key[0] = (uint8_t)(100 + i);
  CHECK(drClaim(slots, key) == -1);

  printf(failures ? "%d FAILED\n" : "OK\n", failures);
  return failures ? 1 : 0;
}
