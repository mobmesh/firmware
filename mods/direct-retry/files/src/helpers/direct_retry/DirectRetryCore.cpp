#include <helpers/direct_retry/DirectRetryCore.h>

#include <string.h>

uint32_t drWait(uint32_t airtime, uint8_t sent, uint32_t jitter) {
  return airtime * 3 + 200 + (uint32_t)sent * 100 + jitter;
}

int drClaim(DrSlot* slots, const uint8_t* key) {
  int open = -1;
  for (int i = 0; i < DR_SLOTS; i++) {
    if (slots[i].active && memcmp(slots[i].key, key, DR_KEY_LEN) == 0) return -1;
    if (!slots[i].active && open < 0) open = i;
  }
  return open;
}

int drClearEcho(DrSlot* slots, const uint8_t* key, uint8_t pc, uint8_t* won_mask) {
  int cleared = 0;
  for (int i = 0; i < DR_SLOTS; i++) {
    if (slots[i].active && pc < slots[i].pc && memcmp(slots[i].key, key, DR_KEY_LEN) == 0) {
      slots[i].active = false;
      cleared++;
      if (won_mask && slots[i].sent > 0) *won_mask |= (uint8_t)(1 << i);
    }
  }
  return cleared;
}

int drClearReply(DrSlot* slots, const uint8_t* key, uint8_t* won_mask) {
  int cleared = 0;
  for (int i = 0; i < DR_SLOTS; i++) {
    if (slots[i].active && memcmp(slots[i].key, key, DR_REPLY_KEY) == 0) {
      slots[i].active = false;
      cleared++;
      if (won_mask && slots[i].sent > 0) *won_mask |= (uint8_t)(1 << i);
    }
  }
  return cleared;
}

int drHopIndex(DrHop* hops, const uint8_t* id, uint8_t len) {
  int open = -1, least = 0;
  for (int i = 0; i < DR_HOPS; i++) {
    if (hops[i].len == len && memcmp(hops[i].id, id, len) == 0) return i;
    if (hops[i].len == 0 && open < 0) open = i;
    if (hops[i].retried < hops[least].retried) least = i;
  }
  int i = open >= 0 ? open : least;
  memset(&hops[i], 0, sizeof(DrHop));
  memcpy(hops[i].id, id, len);
  hops[i].len = len;
  return i;
}
