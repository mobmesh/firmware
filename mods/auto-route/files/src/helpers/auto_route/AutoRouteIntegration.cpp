#include <helpers/auto_route/AutoRouteIntegration.h>

#include <Arduino.h>
#include <Packet.h>
#include <SPIFFS.h>
#include <Utils.h>
#include <helpers/ConfigSerializer.h>
#include <helpers/auto_route/AutoRouteCore.h>

#define SETTINGS_PATH "/autoroute.json"
#define HOP_REPLY     0xA0
#define HOP_REPLY_LEN (1 + AR_REPLY_KEY)
#define REPLY_MAX     160

class SettingsFile : public ConfigSerializer {
  uint8_t& _on;
  uint8_t& _detour;
public:
  SettingsFile(uint8_t& on, uint8_t& detour) : _on(on), _detour(detour) { }
protected:
  void structure() override {
    def("on", _on);
    def("detour", _detour);
  }
};

static uint8_t enabled, detour_on = 1;
static bool booted;
static ArTables tables;
static ArSlot slots[AR_SLOTS];
static mesh::Packet copies[AR_SLOTS];
static uint8_t active;
static ArHash self_hash[AR_HASH_MAX + 1];
static uint32_t count_resent, count_echo, count_reply, count_exhausted, count_detour, count_noalt, count_replied, count_noslot, count_stale;

static void boot() {
  booted = true;
  uint8_t key[32];
  if (modPublisherKey(key)) {
    for (uint8_t w = 1; w <= AR_HASH_MAX; w++) self_hash[w] = arHash(key, w);
  }
  if (!SPIFFS.begin(true) || !SPIFFS.exists(SETTINGS_PATH)) return;
  File f = SPIFFS.open(SETTINGS_PATH, "r");
  if (!f) return;
  SettingsFile file(enabled, detour_on);
  file.loadSerial(f);
  f.close();
}

static bool saveSettings() {
  File f = SPIFFS.open(SETTINGS_PATH, "w", true);
  if (!f) return false;
  SettingsFile file(enabled, detour_on);
  bool ok = file.saveSerial(f);
  f.close();
  return ok;
}

static void recount() {
  active = 0;
  for (int i = 0; i < AR_SLOTS; i++) active += slots[i].active;
}

static void keyOf(const mesh::Packet* packet, uint8_t* key) {
  if (packet->getPayloadType() == PAYLOAD_TYPE_MULTIPART && packet->payload_len >= 2 && (packet->payload[0] & 0x0F) == PAYLOAD_TYPE_ACK) {
    uint8_t type = PAYLOAD_TYPE_ACK;
    mesh::Utils::sha256(key, AR_KEY_LEN, &type, 1, &packet->payload[1], packet->payload_len - 1);
  } else {
    uint8_t full[MAX_HASH_SIZE];
    packet->calculatePacketHash(full);
    memcpy(key, full, AR_KEY_LEN);
  }
}

static bool isAck(const mesh::Packet* packet) {
  uint8_t type = packet->getPayloadType();
  return type == PAYLOAD_TYPE_ACK || type == PAYLOAD_TYPE_MULTIPART;
}

static void track(const mesh::Packet* packet, uint32_t delay_millis, bool ack_copy) {
  uint8_t type = packet->getPayloadType();
  if (!packet->isRouteDirect() || type == PAYLOAD_TYPE_TRACE || type == PAYLOAD_TYPE_CONTROL) return;
  uint8_t pc = packet->getPathHashCount();
  if (pc == 0 && (isAck(packet) || ack_copy)) return;
  mesh::Packet copy = *packet;
  if (ack_copy) copy.header = (copy.header & ~(PH_TYPE_MASK << PH_TYPE_SHIFT)) | (PAYLOAD_TYPE_ACK << PH_TYPE_SHIFT);
  uint8_t key[AR_KEY_LEN];
  keyOf(&copy, key);
  int slot = arClaim(slots, key);
  if (slot < 0) {
    if (active == AR_SLOTS) count_noslot++;
    return;
  }
  ArSlot& s = slots[slot];
  memcpy(s.key, key, AR_KEY_LEN);
  s.active = true;
  s.detoured = false;
  s.queued = true;
  s.pc = pc;
  s.sent = 0;
  s.limit = pc == 0 ? AR_LAST_RESENDS : AR_RESENDS;
  s.due = millis() + delay_millis + AR_QUEUE_MS;
  copies[slot] = copy;
  recount();
}

static void sent(const mesh::Packet* packet) {
  if (active == 0 || !packet->isRouteDirect() || packet->getPayloadType() == PAYLOAD_TYPE_CONTROL) return;
  uint8_t key[AR_KEY_LEN];
  keyOf(packet, key);
  for (int i = 0; i < AR_SLOTS; i++) {
    ArSlot& s = slots[i];
    if (!s.active || memcmp(s.key, key, AR_KEY_LEN) != 0) continue;
    s.queued = false;
    s.due = millis() + arWait(modAirtimeFor(copies[i].getRawLength()), s.sent, random(0, 200));
  }
}

static void hopReply(const mesh::Packet* packet) {
  uint8_t key[AR_KEY_LEN], data[HOP_REPLY_LEN];
  keyOf(packet, key);
  data[0] = HOP_REPLY;
  memcpy(&data[1], key, AR_REPLY_KEY);
  if (modDirectReply(data, sizeof(data), random(40, 160))) count_replied++;
}

static void learn(const mesh::Packet* packet) {
  uint8_t width = packet->getPathHashSize(), pc = packet->getPathHashCount();
  if (width == 0 || width > AR_HASH_MAX) return;
  const ArHash& self = self_hash[width];
  uint32_t now = millis();
  bool advert = packet->getPayloadType() == PAYLOAD_TYPE_ADVERT && packet->payload_len >= 32;
  ArHash origin = arHash(packet->payload, width);
  if (pc > 0) {
    ArHash last = arHash(&packet->path[(pc - 1) * width], width);
    if (!arMatch(last, self)) arHeard(tables, last, packet->_snr, now);
  } else if (advert && !arMatch(origin, self)) {
    arHeard(tables, origin, packet->_snr, now);
  }
  for (uint8_t i = 1; i < pc; i++) {
    ArHash before = arHash(&packet->path[(i - 1) * width], width), after = arHash(&packet->path[i * width], width);
    if (!arMatch(before, after) && !arMatch(before, self) && !arMatch(after, self)) arSawHop(tables, before, after, now);
  }
  if (advert && pc > 0) {
    ArHash first = arHash(packet->path, width);
    if (!arMatch(first, origin) && !arMatch(first, self) && !arMatch(origin, self)) arSawOrigin(tables, first, origin, now);
  }
}

static bool received(const mesh::Packet* packet) {
  if (packet->isRouteFlood()) {
    learn(packet);
    return false;
  }
  if (!packet->isRouteDirect()) return false;
  uint8_t type = packet->getPayloadType(), pc = packet->getPathHashCount();
  if (type == PAYLOAD_TYPE_CONTROL && packet->payload_len == HOP_REPLY_LEN && packet->payload[0] == HOP_REPLY) {
    if (pc == 0) {
      count_reply += arClearReply(slots, &packet->payload[1]);
      recount();
    }
    return true;
  }
  if (active > 0) {
    uint8_t key[AR_KEY_LEN];
    keyOf(packet, key);
    int cleared = arClearEcho(slots, key, pc);
    if (cleared > 0) {
      count_echo += cleared;
      recount();
    }
  }
  if (pc == 0 && packet->payload_len >= 1 && packet->payload[0] == self_hash[1].b[0]
      && (type == PAYLOAD_TYPE_ANON_REQ || type == PAYLOAD_TYPE_REQ || type == PAYLOAD_TYPE_RESPONSE || type == PAYLOAD_TYPE_TXT_MSG || type == PAYLOAD_TYPE_PATH)) {
    hopReply(packet);
  }
  return false;
}

static bool apply(uint32_t setting) {
  enabled = setting != MOD_ROUTE_OFF;
  if (setting != MOD_ROUTE_OFF) detour_on = setting == MOD_ROUTE_ON;
  if (!enabled) {
    memset(slots, 0, sizeof(slots));
    active = 0;
  }
  return saveSettings();
}

bool autoRouteDirect(ModDirectEvent event, const mesh::Packet* packet, uint32_t delay_millis) {
  if (!booted) boot();
  if (event == MOD_DIRECT_PRESENT) return true;
  if (event == MOD_DIRECT_ENABLED) return enabled;
  if (event == MOD_DIRECT_DETOUR) return detour_on;
  if (event == MOD_DIRECT_APPLY) return apply(delay_millis);
  if (!enabled) return false;
  switch (event) {
    case MOD_DIRECT_RECV: return received(packet);
    case MOD_DIRECT_FORWARD: track(packet, delay_millis, false); return false;
    case MOD_DIRECT_FORWARD_MULTI_ACK: track(packet, delay_millis, true); return false;
    case MOD_DIRECT_SENT: sent(packet); return false;
    case MOD_DIRECT_BUSY: return active > 0;
    default: return false;
  }
  return false;
}

static void tryDetour(int index) {
  mesh::Packet& copy = copies[index];
  uint8_t width = copy.getPathHashSize(), pc = copy.getPathHashCount();
  if (pc == 0 || width == 0 || width > AR_HASH_MAX) return;
  ArHash failing = arHash(copy.path, width), next, via;
  if (pc >= 2) next = arHash(&copy.path[width], width);
  else if (!isAck(&copy) && copy.payload_len > 0) next = arHash(copy.payload, 1);
  else return;
  if (arChoose(tables, failing, next, self_hash[width], millis(), &via)) {
    memcpy(copy.path, via.b, width);
    count_detour++;
  } else {
    count_noalt++;
  }
}

void autoRouteLoop() {
  if (!booted) boot();
  if (active == 0) return;
  uint32_t now = millis();
  for (int i = 0; i < AR_SLOTS; i++) {
    ArSlot& s = slots[i];
    if (!s.active || (int32_t)(now - s.due) < 0) continue;
    if (s.queued) {
      s.active = false;
      count_stale++;
      recount();
      continue;
    }
    if (s.sent >= s.limit || !modLoadAllowed()) {
      s.active = false;
      count_exhausted++;
      recount();
      continue;
    }
    if (detour_on && !s.detoured) {
      s.detoured = true;
      tryDetour(i);
    }
    if (!modDirectResend(&copies[i])) {
      s.due = now + 100;
      continue;
    }
    s.sent++;
    count_resent++;
    s.queued = true;
    s.due = now + AR_QUEUE_MS;
  }
}

static void formatHash(const ArHash& h, char* out) {
  for (uint8_t i = 0; i < h.len; i++) sprintf(out + i * 2, "%02x", h.b[i]);
}

static void linksReply(uint32_t timestamp, char* reply) {
  char row[150];
  int n = sprintf(row, ">");
  for (int i = 0; i < AR_LINKS; i++) {
    const ArLink& l = tables.links[i];
    if (l.at == 0) continue;
    char via[AR_HASH_MAX * 2 + 1], hop[AR_HASH_MAX * 2 + 1];
    formatHash(l.via, via);
    formatHash(l.hop, hop);
    n += sprintf(row + n, " %s>%s:%u/%u%s", hop, via, l.forward, l.reverse, arArmed(l) ? "*" : "");
    if (n > 110) {
      modCliAddRow(timestamp, row);
      n = 0;
      row[0] = 0;
    }
  }
  if (n > 0) modCliAddRow(timestamp, row);
  reply[0] = 0;
}

bool autoRouteHandleCli(const ModCliContext& context, char* command, char* reply) {
  if (!booted) boot();
  if (strcmp(command, "get autoroute") == 0) {
    sprintf(reply, "> %s, detour %s", enabled ? "on" : "off", detour_on ? "on" : "off");
    return true;
  }
  if (strcmp(command, "get autoroute.stats") == 0) {
    snprintf(reply, REPLY_MAX, "> resent=%lu echo=%lu reply=%lu exhausted=%lu detour=%lu noalt=%lu replied=%lu noslot=%lu stale=%lu", (unsigned long)count_resent, (unsigned long)count_echo, (unsigned long)count_reply,
            (unsigned long)count_exhausted, (unsigned long)count_detour, (unsigned long)count_noalt, (unsigned long)count_replied, (unsigned long)count_noslot, (unsigned long)count_stale);
    return true;
  }
  if (strcmp(command, "get autoroute.links") == 0) {
    linksReply(context.sender_timestamp, reply);
    return true;
  }
  uint8_t* target = nullptr;
  const char* value = nullptr;
  if (strncmp(command, "set autoroute ", 14) == 0) {
    target = &enabled;
    value = command + 14;
  } else if (strncmp(command, "set autoroute.detour ", 21) == 0) {
    target = &detour_on;
    value = command + 21;
  }
  if (target == nullptr) return false;
  bool on = strcmp(value, "on") == 0;
  if (!on && strcmp(value, "off") != 0) {
    strcpy(reply, "Error: use on or off");
    return true;
  }
  *target = on;
  if (!enabled) {
    memset(slots, 0, sizeof(slots));
    active = 0;
  }
  if (!saveSettings()) {
    strcpy(reply, "ERR: could not save");
    return true;
  }
  strcpy(reply, "OK");
  if (target == &enabled && on) {
    char get_repeat[] = "get repeat";
    char state[160] = "";
    modCliDispatch(0, get_repeat, state);
    if (strcmp(state, "> off") == 0) strcpy(reply, "OK - but repeat is off");
  }
  return true;
}
