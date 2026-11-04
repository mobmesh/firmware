#include <helpers/auto_route/AutoRouteCore.h>

#include <string.h>

ArHash arHash(const uint8_t* bytes, uint8_t len) {
  ArHash h = {};
  h.len = len > AR_HASH_MAX ? AR_HASH_MAX : len;
  memcpy(h.b, bytes, h.len);
  return h;
}

// Widths can differ across a mesh, and a destination hash is always one byte, so the shorter prefix decides.
bool arMatch(const ArHash& a, const ArHash& b) {
  uint8_t n = a.len < b.len ? a.len : b.len;
  return n > 0 && memcmp(a.b, b.b, n) == 0;
}

template <typename T, typename Same>
static T& slotFor(T* items, int count, uint32_t now, Same same) {
  int found = -1, oldest = 0;
  for (int i = 0; i < count; i++) {
    if (same(items[i])) return items[i];
    if (found < 0 && items[i].at == 0) found = i;
    if ((int32_t)(items[oldest].at - items[i].at) > 0) oldest = i;
  }
  T& item = items[found >= 0 ? found : oldest];
  memset(&item, 0, sizeof(item));
  (void)now;
  return item;
}

void arHeard(ArTables& t, const ArHash& id, int8_t snr, uint32_t now) {
  ArNeighbour& n = slotFor(t.neighbours, AR_NEIGHBOURS, now, [&](const ArNeighbour& x) {
    return x.at != 0 && x.id.len == id.len && memcmp(x.id.b, id.b, id.len) == 0;
  });
  n.id = id;
  n.snr = snr;
  n.at = now | 1;
}

static ArLink& linkFor(ArTables& t, const ArHash& via, const ArHash& hop, uint32_t now) {
  ArLink& l = slotFor(t.links, AR_LINKS, now, [&](const ArLink& x) {
    return x.at != 0 && x.via.len == via.len && x.hop.len == hop.len && memcmp(x.via.b, via.b, via.len) == 0 && memcmp(x.hop.b, hop.b, hop.len) == 0;
  });
  l.via = via;
  l.hop = hop;
  l.at = now | 1;
  return l;
}

static const ArNeighbour* neighbour(const ArTables& t, const ArHash& id, uint32_t now) {
  for (int i = 0; i < AR_NEIGHBOURS; i++) {
    const ArNeighbour& n = t.neighbours[i];
    if (n.at != 0 && (int32_t)(now - n.at) <= (int32_t)AR_FRESH_MS && arMatch(n.id, id)) return &n;
  }
  return nullptr;
}

// Only a neighbour heard directly can be a detour, so only its own sides of a path are kept.
void arSawHop(ArTables& t, const ArHash& before, const ArHash& after, uint32_t now) {
  if (neighbour(t, before, now)) linkFor(t, before, after, now).forward++;
  if (neighbour(t, after, now)) linkFor(t, after, before, now).reverse++;
}

void arSawOrigin(ArTables& t, const ArHash& via, const ArHash& origin, uint32_t now) {
  if (neighbour(t, via, now)) linkFor(t, via, origin, now).reverse++;
}

bool arArmed(const ArLink& link) {
  return link.forward >= AR_ARM_FORWARD || link.reverse >= AR_ARM_REVERSE;
}

bool arChoose(const ArTables& t, const ArHash& failing, const ArHash& next, const ArHash& self, uint32_t now, ArHash* out) {
  int best = -1;
  int8_t best_snr = 0;
  for (int i = 0; i < AR_LINKS; i++) {
    const ArLink& l = t.links[i];
    if (l.at == 0 || (int32_t)(now - l.at) > (int32_t)AR_FRESH_MS || !arArmed(l) || !arMatch(l.hop, next)) continue;
    if (l.via.len < failing.len || arMatch(l.via, failing) || arMatch(l.via, self) || arMatch(l.via, next)) continue;
    const ArNeighbour* n = neighbour(t, l.via, now);
    if (n == nullptr) continue;
    if (best < 0 || n->snr > best_snr) {
      best = i;
      best_snr = n->snr;
    }
  }
  if (best < 0) return false;
  *out = t.links[best].via;
  return true;
}

uint32_t arWait(uint32_t airtime, uint8_t sent, uint32_t jitter) {
  return airtime * 3 + 200 + (uint32_t)sent * 100 + jitter;
}

int arClaim(ArSlot* slots, const uint8_t* key) {
  int open = -1;
  for (int i = 0; i < AR_SLOTS; i++) {
    if (slots[i].active && memcmp(slots[i].key, key, AR_KEY_LEN) == 0) return -1;
    if (!slots[i].active && open < 0) open = i;
  }
  return open;
}

int arClearEcho(ArSlot* slots, const uint8_t* key, uint8_t pc) {
  int cleared = 0;
  for (int i = 0; i < AR_SLOTS; i++) {
    if (slots[i].active && pc < slots[i].pc && memcmp(slots[i].key, key, AR_KEY_LEN) == 0) {
      slots[i].active = false;
      cleared++;
    }
  }
  return cleared;
}

int arClearReply(ArSlot* slots, const uint8_t* key) {
  int cleared = 0;
  for (int i = 0; i < AR_SLOTS; i++) {
    if (slots[i].active && memcmp(slots[i].key, key, AR_REPLY_KEY) == 0) {
      slots[i].active = false;
      cleared++;
    }
  }
  return cleared;
}
