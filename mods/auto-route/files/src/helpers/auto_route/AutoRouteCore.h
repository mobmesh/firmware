#pragma once

#include <stddef.h>
#include <stdint.h>

#define AR_HASH_MAX    3
#define AR_KEY_LEN     8
#define AR_REPLY_KEY   4
#define AR_SLOTS       6
#define AR_LINKS       32
#define AR_NEIGHBOURS  20
#define AR_RESENDS     2
#define AR_LAST_RESENDS 1
#define AR_ARM_FORWARD 1
#define AR_ARM_REVERSE 2
#define AR_QUEUE_MS    10000UL
#define AR_FRESH_MS    (72UL * 3600UL * 1000UL)

struct ArHash {
  uint8_t len;
  uint8_t b[AR_HASH_MAX];
};

struct ArLink {
  ArHash via, hop;
  uint16_t forward, reverse;
  uint32_t at;
};

struct ArNeighbour {
  ArHash id;
  int8_t snr;
  uint32_t at;
};

struct ArSlot {
  bool active, detoured, queued;
  uint8_t key[AR_KEY_LEN];
  uint8_t pc, sent, limit;
  uint32_t due;
};

struct ArTables {
  ArLink links[AR_LINKS];
  ArNeighbour neighbours[AR_NEIGHBOURS];
};

ArHash arHash(const uint8_t* bytes, uint8_t len);
bool arMatch(const ArHash& a, const ArHash& b);
void arHeard(ArTables& t, const ArHash& id, int8_t snr, uint32_t now);
void arSawHop(ArTables& t, const ArHash& before, const ArHash& after, uint32_t now);
void arSawOrigin(ArTables& t, const ArHash& via, const ArHash& origin, uint32_t now);
bool arArmed(const ArLink& link);
bool arChoose(const ArTables& t, const ArHash& failing, const ArHash& next, const ArHash& self, uint32_t now, ArHash* out);
uint32_t arWait(uint32_t airtime, uint8_t sent, uint32_t jitter);
int arClaim(ArSlot* slots, const uint8_t* key);
int arClearEcho(ArSlot* slots, const uint8_t* key, uint8_t pc);
int arClearReply(ArSlot* slots, const uint8_t* key);
