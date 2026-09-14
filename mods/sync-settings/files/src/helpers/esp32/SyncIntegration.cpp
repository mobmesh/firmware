#include <helpers/sync_settings/SyncIntegration.h>

#include <helpers/sync_settings/SyncCampaign.h>
#include <helpers/sync_settings/SyncPublish.h>
#include <helpers/sync_settings/SyncRegion.h>
#include <helpers/sync_settings/SyncSelect.h>
#include <helpers/sync_settings/SyncStore.h>

#include <Arduino.h>
#include <Identity.h>
#include <Packet.h>
#include <SHA256.h>
#include <Utils.h>
#include <helpers/ModHooks.h>
#include <helpers/RegionMap.h>
#include <SPIFFS.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace mobmesh {
namespace sync {

static Inbox inbox;
static bool booted;
static bool ready;
static bool config_ready;
static bool enabled;
static bool overlay_ready;
static bool policy_recovery_ready;
#if SYNC_SETTINGS_WITH_REGION
static Regions regions;
#endif
static bool region_state_ready;
static bool policy_state_ready;
static bool trust_ready;
static bool config_degraded;
static bool overlay_degraded;
static bool region_state_degraded;
static bool policy_state_degraded;
static bool recovery_degraded;
static bool trust_degraded;
static bool dirty;
#if SYNC_SETTINGS_WITH_REGION
static bool disable_pending;
enum EditKind : uint8_t {
  EDIT_NONE,
  EDIT_DENY,
  EDIT_REMOVE,
  EDIT_CLEAR,
  EDIT_RELOAD,
};
struct PendingEdit {
  EditKind kind;
  char name[REGION_NAME_MAX + 1];
};
static PendingEdit pending_edit;
#endif
static Config config;
static Publishers publishers;
#if SYNC_SETTINGS_WITH_REGION
static DatasetState region_state;
#endif
#if SYNC_SETTINGS_WITH_POLICY
static DatasetState policy_state;
#endif
#if SYNC_SETTINGS_WITH_REGION
static RegionRecord region_record;
#endif
#if SYNC_SETTINGS_WITH_POLICY
static PolicyRecovery policy_recovery;
#endif

class Temp {
 public:
  explicit Temp(size_t size) : data_(static_cast<uint8_t*>(malloc(size))) {}
  ~Temp() { free(data_); }
  operator uint8_t*() const { return data_; }
  explicit operator bool() const { return data_ != nullptr; }

 private:
  Temp(const Temp&);
  Temp& operator=(const Temp&);
  uint8_t* data_;
};

static bool txSign(const uint8_t* data, size_t len, uint8_t signature[64], void*) {
  return modSignDetached(data, len, signature);
}

static bool txSend(const uint8_t* frame, size_t len, bool scoped,
                   const uint8_t key[16], uint32_t& id, void*) {
  uint8_t payload[3 + FRAME_MAX];
  size_t payload_len = writeCarrier(frame, len, payload, sizeof(payload));
  return payload_len != 0 &&
         modSendGroup(CARRIER_KEY, CARRIER_HASH, payload, payload_len,
                      scoped, key, &id);
}

static void txFree(uint8_t* data, void*) { free(data); }
static bool txFinish(uint8_t dataset, TxEnd ending, uint8_t notices, void*);

static Transmitter& transmitter() {
  static const TxOps ops = {txSign, txSend, txFree, txFinish, nullptr};
  static Transmitter value(ops);
  return value;
}

struct PairPaths {
  const char* path[2];
};

static PairPaths CONFIG_PATHS = {{"/sync_cfg0", "/sync_cfg1"}};
static PairPaths PUBLISHER_PATHS = {{"/sync_pub0", "/sync_pub1"}};
#if SYNC_SETTINGS_WITH_REGION
static PairPaths REGION_PATHS = {{"/sync_ro0", "/sync_ro1"}};
#endif
static PairPaths REGION_STATE_PATHS = {{"/sync_rs0", "/sync_rs1"}};
static PairPaths POLICY_STATE_PATHS = {{"/sync_ps0", "/sync_ps1"}};
#if SYNC_SETTINGS_WITH_POLICY
static PairPaths POLICY_RECOVERY_PATHS = {{"/sync_pr0", "/sync_pr1"}};
#endif

static bool fileExists(uint8_t slot, void* context) {
  PairPaths* paths = static_cast<PairPaths*>(context);
  return SPIFFS.exists(paths->path[slot]);
}

static bool fileRead(uint8_t slot, uint8_t* out, size_t capacity,
                     size_t& len, void* context) {
  PairPaths* paths = static_cast<PairPaths*>(context);
  File file = SPIFFS.open(paths->path[slot], "r");
  if (!file) return false;
  len = file.size();
  bool ok = len <= capacity && file.read(out, len) == len;
  file.close();
  return ok;
}

static bool fileWrite(uint8_t slot, const uint8_t* data, size_t len, void* context) {
  PairPaths* paths = static_cast<PairPaths*>(context);
  File file = SPIFFS.open(paths->path[slot], "w", true);
  if (!file) return false;
  bool ok = file.write(data, len) == len;
  file.close();
  return ok;
}

static PairIO pairIO(PairPaths& paths) {
  PairIO io = {fileExists, fileRead, fileWrite, &paths};
  return io;
}

static void hash(const uint8_t* data, size_t len, uint8_t out[32], void*) {
  SHA256 sha;
  sha.update(data, len);
  sha.finalize(out, 32);
}

static int decrypt(const uint8_t key[32], uint8_t* out,
                   const uint8_t* data, size_t len, void*) {
  if (len > 0x7fffffffu) return -1;
  return mesh::Utils::MACThenDecrypt(key, out, data, (int)len);
}

static void receive(const mesh::Packet* packet, bool accepted,
                    const uint8_t* scope_key) {
  if (!accepted || packet == nullptr ||
      (packet->getRouteType() != ROUTE_TYPE_TRANSPORT_FLOOD &&
       packet->getRouteType() != ROUTE_TYPE_FLOOD) ||
      packet->getPayloadType() != PAYLOAD_TYPE_GRP_DATA) return;
  uint8_t frame[FRAME_MAX];
  uint8_t len = 0;
  if (readCarrier(packet->payload, packet->payload_len, frame, sizeof(frame),
                  len, decrypt, nullptr) == CARRIER_OK) {
    bool scoped = packet->getRouteType() == ROUTE_TYPE_TRANSPORT_FLOOD;
    inbox.push(frame, len, scoped, scope_key);
  }
}

static StoreResult configValid(const uint8_t* data, size_t len, void*) {
  Config value;
  return readConfig(data, len, value);
}

#if SYNC_SETTINGS_WITH_REGION
static StoreResult regionsValid(const uint8_t* data, size_t len, void*) {
  RegionRecord value;
  return readRegionRecord(data, len, value, hash, nullptr);
}
#endif

static StoreResult publishersValid(const uint8_t* data, size_t len, void*) {
  Publishers value;
  return readPublishers(data, len, value);
}

static StoreResult stateValid(const uint8_t* data, size_t len, void*) {
  DatasetState value;
  return readState(data, len, value);
}

#if SYNC_SETTINGS_WITH_POLICY
static StoreResult recoveryValid(const uint8_t* data, size_t len, void*) {
  PolicyRecovery value;
  return readPolicyRecovery(data, len, value, hash, nullptr);
}
#endif

#if SYNC_SETTINGS_WITH_REGION
static void makeKey(const uint8_t* name, uint8_t len, uint8_t out[16], void*) {
  SHA256 sha;
  const uint8_t marker = '#';
  sha.update(&marker, 1);
  sha.update(name, len);
  sha.finalize(out, 16);
}

bool installRegions(const uint8_t* data, size_t len) {
  return regions.replace(data, len, makeKey, nullptr) == REGION_OK;
}

void enableRegions(bool value) {
  enabled = value && overlay_ready && region_state_ready;
}

bool regionsEnabled() { return enabled; }

bool storageReady() { return ready; }
#else
bool installRegions(const uint8_t*, size_t) { return false; }
void enableRegions(bool) {}
bool regionsEnabled() { return false; }
bool storageReady() { return ready; }
#endif

static bool loadConfig(uint8_t* scratch) {
  PairView pair;
  PairResult result = readPair(pairIO(CONFIG_PATHS), STORE_CONFIG, pair, scratch,
                               STORE_MAX, hash, configValid, nullptr);
  if (result == PAIR_ABSENT) {
    memset(&config, 0, sizeof(config));
    config_degraded = false;
    return true;
  }
  if (result == PAIR_OK) config_degraded = pair.degraded;
  return result == PAIR_OK &&
         readConfig(pair.record.payload, pair.record.payload_len, config) == STORE_OK;
}

#if SYNC_SETTINGS_WITH_REGION
static bool loadRegions(uint8_t* scratch) {
  PairView pair;
  PairResult result = readPair(pairIO(REGION_PATHS), STORE_REGIONS, pair, scratch,
                               STORE_MAX, hash, regionsValid, nullptr);
  if (result == PAIR_ABSENT) {
    regions.clear();
    memset(&region_record, 0, sizeof(region_record));
    overlay_degraded = false;
    return true;
  }
  if (result != PAIR_OK) return false;
  overlay_degraded = pair.degraded;
  RegionRecord value;
  if (readRegionRecord(pair.record.payload, pair.record.payload_len,
                       value, hash, nullptr) != STORE_OK ||
      !installRegions(value.regions, value.regions_len)) return false;
  region_record = value;
  region_record.regions = nullptr;
  return true;
}
#endif

static bool loadPublishers(uint8_t* scratch) {
  PairView pair;
  PairResult result = readPair(pairIO(PUBLISHER_PATHS), STORE_PUBLISHERS, pair,
                               scratch, STORE_MAX, hash, publishersValid, nullptr);
  if (result == PAIR_ABSENT) {
    defaultPublishers(publishers);
    trust_degraded = false;
    return true;
  }
  if (result == PAIR_OK) trust_degraded = pair.degraded;
  return result == PAIR_OK &&
         readPublishers(pair.record.payload, pair.record.payload_len,
                        publishers) == STORE_OK;
}

#if SYNC_SETTINGS_WITH_REGION
static bool storedRegionsValid(uint8_t* scratch, bool& degraded) {
  PairView pair;
  PairResult result = readPair(pairIO(REGION_PATHS), STORE_REGIONS, pair, scratch,
                               STORE_MAX, hash, regionsValid, nullptr);
  degraded = result == PAIR_OK && pair.degraded;
  return result == PAIR_ABSENT || result == PAIR_OK;
}
#endif

static bool loadState(PairPaths& paths, uint8_t type, DatasetState& state,
                      uint8_t* scratch) {
  PairView pair;
  PairResult result = readPair(pairIO(paths), type,
                               pair, scratch, STORE_MAX, hash, stateValid, nullptr);
  if (result == PAIR_ABSENT) {
    defaultState(state);
    if (type == STORE_REGION_STATE) region_state_degraded = false;
    else policy_state_degraded = false;
    return true;
  }
  if (result == PAIR_OK) {
    if (type == STORE_REGION_STATE) region_state_degraded = pair.degraded;
    else policy_state_degraded = pair.degraded;
  }
  return result == PAIR_OK &&
         readState(pair.record.payload, pair.record.payload_len, state) == STORE_OK;
}

static size_t buildConfigPayload(uint8_t* out, size_t capacity, void* context) {
  return capacity < 17 ? 0 : writeConfig(*static_cast<Config*>(context), out);
}

static size_t buildPublisherPayload(uint8_t* out, size_t capacity, void* context) {
  return writePublishers(*static_cast<Publishers*>(context), out, capacity);
}

static size_t buildStatePayload(uint8_t* out, size_t capacity, void* context) {
  return writeState(*static_cast<DatasetState*>(context), out, capacity);
}

#if SYNC_SETTINGS_WITH_REGION
static size_t buildRegionPayload(uint8_t* out, size_t capacity, void*) {
  RegionRecord value = {};
  value.regions = regions.data();
  value.regions_len = regions.size();
  return writeRegionRecord(value, out, capacity, hash, nullptr);
}

static size_t buildRegionRecordPayload(uint8_t* out, size_t capacity, void* context) {
  return writeRegionRecord(*static_cast<RegionRecord*>(context), out, capacity,
                           hash, nullptr);
}
#endif

#if SYNC_SETTINGS_WITH_POLICY
static size_t buildRecoveryPayload(uint8_t* out, size_t capacity, void* context) {
  return writePolicyRecovery(*static_cast<PolicyRecovery*>(context), out,
                             capacity, hash, nullptr);
}
#endif

static bool saveConfig(const Config& value, uint8_t* scratch) {
  PairView pair;
  bool ok = buildPair(pairIO(CONFIG_PATHS), STORE_CONFIG, pair, scratch, STORE_MAX,
                      hash, configValid, buildConfigPayload,
                      const_cast<Config*>(&value)) == PAIR_OK;
  if (ok) config_degraded = false;
  return ok;
}

static bool savePublishers(const Publishers& value, uint8_t* scratch) {
  PairView pair;
  bool ok = buildPair(pairIO(PUBLISHER_PATHS), STORE_PUBLISHERS, pair, scratch,
                      STORE_MAX, hash, publishersValid, buildPublisherPayload,
                      const_cast<Publishers*>(&value)) == PAIR_OK;
  if (ok) trust_degraded = false;
  return ok;
}

static bool saveState(PairPaths& paths, uint8_t type, const DatasetState& value,
                      uint8_t* scratch) {
  PairView pair;
  bool ok = buildPair(pairIO(paths), type, pair, scratch, STORE_MAX, hash,
                      stateValid, buildStatePayload,
                      const_cast<DatasetState*>(&value)) == PAIR_OK;
  if (ok) {
    if (type == STORE_REGION_STATE) region_state_degraded = false;
    else policy_state_degraded = false;
  }
  return ok;
}

#if SYNC_SETTINGS_WITH_REGION
static bool saveRegions(uint8_t* scratch) {
  PairView pair;
  bool ok = buildPair(pairIO(REGION_PATHS), STORE_REGIONS, pair, scratch, STORE_MAX,
                      hash, regionsValid, buildRegionPayload, nullptr) == PAIR_OK;
  if (ok) overlay_degraded = false;
  return ok;
}

static bool saveRegionRecord(const RegionRecord& value, uint8_t* scratch) {
  PairView pair;
  bool ok = buildPair(pairIO(REGION_PATHS), STORE_REGIONS, pair, scratch, STORE_MAX,
                      hash, regionsValid, buildRegionRecordPayload,
                      const_cast<RegionRecord*>(&value)) == PAIR_OK;
  if (ok) overlay_degraded = false;
  return ok;
}
#endif

#if SYNC_SETTINGS_WITH_POLICY
static bool saveRecovery(const PolicyRecovery& value, uint8_t* scratch) {
  PairView pair;
  if (buildPair(pairIO(POLICY_RECOVERY_PATHS), STORE_POLICY_RECOVERY, pair,
                scratch, STORE_MAX, hash, recoveryValid,
                buildRecoveryPayload,
                const_cast<PolicyRecovery*>(&value)) != PAIR_OK) return false;
  recovery_degraded = false;
  policy_recovery = value;
  return true;
}
#endif

static DatasetState& stateFor(uint8_t dataset) {
#if SYNC_SETTINGS_WITH_REGION && SYNC_SETTINGS_WITH_POLICY
  return dataset == REGION ? region_state : policy_state;
#elif SYNC_SETTINGS_WITH_REGION
  (void)dataset;
  return region_state;
#else
  (void)dataset;
  return policy_state;
#endif
}

static PairPaths& statePaths(uint8_t dataset) {
#if SYNC_SETTINGS_WITH_REGION && SYNC_SETTINGS_WITH_POLICY
  return dataset == REGION ? REGION_STATE_PATHS : POLICY_STATE_PATHS;
#elif SYNC_SETTINGS_WITH_REGION
  (void)dataset;
  return REGION_STATE_PATHS;
#else
  (void)dataset;
  return POLICY_STATE_PATHS;
#endif
}

static uint8_t stateType(uint8_t dataset) {
#if SYNC_SETTINGS_WITH_REGION && SYNC_SETTINGS_WITH_POLICY
  return dataset == REGION ? STORE_REGION_STATE : STORE_POLICY_STATE;
#elif SYNC_SETTINGS_WITH_REGION
  (void)dataset;
  return STORE_REGION_STATE;
#else
  (void)dataset;
  return STORE_POLICY_STATE;
#endif
}

static void clearGuard(DatasetState& state) {
  state.guard = GUARD_IDLE;
  state.guard_generation = 0;
  state.campaign_start = 0;
  state.guard_days = 0;
  state.route_kind = 0;
  memset(state.route_key, 0, sizeof(state.route_key));
  memset(state.manifest_hash, 0, sizeof(state.manifest_hash));
}

static bool clockSane(uint32_t now) { return now > 1715770351u; }

static bool guardExpired(const DatasetState& state, uint32_t now) {
  return clockSane(now) && state.guard != GUARD_IDLE &&
         (uint64_t)now >= (uint64_t)state.campaign_start +
                          (uint64_t)state.guard_days * 86400u;
}

static bool txFinish(uint8_t dataset, TxEnd ending, uint8_t notices, void*) {
  DatasetState next = stateFor(dataset);
  if (ending == TX_QUIET) {
    next.guard = GUARD_QUIET;
  } else {
    uint32_t generation = next.guard_generation;
    uint32_t start = next.campaign_start;
    uint8_t days = next.guard_days;
    clearGuard(next);
    if (ending == TX_ABORTED) {
      next.warning_generation = 0;
      next.warning_time = 0;
      next.warning_expiry = 0;
      next.abort_sent = 0;
      next.warning_reason = 0;
    } else {
      uint32_t now = modClockGet();
      if (!clockSane(now)) return false;
      next.warning_generation = generation;
      next.warning_time = now;
      next.warning_expiry = start + (uint32_t)days * 86400u;
      next.abort_sent = notices;
      next.warning_reason = 1;
    }
  }
  Temp scratch(STORE_MAX);
  if (!saveState(statePaths(dataset), stateType(dataset), next, scratch)) return false;
  stateFor(dataset) = next;
  return true;
}

#if SYNC_SETTINGS_WITH_POLICY
static bool loadRecovery(uint8_t* scratch) {
  PairView pair;
  PairResult result = readPair(pairIO(POLICY_RECOVERY_PATHS),
                               STORE_POLICY_RECOVERY, pair, scratch,
                               STORE_MAX, hash, recoveryValid, nullptr);
  if (result == PAIR_ABSENT) {
    memset(&policy_recovery, 0, sizeof(policy_recovery));
    recovery_degraded = false;
    return true;
  }
  if (result == PAIR_OK) recovery_degraded = pair.degraded;
  return result == PAIR_OK &&
         readPolicyRecovery(pair.record.payload, pair.record.payload_len,
                            policy_recovery, hash, nullptr) == STORE_OK;
}
#endif

static bool acceptsCampaign(uint8_t dataset, const char* channel, void*) {
  if (!config_ready || !trust_ready || config.channel_len == 0) return false;
  if (channel != nullptr && strcmp(channel, config.channel) != 0) return false;
  if (transmitter().active(dataset) || transmitter().aborting(dataset)) return false;
#if SYNC_SETTINGS_WITH_REGION
  if (dataset == REGION) return region_state_ready && region_state.enabled;
#endif
#if SYNC_SETTINGS_WITH_POLICY
  return dataset == POLICY && policy_state_ready && policy_recovery_ready &&
         policy_state.enabled;
#else
  return false;
#endif
}

static bool verifyFrame(const uint8_t key[32], const uint8_t* data, size_t len,
                        const uint8_t signature[64], void*) {
  if (len > 0x7fffffffu) return false;
  mesh::Identity identity(key);
  return identity.verify(signature, data, (int)len);
}

static uint8_t* allocateFrame(size_t len, void*) {
  return static_cast<uint8_t*>(malloc(len));
}

static void releaseFrame(uint8_t* data, void*) { free(data); }

static bool persistReplay(uint8_t dataset, const DatasetState& state, void*) {
  Temp scratch(STORE_MAX);
#if SYNC_SETTINGS_WITH_REGION
  if (dataset == REGION) {
    return region_state_ready &&
           saveState(REGION_STATE_PATHS, STORE_REGION_STATE, state, scratch);
  }
#endif
#if SYNC_SETTINGS_WITH_POLICY
  return dataset == POLICY && policy_state_ready &&
         saveState(POLICY_STATE_PATHS, STORE_POLICY_STATE, state, scratch);
#else
  return false;
#endif
}

static bool validateDataset(uint8_t dataset, const uint8_t* data,
                            size_t len, void*) {
#if SYNC_SETTINGS_WITH_REGION
  if (dataset == REGION) {
    RegionIndex index;
    return indexRegions(data, len, index) == REGION_OK;
  }
#endif
#if SYNC_SETTINGS_WITH_POLICY
  return dataset == POLICY && validPolicyPayload(data, len);
#else
  return false;
#endif
}

#if SYNC_SETTINGS_WITH_POLICY
static bool readNativePolicy(uint8_t out[POLICY_DATA_LEN]) {
  ModPolicyValues native;
  if (!modPolicyRead(&native) || native.multi_acks > 1) return false;
  PolicyProfile profile = {};
  profile.flood_max = native.flood_max;
  profile.flood_unscoped = native.flood_max_unscoped;
  profile.flood_advert = native.flood_max_advert;
  profile.advert_ticks = native.advert_interval;
  profile.flood_advert_hours = native.flood_advert_interval;
  profile.path_mode = native.path_hash_mode;
  profile.loop_detect = native.loop_detect;
  profile.multi_acks = native.multi_acks != 0;
  profile.airtime_factor = native.airtime_factor;
  profile.tx_delay_factor = native.tx_delay_factor;
  profile.agc_ticks = native.agc_reset_interval;
  return writePolicyPayload(profile, out);
}

static bool nativeSet(const char* format, ...) {
  char command[80];
  va_list args;
  va_start(args, format);
  int length = vsnprintf(command, sizeof(command), format, args);
  va_end(args);
  if (length < 0 || (size_t)length >= sizeof(command)) return false;
  char reply[160];
  modCliDispatch(0, command, reply);
  return reply[0] == 'O' && reply[1] == 'K' &&
         (reply[2] == 0 || reply[2] == ' ' || reply[2] == '-');
}

static bool applyNativePolicy(const uint8_t data[POLICY_DATA_LEN]) {
  PolicyProfile value;
  if (!readPolicyPayload(data, value)) return false;
  static const char* loop_name[] = {"off", "minimal", "moderate", "strict"};
  if (!nativeSet("set flood.max %u", value.flood_max) ||
      !nativeSet("set flood.max.unscoped %u", value.flood_unscoped) ||
      !nativeSet("set flood.max.advert %u", value.flood_advert) ||
      !nativeSet("set advert.interval %u", (unsigned)value.advert_ticks * 2u) ||
      !nativeSet("set flood.advert.interval %u", value.flood_advert_hours) ||
      !nativeSet("set path.hash.mode %u", value.path_mode) ||
      !nativeSet("set loop.detect %s", loop_name[value.loop_detect]) ||
      !nativeSet("set multi.acks %u", value.multi_acks ? 1 : 0) ||
      !nativeSet("set af %.9g", (double)value.airtime_factor) ||
      !nativeSet("set txdelay %.9g", (double)value.tx_delay_factor) ||
      !nativeSet("set agc.reset.interval %u", (unsigned)value.agc_ticks * 4u)) {
    return false;
  }
  uint8_t actual[POLICY_DATA_LEN];
  return readNativePolicy(actual) && memcmp(actual, data, sizeof(actual)) == 0;
}

static bool settlePolicy(bool applied, uint8_t* scratch) {
  DatasetState next = policy_state;
  if (applied &&
      setPublisherReplay(next, policy_recovery.publisher_id,
                         policy_recovery.generation,
                         policy_recovery.digest) != STORE_OK) return false;
  next.settled_receipt = policy_recovery.receipt_id;
  if (!saveState(POLICY_STATE_PATHS, STORE_POLICY_STATE, next, scratch)) return false;
  policy_state = next;
  PolicyRecovery idle = {};
  return saveRecovery(idle, scratch);
}

static bool restorePolicy(uint8_t* scratch) {
  if (policy_recovery.phase != RECOVERY_RESTORING) {
    PolicyRecovery restoring = policy_recovery;
    restoring.phase = RECOVERY_RESTORING;
    if (!saveRecovery(restoring, scratch)) return false;
  }
  uint8_t current[POLICY_DATA_LEN];
  if ((!readNativePolicy(current) ||
       memcmp(current, policy_recovery.prior, sizeof(current)) != 0) &&
      !applyNativePolicy(policy_recovery.prior)) return false;
  return settlePolicy(false, scratch);
}

static bool recoverPolicy(uint8_t* scratch) {
  if (policy_recovery.phase == RECOVERY_IDLE) return true;
  uint8_t current[POLICY_DATA_LEN];
  if (!readNativePolicy(current)) return false;
  bool target = memcmp(current, policy_recovery.target, sizeof(current)) == 0;
  bool prior = memcmp(current, policy_recovery.prior, sizeof(current)) == 0;

  if (policy_recovery.phase == RECOVERY_PREPARED) {
    if (!target && prior && !applyNativePolicy(policy_recovery.target)) {
      return restorePolicy(scratch);
    }
    if (!target && !prior) return restorePolicy(scratch);
    PolicyRecovery applied = policy_recovery;
    applied.phase = RECOVERY_APPLIED;
    if (!saveRecovery(applied, scratch)) return false;
    return settlePolicy(true, scratch);
  }
  if (policy_recovery.phase == RECOVERY_APPLIED) {
    return target ? settlePolicy(true, scratch) : restorePolicy(scratch);
  }
  return restorePolicy(scratch);
}
#endif

static bool applyCampaign(uint8_t dataset, const Campaign& campaign,
                          DatasetState& next, void*) {
#if SYNC_SETTINGS_WITH_POLICY
  if (dataset == POLICY) {
    Temp scratch(STORE_MAX);
    if (policy_recovery.phase != RECOVERY_IDLE && !recoverPolicy(scratch)) {
      return false;
    }
    uint8_t prior[POLICY_DATA_LEN];
    if (!readNativePolicy(prior)) return false;
    PolicyRecovery recovery = {};
    recovery.phase = RECOVERY_PREPARED;
    recovery.reset = campaign.reset;
    recovery.receipt_id = policy_state.settled_receipt + 1;
    if (recovery.receipt_id == 0) recovery.receipt_id = 1;
    recovery.publisher_id = campaign.publisher_id;
    recovery.generation = campaign.generation;
    memcpy(recovery.digest, campaign.digest, sizeof(recovery.digest));
    memcpy(recovery.prior, prior, sizeof(recovery.prior));
    memcpy(recovery.target, campaign.data, sizeof(recovery.target));
    if (!saveRecovery(recovery, scratch) || !recoverPolicy(scratch)) return false;
    next = policy_state;
    return true;
  }
#endif
#if SYNC_SETTINGS_WITH_REGION
  if (dataset != REGION) return false;
  uint32_t receipt = next.settled_receipt + 1;
  if (receipt == 0) receipt = 1;
  RegionRecord record = {};
  record.receipt = true;
  record.reset = campaign.reset;
  record.receipt_id = receipt;
  record.publisher_id = campaign.publisher_id;
  record.generation = campaign.generation;
  memcpy(record.digest, campaign.digest, sizeof(record.digest));
  record.regions = campaign.data;
  record.regions_len = campaign.data_len;

  Temp scratch(STORE_MAX);
  if (!saveRegionRecord(record, scratch)) return false;
  next.settled_receipt = receipt;
  if (!saveState(REGION_STATE_PATHS, STORE_REGION_STATE, next, scratch)) return false;
  if (!installRegions(campaign.data, campaign.data_len)) return false;
  region_record = record;
  region_record.regions = nullptr;
  dirty = false;
  return true;
#else
  (void)dataset;
  (void)campaign;
  (void)next;
  return false;
#endif
}

static ReceiveOps receive_ops = {
  acceptsCampaign, verifyFrame, hash, allocateFrame, releaseFrame,
  persistReplay, validateDataset, applyCampaign, nullptr,
};
static Receiver receiver(
    publishers,
#if SYNC_SETTINGS_WITH_REGION
    &region_state,
#else
    nullptr,
#endif
#if SYNC_SETTINGS_WITH_POLICY
    &policy_state,
#else
    nullptr,
#endif
    receive_ops);

static bool eraseStoredReplay(PairPaths& paths, uint8_t type, uint16_t id,
                              DatasetState* resident, bool resident_ready,
                              uint8_t* scratch) {
  DatasetState value;
  if (resident != nullptr) {
    if (!resident_ready) return false;
    value = *resident;
  } else {
    PairView pair;
    PairResult result = readPair(pairIO(paths), type, pair, scratch, STORE_MAX,
                                 hash, stateValid, nullptr);
    if (result == PAIR_ABSENT) return true;
    if (result != PAIR_OK ||
        readState(pair.record.payload, pair.record.payload_len, value) != STORE_OK) {
      return false;
    }
  }
  erasePublisherReplay(value, id);
  if (!saveState(paths, type, value, scratch)) return false;
  if (resident != nullptr) *resident = value;
  return true;
}

static bool finishForget(uint8_t* scratch) {
  uint16_t id = publishers.pending_forget;
  if (id == 0) return false;
  if (!eraseStoredReplay(REGION_STATE_PATHS, STORE_REGION_STATE, id,
#if SYNC_SETTINGS_WITH_REGION
                         &region_state, region_state_ready,
#else
                         nullptr, false,
#endif
                         scratch) ||
      !eraseStoredReplay(POLICY_STATE_PATHS, STORE_POLICY_STATE, id,
#if SYNC_SETTINGS_WITH_POLICY
                         &policy_state, policy_state_ready,
#else
                         nullptr, false,
#endif
                         scratch)) return false;

  Publishers next = publishers;
  if (completePublisherForget(next) != TRUST_OK) return false;
  if (!savePublishers(next, scratch)) return false;
  publishers = next;
  return true;
}

static bool publisherKnown(uint16_t id) {
  for (uint8_t i = 0; i < publishers.count; ++i) {
    if (publishers.record[i].id == id) return true;
  }
  return false;
}

#if SYNC_SETTINGS_WITH_REGION
static bool settleRegionReceipt(uint8_t* scratch) {
  if (!region_record.receipt) return true;
  SerialOrder order = serialOrder(region_record.receipt_id,
                                  region_state.settled_receipt);
  if (order == SERIAL_EQUAL || order == SERIAL_BEFORE) return true;
  if (order != SERIAL_AFTER || !trust_ready ||
      !publisherKnown(region_record.publisher_id)) return false;
  DatasetState next = region_state;
  if (setPublisherReplay(next, region_record.publisher_id,
                         region_record.generation,
                         region_record.digest) != STORE_OK) return false;
  next.settled_receipt = region_record.receipt_id;
  if (!saveState(REGION_STATE_PATHS, STORE_REGION_STATE, next, scratch)) return false;
  region_state = next;
  return true;
}
#endif

static bool settleGuard(uint8_t dataset, uint8_t* scratch) {
  DatasetState next = stateFor(dataset);
  bool changed = false;
  if (next.guard == GUARD_ACTIVE) {
    next.guard = GUARD_QUIET;
    changed = true;
  }
  uint32_t now = modClockGet();
  if (guardExpired(next, now)) {
    clearGuard(next);
    changed = true;
  }
  if (next.warning_reason != 0 && clockSane(now) && now >= next.warning_expiry) {
    next.warning_generation = 0;
    next.warning_time = 0;
    next.warning_expiry = 0;
    next.abort_sent = 0;
    next.warning_reason = 0;
    changed = true;
  }
  if (!changed) return true;
  if (!saveState(statePaths(dataset), stateType(dataset), next, scratch)) return false;
  stateFor(dataset) = next;
  return true;
}

static void boot() {
  Temp scratch(STORE_MAX);
  config_ready = loadConfig(scratch);
#if SYNC_SETTINGS_WITH_REGION
  overlay_ready = loadRegions(scratch);
#endif
#if SYNC_SETTINGS_WITH_REGION
  region_state_ready =
      loadState(REGION_STATE_PATHS, STORE_REGION_STATE, region_state, scratch);
#endif
#if SYNC_SETTINGS_WITH_POLICY
  policy_state_ready =
      loadState(POLICY_STATE_PATHS, STORE_POLICY_STATE, policy_state, scratch);
#endif
#if SYNC_SETTINGS_WITH_POLICY
  policy_recovery_ready = loadRecovery(scratch);
#endif
  trust_ready = loadPublishers(scratch);
#if SYNC_SETTINGS_WITH_POLICY
  if (trust_ready && policy_recovery_ready &&
      publishers.pending_forget != 0 &&
      policy_recovery.phase != RECOVERY_IDLE) {
    trust_ready = false;
    policy_recovery_ready = false;
  } else if (trust_ready && publishers.pending_forget != 0) {
    trust_ready = finishForget(scratch);
  }
  if (policy_state_ready && policy_recovery_ready &&
      !recoverPolicy(scratch)) policy_recovery_ready = false;
#else
  if (trust_ready && publishers.pending_forget != 0) {
    trust_ready = finishForget(scratch);
  }
#endif
#if SYNC_SETTINGS_WITH_REGION
  if (overlay_ready && region_state_ready && !settleRegionReceipt(scratch)) {
    overlay_ready = false;
    region_state_ready = false;
  }
  if (region_state_ready && !settleGuard(REGION, scratch)) region_state_ready = false;
#endif
#if SYNC_SETTINGS_WITH_POLICY
  if (policy_state_ready && !settleGuard(POLICY, scratch)) policy_state_ready = false;
#endif
  ready = config_ready && trust_ready
#if SYNC_SETTINGS_WITH_REGION
          && overlay_ready && region_state_ready
#endif
#if SYNC_SETTINGS_WITH_POLICY
          && policy_state_ready && policy_recovery_ready
#endif
          ;
#if SYNC_SETTINGS_WITH_REGION
  enabled = overlay_ready && region_state_ready && region_state.enabled;
#endif
  booted = true;
}

static bool setEnabled(bool region, bool value, bool defer_off) {
#if !SYNC_SETTINGS_WITH_REGION
  if (region) return false;
#endif
#if !SYNC_SETTINGS_WITH_POLICY
  if (!region) return false;
#endif
  bool state_ready = region ? region_state_ready
                            : policy_state_ready
#if SYNC_SETTINGS_WITH_POLICY
                              && policy_recovery_ready
#endif
                              ;
  if (!state_ready ||
      (value && (!config_ready || !trust_ready || config.channel_len == 0 ||
#if SYNC_SETTINGS_WITH_REGION
                 (region && !overlay_ready) ||
#endif
#if SYNC_SETTINGS_WITH_POLICY
                 (!region && policy_recovery.phase != RECOVERY_IDLE)))) {
#else
                 false))) {
#endif
    return false;
  }
  Temp scratch(STORE_MAX);
  uint8_t dataset = region ? REGION : POLICY;
  DatasetState next = stateFor(dataset);
  next.enabled = value;
  PairPaths& paths = region ? REGION_STATE_PATHS : POLICY_STATE_PATHS;
  uint8_t type = region ? STORE_REGION_STATE : STORE_POLICY_STATE;
  if (!saveState(paths, type, next, scratch)) return false;
  if (region) {
#if SYNC_SETTINGS_WITH_REGION
    region_state = next;
    if (!value && defer_off) disable_pending = true;
    else enabled = value;
#endif
  } else {
#if SYNC_SETTINGS_WITH_POLICY
    policy_state = next;
#endif
  }
  if (!value) receiver.cancel(region ? REGION : POLICY);
  return true;
}

static bool routeName(const char* input, char out[REGION_NAME_MAX + 1]) {
  if (input == nullptr || *input == 0) return false;
  if (strcmp(input, "*") == 0) {
    strcpy(out, "*");
    return true;
  }
  if (*input == '#') ++input;
  size_t len = strlen(input);
  if (len == 0 || len > REGION_NAME_MAX || input[0] == '$' || input[0] == '*') {
    return false;
  }
  for (size_t i = 0; i < len; ++i) {
    if (!RegionMap::is_name_char((uint8_t)input[i])) return false;
  }
  memcpy(out, input, len + 1);
  return true;
}

static bool publishRoute(const char* input, bool& scoped, uint8_t key[16]) {
  char name[REGION_NAME_MAX + 1];
  if (!routeName(input, name)) return false;
  ModRegionMatch base = {};
  if (!modResolveRegionName(name, &base)) return false;
  if (name[0] == '*') {
    scoped = false;
    memset(key, 0, 16);
    return base.state == MOD_REGION_ALLOW;
  }

  ScopeState overlay = SCOPE_NONE;
  int8_t position = -1;
#if SYNC_SETTINGS_WITH_REGION
  if (enabled) {
    position = regions.find((const uint8_t*)name, (uint8_t)strlen(name));
    if (position >= 0) {
      RegionView entry;
      if (!regions.get((uint8_t)position, entry)) return false;
      overlay = entry.denied ? SCOPE_DENY : SCOPE_ALLOW;
    }
  }
#endif
  if (base.state == MOD_REGION_DENY || overlay == SCOPE_DENY) return false;
  scoped = true;
  if (overlay == SCOPE_ALLOW) {
#if SYNC_SETTINGS_WITH_REGION
    memcpy(key, regions.key((uint8_t)position), 16);
    return true;
#else
    return false;
#endif
  }
  if (base.state != MOD_REGION_ALLOW) return false;
  memcpy(key, base.key, 16);
  return true;
}

static bool publish(uint8_t dataset, const char* route, const char* channel,
                    bool reset, bool empty, char* reply) {
  DatasetState& current = stateFor(dataset);
  bool state_ready = dataset == REGION ? region_state_ready
                                       : policy_state_ready
#if SYNC_SETTINGS_WITH_POLICY
                                         && policy_recovery_ready &&
                                            policy_recovery.phase == RECOVERY_IDLE
#endif
                                         ;
  if (!state_ready) {
    strcpy(reply, "Err - storage");
    return true;
  }
  if (transmitter().active(dataset) || transmitter().aborting(dataset) ||
      receiver.campaign(dataset).state != RECEIVE_IDLE) {
    strcpy(reply, "Err - busy");
    return true;
  }
  size_t channel_len = channel == nullptr ? 0 : strlen(channel);
  if (channel_len == 0 || channel_len > CHANNEL_MAX ||
      !validChannel((const uint8_t*)channel, (uint8_t)channel_len)) {
    strcpy(reply, "Err - invalid channel");
    return true;
  }
  bool scoped;
  uint8_t route_key[16];
  if (!publishRoute(route, scoped, route_key)) {
    strcpy(reply, "Err - invalid route");
    return true;
  }
  uint32_t now = modClockGet();
  if (!clockSane(now)) {
    strcpy(reply, "Err - clock");
    return true;
  }
  if (guardExpired(current, now)) {
    DatasetState expired = current;
    clearGuard(expired);
    Temp scratch(STORE_MAX);
    if (!saveState(statePaths(dataset), stateType(dataset), expired, scratch)) {
      strcpy(reply, "Err - storage");
      return true;
    }
    current = expired;
  }
  if (current.guard != GUARD_IDLE) {
    strcpy(reply, "Err - locked");
    return true;
  }

  uint16_t data_len = POLICY_DATA_LEN;
  bool is_empty = false;
#if SYNC_SETTINGS_WITH_REGION
  if (dataset == REGION) {
    data_len = regions.size();
    is_empty = regions.count() == 0;
  }
#endif
  if (dataset == REGION && is_empty != empty) {
    strcpy(reply, is_empty ? "Err - overlay empty; repeat with -empty"
                           : "Err - -empty requires an empty overlay");
    return true;
  }
  uint8_t* data = static_cast<uint8_t*>(malloc(data_len));
  if (data == nullptr) {
    strcpy(reply, "Err - memory");
    return true;
  }
  bool captured = false;
#if SYNC_SETTINGS_WITH_REGION
  if (dataset == REGION) {
    memcpy(data, regions.data(), data_len);
    captured = true;
  }
#endif
#if SYNC_SETTINGS_WITH_POLICY
  if (dataset == POLICY) captured = readNativePolicy(data);
#endif
  if (!captured || !validateDataset(dataset, data, data_len, nullptr)) {
    free(data);
    strcpy(reply, "Err - invalid dataset");
    return true;
  }

  uint8_t publisher[32];
  if (!modPublisherKey(publisher)) {
    free(data);
    strcpy(reply, "Err - identity");
    return true;
  }
  uint32_t generation = now;
  if (current.local_generation != 0 &&
      memcmp(current.local_key, publisher, sizeof(publisher)) == 0) {
    if (current.local_generation == 0xffffffffu) {
      free(data);
      strcpy(reply, "Err - generation exhausted");
      return true;
    }
    uint32_t next = current.local_generation + 1;
    if (next > generation) generation = next;
  }

  Manifest manifest = {};
  manifest.dataset = dataset;
  manifest.format = 1;
  manifest.reset = reset;
  manifest.days = current.duration_days;
  manifest.generation = generation;
  memcpy(manifest.channel, channel, channel_len + 1);
  manifest.data_len = data_len;
  manifest.chunks = chunkCount(data_len);
  uint8_t fingerprint[DIGEST_LEN];
  hash(publisher, sizeof(publisher), fingerprint, nullptr);
  memcpy(manifest.publisher, fingerprint, sizeof(manifest.publisher));
  hash(data, data_len, manifest.digest, nullptr);
  TxStart start = {};
  start.dataset = dataset;
  start.generation = generation;
  start.interval_hours = current.interval_hours;
  start.duration_days = current.duration_days;
  start.scoped = scoped;
  memcpy(start.key, route_key, sizeof(start.key));
  size_t signed_len = writeManifestPrefix(manifest, start.manifest,
                                          sizeof(start.manifest));
  if (signed_len != MANIFEST_SIGNED_LEN ||
      !modSignDetached(start.manifest, signed_len,
                       start.manifest + signed_len)) {
    free(data);
    strcpy(reply, "Err - signing");
    return true;
  }
  start.data = data;
  start.data_len = data_len;
  if (!transmitter().begin(start, millis())) {
    free(data);
    strcpy(reply, "Err - busy");
    return true;
  }

  DatasetState next = current;
  next.local_generation = generation;
  memcpy(next.local_key, publisher, sizeof(next.local_key));
  next.guard = GUARD_ACTIVE;
  next.guard_generation = generation;
  next.campaign_start = now;
  next.guard_days = current.duration_days;
  next.route_kind = scoped ? 1 : 0;
  memset(next.route_key, 0, sizeof(next.route_key));
  if (scoped) memcpy(next.route_key, route_key, sizeof(next.route_key));
  hash(start.manifest, sizeof(start.manifest), next.manifest_hash, nullptr);
  Temp scratch(STORE_MAX);
  if (!saveState(statePaths(dataset), stateType(dataset), next, scratch)) {
    transmitter().discard(dataset);
    strcpy(reply, "Err - storage");
    return true;
  }
  current = next;
  strcpy(reply, reset ? "OK - older signed generations may be accepted" : "OK");
  return true;
}

static bool parseOffset(const char* text, uint8_t& out) {
  if (text == nullptr || *text == 0) {
    out = 0;
    return true;
  }
  char* end;
  unsigned long value = strtoul(text, &end, 10);
  if (*end != 0 || value > 255) return false;
  out = (uint8_t)value;
  return true;
}

static bool parsePublicKey(const char* text, uint8_t out[32]) {
  if (text == nullptr || strlen(text) != 64 ||
      !mesh::Utils::fromHex(out, 32, text)) return false;
  for (uint8_t i = 0; i < 32; ++i) {
    if (out[i] != 0) return true;
  }
  return false;
}

static void listPublishers(const char* arg, char* reply) {
  uint8_t offset;
  if (!parseOffset(arg, offset)) {
    strcpy(reply, "Err - syntax: sync.publisher list [offset]");
    return;
  }
  uint8_t active = 0;
  for (uint8_t i = 0; i < publishers.count; ++i) {
    if (publishers.record[i].status == PUBLISHER_ACTIVE) ++active;
  }
  if (offset > active) {
    strcpy(reply, "Err - syntax: sync.publisher list [offset]");
    return;
  }

  size_t used = 0;
  uint8_t seen = 0;
  reply[0] = 0;
  for (uint8_t i = 0; i < publishers.count; ++i) {
    const Publisher& record = publishers.record[i];
    if (record.status != PUBLISHER_ACTIVE) continue;
    if (seen++ < offset) continue;
    if (used + (used ? 1 : 0) + 64 >= 160) break;
    if (used) reply[used++] = ' ';
    mesh::Utils::toHex(reply + used, record.key, sizeof(record.key));
    used += 64;
  }
  if (used == 0) strcpy(reply, "empty");
}

static bool publisherCommand(char* command, char* reply) {
  if (strcmp(command, "sync.publisher list") == 0 ||
      strncmp(command, "sync.publisher list ", 20) == 0) {
    if (!trust_ready) strcpy(reply, "Err - storage");
    else listPublishers(command[19] == ' ' ? command + 20 : nullptr, reply);
    return true;
  }

  static const char add[] = "sync.publisher add";
  static const char remove[] = "sync.publisher remove";
  static const char forget[] = "sync.publisher forget";
  const char* verb = nullptr;
  enum { PUBLISHER_ADD, PUBLISHER_REMOVE, PUBLISHER_FORGET } action;
  if (strncmp(command, add, sizeof(add) - 1) == 0) {
    verb = add;
    action = PUBLISHER_ADD;
  } else if (strncmp(command, remove, sizeof(remove) - 1) == 0) {
    verb = remove;
    action = PUBLISHER_REMOVE;
  } else if (strncmp(command, forget, sizeof(forget) - 1) == 0) {
    verb = forget;
    action = PUBLISHER_FORGET;
  } else {
    return false;
  }

  size_t prefix = strlen(verb);
  uint8_t key[32];
  if (command[prefix] != ' ' || strchr(command + prefix + 1, ' ') != nullptr ||
      !parsePublicKey(command + prefix + 1, key)) {
    snprintf(reply, 160, "Err - syntax: %s <full-public-key>", verb);
    return true;
  }
  if (!trust_ready || publishers.pending_forget != 0 ||
#if SYNC_SETTINGS_WITH_POLICY
      (action == PUBLISHER_FORGET &&
       policy_recovery.phase != RECOVERY_IDLE)
#else
      false
#endif
      ) {
    strcpy(reply, "Err - storage");
    return true;
  }

  Publishers next = publishers;
  TrustResult result;
  if (action == PUBLISHER_ADD) {
    result = authorizePublisher(next, key);
  } else {
    if (action == PUBLISHER_REMOVE) {
      result = removePublisher(next, key);
    } else {
      result = markPublisherForget(next, key);
    }
  }

  if (result != TRUST_OK) {
    if (result == TRUST_EXISTS) strcpy(reply, "Err - already authorized");
    else if (result == TRUST_NOT_FOUND) strcpy(reply, "Err - publisher not found");
    else if (result == TRUST_CAPACITY) strcpy(reply, "Err - publisher table full");
    else if (action == PUBLISHER_REMOVE) strcpy(reply, "Err - publisher already removed");
    else strcpy(reply, "Err - remove publisher first");
    return true;
  }

  Temp scratch(STORE_MAX);
  if (!savePublishers(next, scratch)) {
    strcpy(reply, "Err - storage");
    return true;
  }
  publishers = next;
  if (action == PUBLISHER_REMOVE) receiver.cancelPublisher(key);
  if (action == PUBLISHER_FORGET && !finishForget(scratch)) {
    trust_ready = false;
    strcpy(reply, "Err - storage");
    return true;
  }
  strcpy(reply, "OK");
  return true;
}

#if SYNC_SETTINGS_WITH_REGION
static void listRegions(const char* arg, char* reply) {
  uint8_t offset;
  if (!parseOffset(arg, offset) || offset > regions.count()) {
    strcpy(reply, "Err - syntax: sync.region list [offset]");
    return;
  }
  size_t used = 0;
  reply[0] = 0;
  for (uint8_t i = offset; i < regions.count(); ++i) {
    RegionView entry;
    regions.get(i, entry);
    RegionView parent;
    int n = entry.parent == 0
          ? snprintf(reply + used, 160 - used, "%s%.*s%s", used ? " " : "",
                     entry.name_len, (const char*)entry.name, entry.denied ? "" : " F")
          : (regions.get((uint8_t)(entry.parent - 1), parent),
             snprintf(reply + used, 160 - used, "%s%.*s (%.*s)%s", used ? " " : "",
                      entry.name_len, (const char*)entry.name,
                      parent.name_len, (const char*)parent.name,
                      entry.denied ? "" : " F"));
    if (n < 0 || (size_t)n >= 160 - used) break;
    used += (size_t)n;
  }
  if (used == 0) strcpy(reply, "empty");
}
#endif

static bool setChannel(const char* value, char* reply) {
  size_t len = value == nullptr ? 0 : strlen(value);
  if (len == 0 || len > CHANNEL_MAX || strchr(value, ' ') != nullptr ||
      !validChannel((const uint8_t*)value, (uint8_t)len)) {
    strcpy(reply, "Err - syntax: set sync.channel <channel>");
    return true;
  }
  if (!config_ready || !trust_ready ||
#if SYNC_SETTINGS_WITH_REGION
      !region_state_ready ||
#endif
#if SYNC_SETTINGS_WITH_POLICY
      !policy_state_ready || !policy_recovery_ready ||
#endif
      false) {
    strcpy(reply, "Err - storage");
    return true;
  }
  if (
#if SYNC_SETTINGS_WITH_REGION
      region_state.enabled ||
#endif
#if SYNC_SETTINGS_WITH_POLICY
      policy_state.enabled ||
#endif
      false) {
    strcpy(reply, "Err - sync must be off");
    return true;
  }
  if (
#if SYNC_SETTINGS_WITH_REGION
      receiver.campaign(REGION).state != RECEIVE_IDLE ||
#endif
#if SYNC_SETTINGS_WITH_POLICY
      receiver.campaign(POLICY).state != RECEIVE_IDLE ||
      policy_recovery.phase != RECOVERY_IDLE) {
#else
      false) {
#endif
    strcpy(reply, "Err - busy");
    return true;
  }
  Config next = {};
  next.channel_len = (uint8_t)len;
  memcpy(next.channel, value, len);
  Temp scratch(STORE_MAX);
  if (!config_ready || !saveConfig(next, scratch)) strcpy(reply, "Err - storage");
  else {
    config = next;
    strcpy(reply, "OK");
  }
  return true;
}

static bool parseUnit(const char* value, char suffix, uint8_t low, uint8_t high,
                      uint8_t& out) {
  if (value == nullptr || *value == 0) return false;
  char* end;
  unsigned long number = strtoul(value, &end, 10);
  if (end == value || *end != suffix || end[1] != 0 || number < low || number > high) {
    return false;
  }
  out = (uint8_t)number;
  return true;
}

static bool setSchedule(bool region, bool interval, const char* value, char* reply) {
  uint8_t parsed;
  uint8_t low = interval ? 3 : 1;
  uint8_t high = interval ? 24 : 4;
  char suffix = interval ? 'h' : 'd';
  const char* dataset = region ? "region" : "policy";
  const char* field = interval ? "interval" : "duration";
  if (!parseUnit(value, suffix, low, high, parsed)) {
    snprintf(reply, 160, "Err - syntax: set sync.%s.publish.%s <%u-%u>%c",
             dataset, field, low, high, suffix);
    return true;
  }

  uint8_t selected = region ? REGION : POLICY;
  DatasetState next = stateFor(selected);
  if (interval) next.interval_hours = parsed;
  else next.duration_days = parsed;
  Temp scratch(STORE_MAX);
  PairPaths& paths = region ? REGION_STATE_PATHS : POLICY_STATE_PATHS;
  uint8_t type = region ? STORE_REGION_STATE : STORE_POLICY_STATE;
  bool state_ready = region ? region_state_ready : policy_state_ready;
  if (!state_ready || !saveState(paths, type, next, scratch)) strcpy(reply, "Err - storage");
  else {
    stateFor(selected) = next;
    strcpy(reply, "OK");
  }
  return true;
}

static int words(char* text, char* out[], int capacity) {
  int count = 0;
  while (text != nullptr && *text != 0) {
    while (*text == ' ') ++text;
    if (*text == 0) break;
    if (count == capacity) return capacity + 1;
    out[count++] = text;
    while (*text != 0 && *text != ' ') ++text;
    if (*text != 0) *text++ = 0;
  }
  return count;
}

#if SYNC_SETTINGS_WITH_REGION
static RegionResult buildEdit(EditKind kind, const char* name,
                              uint8_t* out, uint16_t& len) {
  if (kind == EDIT_DENY) {
    return setRegionFlood(regions, name, false, out, REGION_DATA_MAX, len);
  }
  if (kind == EDIT_REMOVE) {
    return removeRegion(regions, name, out, REGION_DATA_MAX, len);
  }
  if (kind == EDIT_CLEAR) {
    out[0] = 0;
    len = 1;
    return REGION_OK;
  }
  return REGION_MALFORMED;
}

static bool applyEdit(EditKind kind, const char* name) {
  if (kind == EDIT_RELOAD) {
    Temp scratch(STORE_MAX);
    if (!loadRegions(scratch)) return false;
    dirty = false;
    return true;
  }
  Temp candidate(REGION_DATA_MAX);
  if (!candidate) return false;
  uint16_t len;
  if (buildEdit(kind, name, candidate, len) != REGION_OK ||
      !installRegions(candidate, len)) return false;
  dirty = true;
  return true;
}

static bool deferEdit(EditKind kind, const char* name) {
  if (pending_edit.kind != EDIT_NONE) return false;
  pending_edit.kind = kind;
  pending_edit.name[0] = 0;
  if (name != nullptr) {
    size_t len = strnlen(name, REGION_NAME_MAX + 1);
    if (len > REGION_NAME_MAX) return false;
    memcpy(pending_edit.name, name, len + 1);
  }
  return true;
}

static void editError(RegionResult result, const char* syntax, char* reply) {
  if (result == REGION_CAPACITY) strcpy(reply, "Err - region table full");
  else if (result == REGION_HIERARCHY) strcpy(reply, "Err - invalid hierarchy");
  else if (result == REGION_NAME) strcpy(reply, "Err - unknown or invalid region");
  else snprintf(reply, 160, "Err - syntax: %s", syntax);
}

static bool regionEditCommand(const ModCliContext& context, char* command,
                              char* reply) {
  static const char put[] = "sync.region put";
  static const char allow[] = "sync.region allowf";
  static const char deny[] = "sync.region denyf";
  static const char remove[] = "sync.region remove";
  const char* verb = nullptr;
  enum { COMMAND_PUT, COMMAND_ALLOW, COMMAND_DENY, COMMAND_REMOVE } selected;
  if (strncmp(command, put, sizeof(put) - 1) == 0) {
    verb = put;
    selected = COMMAND_PUT;
  } else if (strncmp(command, allow, sizeof(allow) - 1) == 0) {
    verb = allow;
    selected = COMMAND_ALLOW;
  } else if (strncmp(command, deny, sizeof(deny) - 1) == 0) {
    verb = deny;
    selected = COMMAND_DENY;
  } else if (strncmp(command, remove, sizeof(remove) - 1) == 0) {
    verb = remove;
    selected = COMMAND_REMOVE;
  } else {
    return false;
  }

  if (!overlay_ready) {
    strcpy(reply, "Err - storage");
    return true;
  }
  if (pending_edit.kind != EDIT_NONE) {
    strcpy(reply, "Err - busy");
    return true;
  }

  size_t prefix = strlen(verb);
  if (command[prefix] != ' ') {
    snprintf(reply, 160, "Err - syntax: %s <name>%s", verb,
             selected == COMMAND_PUT ? " [<parent>]" : "");
    return true;
  }
  char* part[2];
  int count = words(command + prefix + 1, part, 2);
  int wanted = selected == COMMAND_PUT ? 2 : 1;
  if (count == 0 || count > wanted) {
    snprintf(reply, 160, "Err - syntax: %s <name>%s", verb,
             selected == COMMAND_PUT ? " [<parent>]" : "");
    return true;
  }

  Temp candidate(REGION_DATA_MAX);
  if (!candidate) {
    strcpy(reply, "Err - memory");
    return true;
  }
  uint16_t len;
  RegionResult result;
  EditKind kind = EDIT_NONE;
  if (selected == COMMAND_PUT) {
    result = putRegion(regions, part[0], count == 2 ? part[1] : nullptr,
                       candidate, REGION_DATA_MAX, len);
  } else if (selected == COMMAND_ALLOW) {
    result = setRegionFlood(regions, part[0], true, candidate,
                            REGION_DATA_MAX, len);
  } else {
    kind = selected == COMMAND_DENY ? EDIT_DENY : EDIT_REMOVE;
    result = buildEdit(kind, part[0], candidate, len);
  }
  if (result != REGION_OK) {
    editError(result, verb, reply);
    return true;
  }

  bool remote_destructive = context.sender_timestamp != 0 && kind != EDIT_NONE;
  if (remote_destructive) {
    if (!deferEdit(kind, part[0])) strcpy(reply, "Err - busy");
    else strcpy(reply, "OK");
  } else if (!installRegions(candidate, len)) {
    strcpy(reply, "Err - invalid overlay");
  } else {
    dirty = true;
    strcpy(reply, "OK");
  }
  return true;
}
#endif

static bool scheduleCommand(char* command, char* reply) {
  struct Entry {
    const char* get;
    const char* set;
    bool region;
    bool interval;
  };
  static const Entry entries[] = {
#if SYNC_SETTINGS_WITH_REGION
    {"get sync.region.publish.interval", "set sync.region.publish.interval", true, true},
    {"get sync.region.publish.duration", "set sync.region.publish.duration", true, false},
#endif
#if SYNC_SETTINGS_WITH_POLICY
    {"get sync.policy.publish.interval", "set sync.policy.publish.interval", false, true},
    {"get sync.policy.publish.duration", "set sync.policy.publish.duration", false, false},
#endif
  };
  for (const Entry& entry : entries) {
    if (strcmp(command, entry.get) == 0) {
      if (entry.region ? !region_state_ready : !policy_state_ready) {
        strcpy(reply, "Err - storage");
        return true;
      }
      const DatasetState& state = stateFor(entry.region ? REGION : POLICY);
      snprintf(reply, 160, "%u%c",
               entry.interval ? state.interval_hours : state.duration_days,
               entry.interval ? 'h' : 'd');
      return true;
    }
    size_t len = strlen(entry.set);
    if (strncmp(command, entry.set, len) == 0) {
      const char* value = command[len] == ' ' ? command + len + 1 : nullptr;
      return setSchedule(entry.region, entry.interval, value, reply);
    }
  }
  return false;
}

static bool parseOffset(const char* text, uint8_t& out);

static bool append(char* reply, size_t& at, const char* format, ...) {
  if (at >= 160) return false;
  va_list args;
  va_start(args, format);
  int length = vsnprintf(reply + at, 160 - at, format, args);
  va_end(args);
  if (length < 0 || (size_t)length >= 160 - at) return false;
  at += (size_t)length;
  return true;
}

static bool expireRuntime(uint8_t dataset) {
  DatasetState next = stateFor(dataset);
  uint32_t now = modClockGet();
  bool changed = false;
  if (guardExpired(next, now)) {
    clearGuard(next);
    changed = true;
  }
  if (next.warning_reason != 0 && clockSane(now) &&
      now >= next.warning_expiry) {
    next.warning_generation = 0;
    next.warning_time = 0;
    next.warning_expiry = 0;
    next.abort_sent = 0;
    next.warning_reason = 0;
    changed = true;
  }
  if (!changed) return true;
  Temp scratch(STORE_MAX);
  if (!saveState(statePaths(dataset), stateType(dataset), next, scratch)) return false;
  stateFor(dataset) = next;
  return true;
}

static uint8_t bitCount(uint16_t value) {
  uint8_t count = 0;
  while (value != 0) {
    count += (uint8_t)(value & 1u);
    value >>= 1;
  }
  return count;
}

static bool routeLabel(bool scoped, const uint8_t key[16],
                       char out[REGION_NAME_MAX + 1]) {
  if (!scoped) {
    strcpy(out, "*");
    return true;
  }
#if SYNC_SETTINGS_WITH_REGION
  for (uint8_t i = 0; i < regions.count(); ++i) {
    if (memcmp(regions.key(i), key, 16) != 0) continue;
    RegionView entry;
    if (!regions.get(i, entry)) return false;
    memcpy(out, entry.name, entry.name_len);
    out[entry.name_len] = 0;
    return true;
  }
#endif
  return modRegionNameForKey(key, out, REGION_NAME_MAX + 1);
}

static void status(uint8_t dataset, char* reply) {
  bool region = dataset == REGION;
  bool state_ready = region ? overlay_ready && region_state_ready
                            : policy_state_ready
#if SYNC_SETTINGS_WITH_POLICY
                              && policy_recovery_ready
#endif
                              ;
  bool degraded = config_degraded || trust_degraded ||
                  (region ? overlay_degraded || region_state_degraded
                          : policy_state_degraded
#if SYNC_SETTINGS_WITH_POLICY
                            || recovery_degraded
#endif
                  );
  if (!state_ready || !expireRuntime(dataset)) {
    strcpy(reply, "fault storage");
    return;
  }
  const DatasetState& state = stateFor(dataset);
  const Campaign& inbound = receiver.campaign(dataset);
  const TxState* outbound = transmitter().state(dataset);
  const char* channel = !config_ready ? "fault" :
                        (config.channel_len ? config.channel : "unset");
  char route[REGION_NAME_MAX + 1];
  bool has_route = false;
  size_t at = 0;
  reply[0] = 0;
  if (!append(reply, at, "%s", state.enabled ? "on" : "off")) goto overflow;
  if (region && !append(reply, at, " %s%s", enabled ? "active" : "inactive",
                        dirty ? " dirty" : "")) goto overflow;

  if (outbound != nullptr && outbound->aborting) {
    if (!append(reply, at, " aborting notices %u/2", outbound->abort_sent)) goto overflow;
  } else if (outbound != nullptr && outbound->active) {
    if (!append(reply, at, " publishing gen %lu round %u/%u",
                (unsigned long)outbound->generation, outbound->rounds_done,
                outbound->rounds)) goto overflow;
  } else if (inbound.state != RECEIVE_IDLE) {
    uint8_t fingerprint[32];
    hash(inbound.publisher_key, sizeof(inbound.publisher_key), fingerprint, nullptr);
    if (!append(reply, at, inbound.state == RECEIVE_STAGED
                           ? " staged %02x%02x%02x%02x%02x%02x%02x%02x gen %lu"
                           : " receiving %02x%02x%02x%02x%02x%02x%02x%02x gen %lu chunks %u/%u",
                fingerprint[0], fingerprint[1], fingerprint[2], fingerprint[3],
                fingerprint[4], fingerprint[5], fingerprint[6], fingerprint[7],
                (unsigned long)inbound.generation,
                bitCount(inbound.received), inbound.chunks)) goto overflow;
    if (inbound.state == RECEIVE_ACTIVE) {
      int32_t left = (int32_t)(inbound.deadline - millis());
      if (left > 0 && !append(reply, at, " timeout %lus",
                              (unsigned long)((uint32_t)left / 1000u))) goto overflow;
    }
#if SYNC_SETTINGS_WITH_POLICY
  } else if (!region && policy_recovery.phase != RECOVERY_IDLE) {
    if (!append(reply, at, " recovering")) goto overflow;
#endif
  } else if (state.guard != GUARD_IDLE) {
    if (!append(reply, at, " quiet gen %lu", (unsigned long)state.guard_generation)) {
      goto overflow;
    }
  } else if (!append(reply, at, " idle")) goto overflow;

  if (state.guard != GUARD_IDLE && clockSane(modClockGet())) {
    uint64_t end = (uint64_t)state.campaign_start +
                   (uint64_t)state.guard_days * 86400u;
    uint32_t now = modClockGet();
    if (end > now && !append(reply, at, " lock %lus",
                             (unsigned long)(end - now))) goto overflow;
  }
  if (outbound != nullptr) {
    has_route = routeLabel(outbound->scoped, outbound->key, route);
  } else if (inbound.state != RECEIVE_IDLE) {
    has_route = routeLabel(inbound.scoped, inbound.route_key, route);
  } else if (state.guard != GUARD_IDLE) {
    has_route = routeLabel(state.route_kind != 0, state.route_key, route);
  }
  if (has_route && !append(reply, at, " region %s", route)) goto overflow;
  if (!append(reply, at, " channel %s", channel)) goto overflow;
  if (state.enabled && (!config_ready || !trust_ready) &&
      !append(reply, at, " receive fault storage")) goto overflow;
  if (state.enabled && config_ready && trust_ready && config.channel_len == 0 &&
      !append(reply, at, " receive unavailable")) goto overflow;
  if (degraded && !append(reply, at, " storage degraded")) goto overflow;
  if (state.warning_reason != 0 && !append(reply, at, " reports:1")) goto overflow;

  if (!region && outbound == nullptr && inbound.state == RECEIVE_IDLE &&
#if SYNC_SETTINGS_WITH_POLICY
      policy_recovery.phase == RECOVERY_IDLE) {
#else
      false) {
#endif
#if SYNC_SETTINGS_WITH_POLICY
    ModPolicyValues value;
    if (modPolicyRead(&value) &&
        !append(reply, at,
                " policy %u/%u/%u adv %u/%u path %u loop %u ack %u af %.6g tx %.6g agc %u",
                value.flood_max, value.flood_max_unscoped,
                value.flood_max_advert, value.advert_interval,
                value.flood_advert_interval, value.path_hash_mode,
                value.loop_detect, value.multi_acks,
                (double)value.airtime_factor, (double)value.tx_delay_factor,
                value.agc_reset_interval)) goto overflow;
#endif
  }
  return;

overflow:
  strcpy(reply, "Err - status too long");
}

static bool reportCommand(uint8_t dataset, const char* page_text, char* reply) {
  uint8_t page;
  if (!parseOffset(page_text, page) || page == 0) {
    strcpy(reply, "Err - page");
    return true;
  }
  bool state_ready = dataset == REGION ? region_state_ready
                                       : policy_state_ready
#if SYNC_SETTINGS_WITH_POLICY
                                         && policy_recovery_ready
#endif
                                         ;
  if (!state_ready || !expireRuntime(dataset)) {
    strcpy(reply, "Err - storage");
    return true;
  }
  const DatasetState& state = stateFor(dataset);
  if (state.warning_reason == 0) strcpy(reply, "Err - no reports");
  else if (page != 1) strcpy(reply, "Err - page range");
  else snprintf(reply, 160,
                "1/1 abort gen %lu notices %u reason deadline at %lu until %lu",
                (unsigned long)state.warning_generation, state.abort_sent,
                (unsigned long)state.warning_time,
                (unsigned long)state.warning_expiry);
  return true;
}

static bool publishCommand(char* command, char* reply) {
  struct Prefix {
    const char* text;
    uint8_t dataset;
    bool reset;
  };
  static const Prefix prefix[] = {
#if SYNC_SETTINGS_WITH_REGION
    {"sync.region publish.reset", REGION, true},
    {"sync.region publish", REGION, false},
#endif
#if SYNC_SETTINGS_WITH_POLICY
    {"sync.policy publish.reset", POLICY, true},
    {"sync.policy publish", POLICY, false},
#endif
  };
  for (const Prefix& entry : prefix) {
    size_t len = strlen(entry.text);
    if (strncmp(command, entry.text, len) != 0 || command[len] != ' ') continue;
    char* part[3];
    int count = words(command + len + 1, part, 3);
    bool empty = count == 3 && strcmp(part[2], "-empty") == 0;
    bool valid = entry.dataset == REGION ? (count == 2 || empty) : count == 2;
    if (!valid) {
      snprintf(reply, 160, "Err - syntax: %s <region|*> <channel>%s",
               entry.text, entry.dataset == REGION ? " [-empty]" : "");
      return true;
    }
    return publish(entry.dataset, part[0], part[1], entry.reset, empty, reply);
  }
  for (const Prefix& entry : prefix) {
    if (strcmp(command, entry.text) != 0) continue;
    snprintf(reply, 160, "Err - syntax: %s <region|*> <channel>%s",
             entry.text, entry.dataset == REGION ? " [-empty]" : "");
    return true;
  }
  return false;
}

static bool abortCommand(uint8_t dataset, char* reply) {
  if (transmitter().aborting(dataset)) {
    strcpy(reply, "Err - busy");
    return true;
  }
  if (transmitter().active(dataset)) {
    DatasetState& state = stateFor(dataset);
    if (!transmitter().abort(dataset, state.manifest_hash,
                             state.route_kind != 0, state.route_key, millis())) {
      strcpy(reply, "Err - abort");
    } else {
      strcpy(reply, "OK - aborting");
    }
    return true;
  }
  DatasetState& state = stateFor(dataset);
  bool guarded = state.guard != GUARD_IDLE;
  if (guarded) {
    DatasetState next = state;
    clearGuard(next);
    Temp scratch(STORE_MAX);
    if (!saveState(statePaths(dataset), stateType(dataset), next, scratch)) {
      strcpy(reply, "Err - storage");
      return true;
    }
    state = next;
  }
  ReceiveResult result = receiver.cancel(dataset);
  strcpy(reply, guarded || result == RECEIVE_OK ? "OK" : "Err - no campaign");
  return true;
}

static bool handleCli(const ModCliContext& context, char* command, char* reply) {
#if !SYNC_SETTINGS_WITH_REGION
  (void)context;
#endif
  if (!booted) boot();
  if (publisherCommand(command, reply)) return true;
  if (publishCommand(command, reply)) return true;
  if (strcmp(command, "get sync.channel") == 0) {
    if (!config_ready) strcpy(reply, "Err - storage");
    else if (config.channel_len == 0) strcpy(reply, "unset");
    else memcpy(reply, config.channel, config.channel_len + 1);
    return true;
  }
  if (strncmp(command, "set sync.channel", 16) == 0) {
    return setChannel(command[16] == ' ' ? command + 17 : nullptr, reply);
  }
#if SYNC_SETTINGS_WITH_REGION
  if (strcmp(command, "sync.region on") == 0 ||
      strcmp(command, "sync.region off") == 0) {
    bool on = command[12] == 'o' && command[13] == 'n';
    if (!setEnabled(true, on, !on)) strcpy(reply, "Err - storage or channel");
    else strcpy(reply, "OK");
    return true;
  }
#endif
#if SYNC_SETTINGS_WITH_POLICY
  if (strcmp(command, "sync.policy on") == 0 ||
      strcmp(command, "sync.policy off") == 0) {
    bool on = command[12] == 'o' && command[13] == 'n';
    if (!setEnabled(false, on, false)) strcpy(reply, "Err - storage or channel");
    else strcpy(reply, "OK");
    return true;
  }
#endif
#if SYNC_SETTINGS_WITH_REGION
  if (strcmp(command, "sync.region list") == 0) {
    if (!overlay_ready) strcpy(reply, "Err - storage");
    else listRegions(nullptr, reply);
    return true;
  }
  if (strncmp(command, "sync.region list ", 17) == 0) {
    if (!overlay_ready) strcpy(reply, "Err - storage");
    else listRegions(command + 17, reply);
    return true;
  }
  static const char define[] = "sync.region def";
  if (strncmp(command, define, sizeof(define) - 1) == 0) {
    if (!overlay_ready) strcpy(reply, "Err - storage");
    else if (pending_edit.kind != EDIT_NONE) strcpy(reply, "Err - busy");
    else if (command[sizeof(define) - 1] != ' ') {
      strcpy(reply, "Err - syntax: sync.region def <token> [<token> ...]");
    } else {
      Temp candidate(REGION_DATA_MAX);
      if (!candidate) {
        strcpy(reply, "Err - memory");
        return true;
      }
      uint16_t len;
      RegionResult result = defineRegions(regions, command + sizeof(define),
                                          candidate, REGION_DATA_MAX, len);
      if (result == REGION_NAME) {
        strcpy(reply, "Err - syntax: sync.region def <token> [<token> ...]");
      } else if (result != REGION_OK) editError(result, define, reply);
      else if (!installRegions(candidate, len)) strcpy(reply, "Err - invalid overlay");
      else {
        dirty = true;
        strcpy(reply, "OK");
      }
    }
    return true;
  }
  if (regionEditCommand(context, command, reply)) return true;
  if (strcmp(command, "sync.region clear") == 0) {
    Temp candidate(REGION_DATA_MAX);
    if (!candidate) {
      strcpy(reply, "Err - memory");
      return true;
    }
    uint16_t len;
    RegionResult result = buildEdit(EDIT_CLEAR, nullptr, candidate, len);
    if (!overlay_ready) strcpy(reply, "Err - storage");
    else if (pending_edit.kind != EDIT_NONE) strcpy(reply, "Err - busy");
    else if (result != REGION_OK) strcpy(reply, "Err - invalid overlay");
    else if (context.sender_timestamp != 0) {
      if (!deferEdit(EDIT_CLEAR, nullptr)) strcpy(reply, "Err - busy");
      else strcpy(reply, "OK - save required");
    } else if (!installRegions(candidate, len)) strcpy(reply, "Err - invalid overlay");
    else {
      dirty = true;
      strcpy(reply, "OK - save required");
    }
    return true;
  }
  if (strcmp(command, "sync.region save") == 0) {
    Temp scratch(STORE_MAX);
    if (!overlay_ready || !saveRegions(scratch)) strcpy(reply, "Err - storage");
    else {
      memset(&region_record, 0, sizeof(region_record));
      dirty = false;
      strcpy(reply, region_state.enabled ? "OK - campaigns may overwrite" : "OK");
    }
    return true;
  }
  if (strcmp(command, "sync.region reload") == 0) {
    Temp scratch(STORE_MAX);
    bool degraded = false;
    if (!overlay_ready || !storedRegionsValid(scratch, degraded)) {
      strcpy(reply, "Err - storage");
    }
    else if (receiver.campaign(REGION).state != RECEIVE_IDLE) {
      strcpy(reply, "Err - busy");
    }
    else if (context.sender_timestamp != 0) {
      if (!deferEdit(EDIT_RELOAD, nullptr)) strcpy(reply, "Err - busy");
      else strcpy(reply, degraded ? "OK - degraded storage" : "OK");
    }
    else if (!loadRegions(scratch)) strcpy(reply, "Err - storage");
    else {
      dirty = false;
      strcpy(reply, overlay_degraded ? "OK - degraded storage" : "OK");
    }
    return true;
  }
#endif
#if SYNC_SETTINGS_WITH_REGION
  if (strcmp(command, "sync.region publish.status") == 0) {
    status(REGION, reply);
    return true;
  }
#endif
#if SYNC_SETTINGS_WITH_POLICY
  if (strcmp(command, "sync.policy publish.status") == 0) {
    status(POLICY, reply);
    return true;
  }
#endif
#if SYNC_SETTINGS_WITH_REGION
  static const char region_report[] = "sync.region publish.report ";
  if (strncmp(command, region_report, sizeof(region_report) - 1) == 0) {
    return reportCommand(REGION, command + sizeof(region_report) - 1, reply);
  }
#endif
#if SYNC_SETTINGS_WITH_POLICY
  static const char policy_report[] = "sync.policy publish.report ";
  if (strncmp(command, policy_report, sizeof(policy_report) - 1) == 0) {
    return reportCommand(POLICY, command + sizeof(policy_report) - 1, reply);
  }
#endif
  if (
#if SYNC_SETTINGS_WITH_REGION
      strcmp(command, "sync.region publish.report") == 0 ||
#endif
#if SYNC_SETTINGS_WITH_POLICY
      strcmp(command, "sync.policy publish.report") == 0 ||
#endif
      false) {
    strcpy(reply, "Err - page");
    return true;
  }
#if SYNC_SETTINGS_WITH_REGION
  if (strcmp(command, "sync.region publish.abort") == 0) {
    return abortCommand(REGION, reply);
  }
#endif
#if SYNC_SETTINGS_WITH_POLICY
  if (strcmp(command, "sync.policy publish.abort") == 0) {
    return abortCommand(POLICY, reply);
  }
#endif
  if (scheduleCommand(command, reply)) return true;
  return false;
}

struct PacketMatch {
  const mesh::Packet* packet;
};

#if SYNC_SETTINGS_WITH_REGION
static bool matches(const uint8_t key[16], void* context) {
  PacketMatch* match = static_cast<PacketMatch*>(context);
  TransportKey scope;
  memcpy(scope.key, key, sizeof(scope.key));
  return scope.calcTransportCode(match->packet) == match->packet->transport_codes[0];
}

static ScopeMatch matchBase(mesh::Packet* packet, RegionMap& base,
                            TransportKey& allowed) {
  ScopeMatch result = {SCOPE_NONE, -1, nullptr};
  for (int i = 0; i < base.getCount(); ++i) {
    const RegionEntry* entry = base.getByIdx(i);
    TransportKey keys[4];
    int count = base.getTransportKeysFor(*entry, keys, 4);
    for (int k = 0; k < count; ++k) {
      if (keys[k].calcTransportCode(packet) != packet->transport_codes[0]) continue;
      if ((entry->flags & REGION_DENY_FLOOD) != 0) {
        result.state = SCOPE_DENY;
        return result;
      }
      if (result.state == SCOPE_NONE) {
        allowed = keys[k];
        result.state = SCOPE_ALLOW;
      }
    }
  }
  return result;
}

static bool resolve(mesh::Packet* packet, RegionMap* base, ModRegionMatch* out) {
  if (!enabled) return false;
  if (packet == nullptr || base == nullptr || out == nullptr) return false;

  PacketMatch context = {packet};
  ScopeMatch overlay = matchRegions(regions, matches, &context);
  TransportKey base_key;
  ScopeMatch native = matchBase(packet, *base, base_key);
  ScopeMatch result = chooseScope(overlay, native);

  out->state = result.state;
  memset(out->key, 0, sizeof(out->key));
  if (result.state == SCOPE_ALLOW) {
    const uint8_t* key = overlay.state == SCOPE_ALLOW &&
                         native.state != SCOPE_DENY ? overlay.key : base_key.key;
    memcpy(out->key, key, sizeof(out->key));
  }
  return true;
}

static ScopeState findBase(const uint8_t* name, uint8_t len, void* context) {
  if (len > REGION_NAME_MAX) return SCOPE_NONE;
  char text[REGION_NAME_MAX + 1];
  memcpy(text, name, len);
  text[len] = 0;
  RegionEntry* entry = static_cast<RegionMap*>(context)->findByName(text);
  if (entry == nullptr) return SCOPE_NONE;
  return (entry->flags & REGION_DENY_FLOOD) != 0 ? SCOPE_DENY : SCOPE_ALLOW;
}

static bool readBase(uint8_t position, RegionView& out, void* context) {
  const RegionEntry* entry = static_cast<RegionMap*>(context)->getByIdx(position);
  if (entry == nullptr) return false;
  const char* name = entry->name[0] == '#' ? entry->name + 1 : entry->name;
  out.parent = 0;
  out.denied = (entry->flags & REGION_DENY_FLOOD) != 0;
  out.name = (const uint8_t*)name;
  out.name_len = (uint8_t)strlen(name);
  return true;
}

static int exportRegions(RegionMap* base, char* out, size_t capacity,
                         uint8_t excluded_flags) {
  if (!enabled || base == nullptr || out == nullptr || capacity == 0 ||
      excluded_flags != REGION_DENY_FLOOD) return -1;
  RegionSource source = {};
  source.count = (uint8_t)base->getCount();
  source.wildcard = (base->getWildcard().flags & REGION_DENY_FLOOD) == 0;
  source.find = findBase;
  source.get = readBase;
  source.context = base;
  return writeRegionList(regions, source, out, capacity);
}
#endif
}  // namespace sync
}  // namespace mobmesh

bool syncRoute(mesh::Packet* packet, RegionMap* base, ModRegionMatch* out) {
#if SYNC_SETTINGS_WITH_REGION
  return mobmesh::sync::resolve(packet, base, out);
#else
  (void)packet;
  (void)base;
  (void)out;
  return false;
#endif
}

int syncExportRegions(RegionMap* base, char* out, size_t capacity,
                      uint8_t excluded_flags) {
#if SYNC_SETTINGS_WITH_REGION
  return mobmesh::sync::exportRegions(base, out, capacity, excluded_flags);
#else
  (void)base;
  (void)out;
  (void)capacity;
  (void)excluded_flags;
  return -1;
#endif
}

void syncRecv(const mesh::Packet* packet, bool accepted,
              const uint8_t scope_key[16]) {
  mobmesh::sync::receive(packet, accepted, scope_key);
}

void syncLoop() {
  if (!mobmesh::sync::booted) mobmesh::sync::boot();
  const mobmesh::sync::InboxFrame* frame = mobmesh::sync::inbox.front();
  if (frame != nullptr) {
    mobmesh::sync::receiver.take(frame->data, frame->len, millis(), modClockGet(),
                                 frame->scoped, frame->key);
    if (frame->len > 4) {
      uint8_t dataset = frame->data[4];
      if (mobmesh::sync::receiver.campaign(dataset).state ==
          mobmesh::sync::RECEIVE_STAGED) {
        mobmesh::sync::receiver.finish(dataset);
      }
    }
    mobmesh::sync::inbox.drop();
  }
  mobmesh::sync::receiver.tick(millis());
  mobmesh::sync::transmitter().tick(millis());
#if SYNC_SETTINGS_WITH_REGION
  if (mobmesh::sync::disable_pending) {
    mobmesh::sync::enabled = false;
    mobmesh::sync::disable_pending = false;
  }
  if (mobmesh::sync::pending_edit.kind != mobmesh::sync::EDIT_NONE) {
    mobmesh::sync::EditKind kind = mobmesh::sync::pending_edit.kind;
    mobmesh::sync::pending_edit.kind = mobmesh::sync::EDIT_NONE;
    mobmesh::sync::applyEdit(kind, mobmesh::sync::pending_edit.name);
  }
#endif
}

void syncTx(uint32_t packet_id, bool succeeded) {
  mobmesh::sync::transmitter().complete(packet_id, succeeded, millis());
}

bool syncCli(const ModCliContext& context, char* command, char* reply) {
  return mobmesh::sync::handleCli(context, command, reply);
}
