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

int drClearEcho(DrSlot* slots, const uint8_t* key, uint8_t pc) {
  int cleared = 0;
  for (int i = 0; i < DR_SLOTS; i++) {
    if (slots[i].active && pc < slots[i].pc && memcmp(slots[i].key, key, DR_KEY_LEN) == 0) {
      slots[i].active = false;
      cleared++;
    }
  }
  return cleared;
}

int drClearReply(DrSlot* slots, const uint8_t* key) {
  int cleared = 0;
  for (int i = 0; i < DR_SLOTS; i++) {
    if (slots[i].active && memcmp(slots[i].key, key, DR_REPLY_KEY) == 0) {
      slots[i].active = false;
      cleared++;
    }
  }
  return cleared;
}
