#pragma once

#include <stddef.h>
#include <stdint.h>

#define DR_KEY_LEN     8
#define DR_REPLY_KEY   4
#define DR_SLOTS       6
#define DR_RESENDS     2
#define DR_LAST_RESENDS 1
#define DR_QUEUE_MS    10000UL

struct DrSlot {
  bool active, queued;
  uint8_t key[DR_KEY_LEN];
  uint8_t pc, sent, limit;
  uint32_t due;
};

uint32_t drWait(uint32_t airtime, uint8_t sent, uint32_t jitter);
int drClaim(DrSlot* slots, const uint8_t* key);
int drClearEcho(DrSlot* slots, const uint8_t* key, uint8_t pc);
int drClearReply(DrSlot* slots, const uint8_t* key);
