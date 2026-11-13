#include <Arduino.h>
#include <string.h>
#include <Mesh.h>
#include <helpers/CommonCLI.h>
#include <helpers/RegionMap.h>
#include <helpers/TransportKeyStore.h>
#include <helpers/ModHooks.h>

// Services mods call into MeshCore, acting on what MyMesh's constructor binds; one copy serves the repeater and the room server.
static ModBindings bound;

void modBind(const ModBindings* bindings) {
  if (bindings != nullptr) bound = *bindings;
}

const ModBindings* modBindings() {
  return &bound;
}

void modCliDispatch(uint32_t sender_timestamp, char* command, char* reply) {
  if (bound.cli) bound.cli->handleCommand(sender_timestamp, command, reply);
}

bool modPolicyRead(ModPolicyValues* out) {
  if (out == nullptr || bound.prefs == nullptr) return false;
  out->flood_max = bound.prefs->flood_max;
  out->flood_max_unscoped = bound.prefs->flood_max_unscoped;
  out->flood_max_advert = bound.prefs->flood_max_advert;
  out->advert_interval = bound.prefs->advert_interval;
  out->flood_advert_interval = bound.prefs->flood_advert_interval;
  out->path_hash_mode = bound.prefs->path_hash_mode;
  out->loop_detect = bound.prefs->loop_detect;
  out->multi_acks = bound.prefs->multi_acks;
  out->airtime_factor = bound.prefs->airtime_factor;
  out->tx_delay_factor = bound.prefs->tx_delay_factor;
  out->rx_delay = bound.prefs->rx_delay_base;
  out->direct_tx_delay = bound.prefs->direct_tx_delay_factor;
  out->agc_reset_interval = bound.prefs->agc_reset_interval;
  return true;
}

bool modRadioPrefsGet(ModRadioValues* out) {
  if (out == nullptr || bound.prefs == nullptr) return false;
  out->freq = bound.prefs->freq;
  out->bw = bound.prefs->bw;
  out->sf = bound.prefs->sf;
  out->cr = bound.prefs->cr;
  return true;
}

bool modResolveRegionName(const char* name, ModRegionMatch* out) {
  if (name == nullptr || out == nullptr || bound.regions == nullptr) return false;
  memset(out, 0, sizeof(*out));
  RegionEntry* entry = strcmp(name, "*") == 0
                     ? &bound.regions->getWildcard()
                     : bound.regions->findByName(name);
  if (entry == nullptr) return true;
  if ((entry->flags & REGION_DENY_FLOOD) != 0) {
    out->state = MOD_REGION_DENY;
    return true;
  }
  out->state = MOD_REGION_ALLOW;
  if (entry->isWildcard()) return true;
  TransportKey key;
  if (bound.regions->getTransportKeysFor(*entry, &key, 1) != 1) return false;
  memcpy(out->key, key.key, sizeof(out->key));
  return true;
}

bool modRegionNameForKey(const uint8_t key[16], char* out, size_t capacity) {
  if (key == nullptr || out == nullptr || capacity == 0 ||
      bound.regions == nullptr) return false;
  for (int i = 0; i < bound.regions->getCount(); ++i) {
    const RegionEntry* entry = bound.regions->getByIdx(i);
    if (entry == nullptr || entry->isWildcard()) continue;
    TransportKey keys[4];
    int count = bound.regions->getTransportKeysFor(*entry, keys, 4);
    bool match = false;
    for (int k = 0; k < count; ++k) {
      if (memcmp(keys[k].key, key, 16) == 0) match = true;
    }
    if (!match) continue;
    const char* name = entry->name[0] == '#' ? entry->name + 1 : entry->name;
    size_t len = strlen(name);
    if (len + 1 > capacity) return false;
    memcpy(out, name, len + 1);
    return true;
  }
  return false;
}

bool modDirectResend(const mesh::Packet* packet) {
  if (bound.mesh == nullptr) return false;
  mesh::Packet* copy = bound.mesh->obtainNewPacket();
  if (copy == nullptr) return false;
  *copy = *packet;
  bound.mesh->sendPacket(copy, 0, 0);
  return true;
}

bool modDirectReply(const uint8_t* data, size_t len, uint32_t delay_millis) {
  if (bound.mesh == nullptr) return false;
  mesh::Packet* packet = bound.mesh->createControlData(data, len);
  if (packet == nullptr) return false;
  bound.mesh->sendZeroHop(packet, delay_millis);
  return true;
}

bool modPublisherKey(uint8_t out[32]) {
  if (out == nullptr || bound.mesh == nullptr) return false;
  memcpy(out, bound.mesh->self_id.pub_key, 32);
  return true;
}

bool modSignDetached(const uint8_t* data, size_t len, uint8_t signature[64]) {
  if (data == nullptr || len == 0 || signature == nullptr ||
      bound.mesh == nullptr || len > 0x7fffffffu) return false;
  bound.mesh->self_id.sign(signature, data, (int)len);
  return true;
}

#ifdef MOD_WITH_TX_HOOKS
struct ModPendingTx {
  const mesh::Packet* packet;
  uint32_t id;
};
static ModPendingTx pending_tx[4];
static uint32_t next_tx_id = 1;

static bool rememberTx(const mesh::Packet* packet, uint32_t id) {
  for (uint8_t i = 0; i < 4; ++i) {
    if (pending_tx[i].packet != nullptr) continue;
    pending_tx[i].packet = packet;
    pending_tx[i].id = id;
    return true;
  }
  return false;
}

uint32_t modTxDone(const mesh::Packet* packet) {
  for (uint8_t i = 0; i < 4; ++i) {
    if (pending_tx[i].packet != packet) continue;
    uint32_t id = pending_tx[i].id;
    pending_tx[i] = {};
    return id;
  }
  return 0;
}
#else
uint32_t modTxDone(const mesh::Packet* packet) {
  (void)packet;
  return 0;
}
#endif

bool modSendGroup(const uint8_t* secret, uint8_t hash, const uint8_t* data,
                  size_t len, bool scoped, const uint8_t scope_key[16],
                  uint32_t* packet_id, uint32_t* airtime_ms) {
#ifndef MOD_WITH_TX_HOOKS
  (void)secret; (void)hash; (void)data; (void)len; (void)scoped;
  (void)scope_key; (void)packet_id; (void)airtime_ms;
  return false;
#else
  if (secret == nullptr || data == nullptr || len == 0 || len > 255 ||
      bound.mesh == nullptr || bound.packets == nullptr || bound.flood_scoped == nullptr ||
      (bound.prefs != nullptr && bound.prefs->path_hash_mode > 2) ||
      (scoped && scope_key == nullptr)) return false;
  mesh::GroupChannel channel = {};
  channel.hash[0] = hash;
  memcpy(channel.secret, secret, sizeof(channel.secret));
  mesh::Packet* packet = bound.mesh->createGroupDatagram(
      PAYLOAD_TYPE_GRP_DATA, channel, data, len);
  if (packet == nullptr) return false;
  uint32_t id = next_tx_id++;
  if (next_tx_id == 0) next_tx_id = 1;
  if (!rememberTx(packet, id)) {
    bound.mesh->releasePacket(packet);
    return false;
  }
  uint8_t path_size = bound.prefs == nullptr ? 1 : bound.prefs->path_hash_mode + 1;
  if (scoped) {
    TransportKey scope;
    memcpy(scope.key, scope_key, sizeof(scope.key));
    bound.flood_scoped(scope, packet, 0, path_size);
  } else {
    bound.mesh->sendFlood(packet, 0, path_size);
  }
  bool queued = false;
  for (int i = 0; i < bound.packets->getOutboundTotal(); ++i) {
    if (bound.packets->getOutboundByIdx(i) == packet) queued = true;
  }
  if (!queued) {
    modTxDone(packet);
    return false;
  }
  if (packet_id != nullptr) *packet_id = id;
  if (airtime_ms != nullptr && bound.radio != nullptr) {
    *airtime_ms = bound.radio->getEstAirtimeFor(packet->getRawLength());
  }
  return true;
#endif
}

static struct {
  float freq, bw;
  uint8_t sf, cr;
  bool live;
} temp_radio;

void modTempRadioSet(float freq, float bw, uint8_t sf, uint8_t cr, bool live) {
  temp_radio = { freq, bw, sf, cr, live };
}

bool modTempRadioGet(float* freq, float* bw, uint8_t* sf, uint8_t* cr) {
  if (!temp_radio.live) return false;
  *freq = temp_radio.freq;
  *bw = temp_radio.bw;
  *sf = temp_radio.sf;
  *cr = temp_radio.cr;
  return true;
}
