#include <helpers/direct_retry/DirectRetryIntegration.h>

#include <Arduino.h>
#include <Packet.h>
#include <SPIFFS.h>
#include <Utils.h>
#include <helpers/ConfigSerializer.h>
#include <helpers/direct_retry/DirectRetryCore.h>

// Keeps the old name, so a node that had this on stays on after an update.
#define SETTINGS_PATH "/autoroute.json"
#define HOP_REPLY     0xA0
#define HOP_REPLY_LEN (1 + DR_REPLY_KEY)
#define REPLY_MAX     160

class SettingsFile : public ConfigSerializer {
  uint8_t& _on;
public:
  explicit SettingsFile(uint8_t& on) : _on(on) { }
protected:
  void structure() override {
    def("on", _on);
  }
};

static uint8_t enabled;
static bool booted;
static DrSlot slots[DR_SLOTS];
static mesh::Packet copies[DR_SLOTS];
static uint8_t active;
static uint8_t self_byte;
static uint32_t count_resent, count_echo, count_reply, count_exhausted, count_replied, count_noslot, count_stale;

static void boot() {
  booted = true;
  uint8_t key[32];
  if (modPublisherKey(key)) self_byte = key[0];
  if (!SPIFFS.begin(true) || !SPIFFS.exists(SETTINGS_PATH)) return;
  File f = SPIFFS.open(SETTINGS_PATH, "r");
  if (!f) return;
  SettingsFile file(enabled);
  file.loadSerial(f);
  f.close();
}

static bool saveSettings() {
  File f = SPIFFS.open(SETTINGS_PATH, "w", true);
  if (!f) return false;
  SettingsFile file(enabled);
  bool ok = file.saveSerial(f);
  f.close();
  return ok;
}

static void recount() {
  active = 0;
  for (int i = 0; i < DR_SLOTS; i++) active += slots[i].active;
}

static void keyOf(const mesh::Packet* packet, uint8_t* key) {
  if (packet->getPayloadType() == PAYLOAD_TYPE_MULTIPART && packet->payload_len >= 2 && (packet->payload[0] & 0x0F) == PAYLOAD_TYPE_ACK) {
    uint8_t type = PAYLOAD_TYPE_ACK;
    mesh::Utils::sha256(key, DR_KEY_LEN, &type, 1, &packet->payload[1], packet->payload_len - 1);
  } else {
    uint8_t full[MAX_HASH_SIZE];
    packet->calculatePacketHash(full);
    memcpy(key, full, DR_KEY_LEN);
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
  uint8_t key[DR_KEY_LEN];
  keyOf(&copy, key);
  int slot = drClaim(slots, key);
  if (slot < 0) {
    if (active == DR_SLOTS) count_noslot++;
    return;
  }
  DrSlot& s = slots[slot];
  memcpy(s.key, key, DR_KEY_LEN);
  s.active = true;
  s.queued = true;
  s.pc = pc;
  s.sent = 0;
  s.limit = pc == 0 ? DR_LAST_RESENDS : DR_RESENDS;
  s.due = millis() + delay_millis + DR_QUEUE_MS;
  copies[slot] = copy;
  recount();
}

static void sent(const mesh::Packet* packet) {
  if (active == 0 || !packet->isRouteDirect() || packet->getPayloadType() == PAYLOAD_TYPE_CONTROL) return;
  uint8_t key[DR_KEY_LEN];
  keyOf(packet, key);
  for (int i = 0; i < DR_SLOTS; i++) {
    DrSlot& s = slots[i];
    if (!s.active || memcmp(s.key, key, DR_KEY_LEN) != 0) continue;
    s.queued = false;
    s.due = millis() + drWait(modAirtimeFor(copies[i].getRawLength()), s.sent, random(0, 200));
  }
}

static void hopReply(const mesh::Packet* packet) {
  uint8_t key[DR_KEY_LEN], data[HOP_REPLY_LEN];
  keyOf(packet, key);
  data[0] = HOP_REPLY;
  memcpy(&data[1], key, DR_REPLY_KEY);
  if (modDirectReply(data, sizeof(data), random(40, 160))) count_replied++;
}

static bool received(const mesh::Packet* packet) {
  if (!packet->isRouteDirect()) return false;
  uint8_t type = packet->getPayloadType(), pc = packet->getPathHashCount();
  if (type == PAYLOAD_TYPE_CONTROL && packet->payload_len == HOP_REPLY_LEN && packet->payload[0] == HOP_REPLY) {
    if (pc == 0) {
      count_reply += drClearReply(slots, &packet->payload[1]);
      recount();
    }
    return true;
  }
  if (active > 0) {
    uint8_t key[DR_KEY_LEN];
    keyOf(packet, key);
    int cleared = drClearEcho(slots, key, pc);
    if (cleared > 0) {
      count_echo += cleared;
      recount();
    }
  }
  if (pc == 0 && packet->payload_len >= 1 && packet->payload[0] == self_byte
      && (type == PAYLOAD_TYPE_ANON_REQ || type == PAYLOAD_TYPE_REQ || type == PAYLOAD_TYPE_RESPONSE || type == PAYLOAD_TYPE_TXT_MSG || type == PAYLOAD_TYPE_PATH)) {
    hopReply(packet);
  }
  return false;
}

static void clearSlots() {
  memset(slots, 0, sizeof(slots));
  active = 0;
}

static bool apply(uint32_t setting) {
  enabled = setting != MOD_ROUTE_OFF;
  if (!enabled) clearSlots();
  return saveSettings();
}

bool directRetryDirect(ModDirectEvent event, const mesh::Packet* packet, uint32_t delay_millis) {
  if (!booted) boot();
  if (event == MOD_DIRECT_PRESENT) return true;
  if (event == MOD_DIRECT_ENABLED) return enabled;
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

void directRetryLoop() {
  if (!booted) boot();
  if (active == 0) return;
  uint32_t now = millis();
  for (int i = 0; i < DR_SLOTS; i++) {
    DrSlot& s = slots[i];
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
    if (!modDirectResend(&copies[i])) {
      s.due = now + 100;
      continue;
    }
    s.sent++;
    count_resent++;
    s.queued = true;
    s.due = now + DR_QUEUE_MS;
  }
}

bool directRetryHandleCli(const ModCliContext& context, char* command, char* reply) {
  if (!booted) boot();
  if (strcmp(command, "get direct.retry") == 0) {
    sprintf(reply, "> %s", enabled ? "on" : "off");
    return true;
  }
  if (strcmp(command, "get direct.retry.stats") == 0) {
    snprintf(reply, REPLY_MAX, "> resent=%lu echo=%lu reply=%lu exhausted=%lu replied=%lu noslot=%lu stale=%lu", (unsigned long)count_resent, (unsigned long)count_echo, (unsigned long)count_reply,
            (unsigned long)count_exhausted, (unsigned long)count_replied, (unsigned long)count_noslot, (unsigned long)count_stale);
    return true;
  }
  if (strncmp(command, "set direct.retry ", 17) != 0) return false;
  const char* value = command + 17;
  bool on = strcmp(value, "on") == 0;
  if (!on && strcmp(value, "off") != 0) {
    strcpy(reply, "Error: use on or off");
    return true;
  }
  enabled = on;
  if (!enabled) clearSlots();
  if (!saveSettings()) {
    strcpy(reply, "ERR: could not save");
    return true;
  }
  strcpy(reply, "OK");
  if (on) {
    char get_repeat[] = "get repeat";
    char state[160] = "";
    modCliDispatch(0, get_repeat, state);
    if (strcmp(state, "> off") == 0) strcpy(reply, "OK - but repeat is off");
  }
  return true;
}
