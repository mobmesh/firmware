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

  for (int i = 0; i < DR_SLOTS; i++) slots[i].active = true, slots[i].key[0] = (uint8_t)(100 + i);
  CHECK(drClaim(slots, key) == -1);

  printf(failures ? "%d FAILED\n" : "OK\n", failures);
  return failures ? 1 : 0;
}
