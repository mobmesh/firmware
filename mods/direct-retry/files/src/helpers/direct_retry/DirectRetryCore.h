#pragma once

#include <stddef.h>
#include <stdint.h>

#define DR_KEY_LEN     8
#define DR_REPLY_KEY   4
#define DR_SLOTS       6
#define DR_RESENDS     2
#define DR_LAST_RESENDS 1
#define DR_QUEUE_MS    10000UL
#define DR_HOPS        8              // next hops with their own resend record
#define DR_NO_HOP      0xFF

struct DrSlot {
  bool active, queued;
  uint8_t key[DR_KEY_LEN];
  uint8_t pc, sent, limit;
  uint8_t hop;                         // index in the hop table, or DR_NO_HOP
  uint32_t due;
};

// One next hop's record: packets to it that needed a resend, and those then confirmed.
struct DrHop {
  uint8_t id[2], len;                  // its path hash, 1 or 2 bytes; len 0 = free
  uint16_t retried, won;
};

// The hop's index, taking a free entry or the one with the least history when the table is full.
int drHopIndex(DrHop* hops, const uint8_t* id, uint8_t len);

uint32_t drWait(uint32_t airtime, uint8_t sent, uint32_t jitter);
int drClaim(DrSlot* slots, const uint8_t* key);
// Both return the slots cleared; won_mask, when given, gains a bit per slot cleared after at least one resend.
int drClearEcho(DrSlot* slots, const uint8_t* key, uint8_t pc, uint8_t* won_mask = nullptr);
int drClearReply(DrSlot* slots, const uint8_t* key, uint8_t* won_mask = nullptr);
