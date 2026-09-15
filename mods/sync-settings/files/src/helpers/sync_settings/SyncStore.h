#pragma once

#include "SyncRegion.h"

namespace mobmesh {
namespace sync {

static const size_t STORE_HEADER_LEN = 12;
static const size_t STORE_DIGEST_LEN = 32;
static const size_t STORE_OVERHEAD = STORE_HEADER_LEN + STORE_DIGEST_LEN;
static const size_t STORE_MAX = 1146;
static const uint8_t STORE_VERSION = 0x01;
static const uint8_t PUBLISHER_MAX = 16;

enum StoreType : uint8_t {
  STORE_CONFIG = 0x01,
  STORE_PUBLISHERS = 0x02,
  STORE_REGION_STATE = 0x03,
  STORE_REGIONS = 0x04,
  STORE_POLICY_STATE = 0x05,
  STORE_POLICY_RECOVERY = 0x06,
};

enum StoreResult : uint8_t {
  STORE_OK,
  STORE_MALFORMED,
  STORE_TYPE,
  STORE_VERSION_UNSUPPORTED,
  STORE_SEQUENCE,
  STORE_DIGEST,
  STORE_CAPACITY,
  STORE_PAYLOAD,
};

enum SerialOrder : uint8_t {
  SERIAL_EQUAL,
  SERIAL_BEFORE,
  SERIAL_AFTER,
  SERIAL_AMBIGUOUS,
};

typedef void (*HashFn)(const uint8_t* data, size_t len,
                       uint8_t out[STORE_DIGEST_LEN], void* context);
typedef StoreResult (*PayloadFn)(const uint8_t* data, size_t len, void* context);
typedef size_t (*PayloadBuildFn)(uint8_t* out, size_t capacity, void* context);

struct PairIO {
  bool (*exists)(uint8_t slot, void* context);
  bool (*read)(uint8_t slot, uint8_t* out, size_t capacity,
               size_t& len, void* context);
  bool (*write)(uint8_t slot, const uint8_t* data, size_t len, void* context);
  void* context;
};

enum PairResult : uint8_t {
  PAIR_OK,
  PAIR_ABSENT,
  PAIR_FAULT,
  PAIR_AMBIGUOUS,
};

struct StoreView {
  uint8_t type;
  uint8_t version;
  uint32_t sequence;
  const uint8_t* payload;
  uint16_t payload_len;
};

struct PairView {
  uint8_t slot;
  bool degraded;
  StoreView record;
};

struct Config {
  uint8_t channel_len;
  char channel[CHANNEL_MAX + 1];
};

enum PublisherStatus : uint8_t {
  PUBLISHER_REMOVED,
  PUBLISHER_ACTIVE,
};

struct Publisher {
  uint16_t id;
  uint8_t status;
  uint8_t key[32];
};

struct Publishers {
  uint8_t count;
  uint16_t next_id;
  uint16_t pending_forget;
  Publisher record[PUBLISHER_MAX];
};

struct DatasetState;

enum TrustResult : uint8_t {
  TRUST_OK,
  TRUST_EXISTS,
  TRUST_NOT_FOUND,
  TRUST_STATE,
  TRUST_CAPACITY,
};

struct RegionRecord {
  bool receipt;
  bool reset;
  uint32_t receipt_id;
  uint16_t publisher_id;
  uint32_t generation;
  uint8_t digest[DIGEST_LEN];
  const uint8_t* regions;
  uint16_t regions_len;
};

enum GuardState : uint8_t {
  GUARD_IDLE,
  GUARD_ACTIVE,
  GUARD_QUIET,
};

struct ReplayRecord {
  uint16_t publisher_id;
  uint32_t generation;
  uint8_t digest[DIGEST_LEN];
};

struct DatasetState {
  bool enabled;
  uint8_t interval_hours;
  uint8_t duration_days;
  uint32_t local_generation;
  uint8_t local_key[32];
  uint8_t guard;
  uint32_t guard_generation;
  uint32_t campaign_start;
  uint8_t guard_days;
  uint8_t route_kind;
  uint8_t route_key[16];
  uint8_t manifest_hash[DIGEST_LEN];
  uint32_t warning_generation;
  uint32_t warning_time;
  uint32_t warning_expiry;
  uint8_t abort_sent;
  uint8_t warning_reason;
  uint32_t settled_receipt;
  uint8_t replay_count;
  ReplayRecord replay[16];
};

enum RecoveryPhase : uint8_t {
  RECOVERY_IDLE,
  RECOVERY_PREPARED,
  RECOVERY_APPLIED,
  RECOVERY_RESTORING,
};

struct PolicyRecovery {
  uint8_t phase;
  bool reset;
  uint32_t receipt_id;
  uint16_t publisher_id;
  uint32_t generation;
  uint8_t digest[DIGEST_LEN];
  uint8_t prior[POLICY_DATA_LEN];
  uint8_t target[POLICY_DATA_LEN];
};

SerialOrder serialOrder(uint32_t first, uint32_t second);
uint32_t nextSequence(uint32_t current);
StoreResult readStore(const uint8_t* data, size_t len, uint8_t expected_type,
                      StoreView& out, HashFn hash, void* context);
size_t writeStore(uint8_t type, uint32_t sequence, const uint8_t* payload,
                  uint16_t payload_len, uint8_t* out, size_t capacity,
                  HashFn hash, void* context);
PairResult readPair(const PairIO& io, uint8_t type, PairView& out,
                    uint8_t* scratch, size_t capacity, HashFn hash,
                    PayloadFn validate, void* context);
PairResult buildPair(const PairIO& io, uint8_t type, PairView& out,
                     uint8_t* scratch, size_t capacity, HashFn hash,
                     PayloadFn validate, PayloadBuildFn build, void* context);

StoreResult readConfig(const uint8_t* data, size_t len, Config& out);
size_t writeConfig(const Config& config, uint8_t out[17]);
void defaultPublishers(Publishers& publishers);
StoreResult readPublishers(const uint8_t* data, size_t len, Publishers& out);
size_t writePublishers(const Publishers& publishers, uint8_t* out, size_t capacity);
int8_t publisherByKey(const Publishers& publishers, const uint8_t key[32]);
TrustResult authorizePublisher(Publishers& publishers, const uint8_t key[32]);
TrustResult removePublisher(Publishers& publishers, const uint8_t key[32]);
TrustResult markPublisherForget(Publishers& publishers, const uint8_t key[32]);
TrustResult completePublisherForget(Publishers& publishers);
void erasePublisherReplay(DatasetState& state, uint16_t publisher_id);
StoreResult setPublisherReplay(DatasetState& state, uint16_t publisher_id,
                               uint32_t generation,
                               const uint8_t digest[DIGEST_LEN]);
StoreResult readRegionRecord(const uint8_t* data, size_t len, RegionRecord& out,
                             HashFn hash, void* context);
size_t writeRegionRecord(const RegionRecord& record, uint8_t* out, size_t capacity,
                         HashFn hash, void* context);
void defaultState(DatasetState& state);
StoreResult readState(const uint8_t* data, size_t len, DatasetState& out);
size_t writeState(const DatasetState& state, uint8_t* out, size_t capacity);
StoreResult readPolicyRecovery(const uint8_t* data, size_t len,
                               PolicyRecovery& out, HashFn hash, void* context);
size_t writePolicyRecovery(const PolicyRecovery& recovery, uint8_t* out,
                           size_t capacity, HashFn hash, void* context);

}  // namespace sync
}  // namespace mobmesh
