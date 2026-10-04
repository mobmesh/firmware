#include "SyncStore.h"

#include <string.h>

namespace mobmesh {
namespace sync {

static const uint8_t STORE_MAGIC[] = {'S', 'Y', 'F', 'S'};
static const uint16_t PUBLISHER_FIXED_LEN = 5;
static const uint8_t PUBLISHER_LEGACY_LEN = 35;
static const uint8_t PUBLISHER_RECORD_LEN = 39;
static const uint16_t REGION_META_LEN = 45;
static const uint16_t STATE_FIXED_LEN = 117;
static const uint8_t STATE_REPLAY_MAX = 16;
static const uint8_t STATE_REPLAY_LEN = 38;

static uint16_t read16(const uint8_t* p) {
  return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static uint32_t read32(const uint8_t* p) {
  return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 |
         (uint32_t)p[2] << 8 | p[3];
}

static uint64_t read64(const uint8_t* p) {
  return (uint64_t)read32(p) << 32 | read32(p + 4);
}

static void write16(uint8_t* p, uint16_t value) {
  p[0] = (uint8_t)(value >> 8);
  p[1] = (uint8_t)value;
}

static void write32(uint8_t* p, uint32_t value) {
  p[0] = (uint8_t)(value >> 24);
  p[1] = (uint8_t)(value >> 16);
  p[2] = (uint8_t)(value >> 8);
  p[3] = (uint8_t)value;
}

static void write64(uint8_t* p, uint64_t value) {
  write32(p, (uint32_t)(value >> 32));
  write32(p + 4, (uint32_t)value);
}

static bool allZero(const uint8_t* data, size_t len) {
  for (size_t i = 0; i < len; ++i) {
    if (data[i] != 0) return false;
  }
  return true;
}

static bool anyNonzero(const uint8_t* data, size_t len) {
  return !allZero(data, len);
}

SerialOrder serialOrder(uint32_t first, uint32_t second) {
  if (first == second) return SERIAL_EQUAL;
  uint32_t delta = second - first;
  if (delta == 0x80000000u) return SERIAL_AMBIGUOUS;
  return delta < 0x80000000u ? SERIAL_BEFORE : SERIAL_AFTER;
}

uint32_t nextSequence(uint32_t current) {
  ++current;
  return current == 0 ? 1 : current;
}

StoreResult readStore(const uint8_t* data, size_t len, uint8_t expected_type,
                      StoreView& out, HashFn hash, void* context) {
  if (data == nullptr || hash == nullptr || len < STORE_OVERHEAD || len > STORE_MAX ||
      memcmp(data, STORE_MAGIC, sizeof(STORE_MAGIC)) != 0) {
    return STORE_MALFORMED;
  }
  if (data[4] != expected_type) return STORE_TYPE;
  if (data[5] != STORE_VERSION) return STORE_VERSION_UNSUPPORTED;

  uint32_t sequence = read32(data + 6);
  uint16_t payload_len = read16(data + 10);
  if (sequence == 0) return STORE_SEQUENCE;
  if ((size_t)payload_len + STORE_OVERHEAD != len) return STORE_MALFORMED;

  uint8_t digest[STORE_DIGEST_LEN];
  hash(data, len - STORE_DIGEST_LEN, digest, context);
  if (memcmp(digest, data + len - STORE_DIGEST_LEN, STORE_DIGEST_LEN) != 0) {
    return STORE_DIGEST;
  }

  out.type = data[4];
  out.version = data[5];
  out.sequence = sequence;
  out.payload = data + STORE_HEADER_LEN;
  out.payload_len = payload_len;
  return STORE_OK;
}

size_t writeStore(uint8_t type, uint32_t sequence, const uint8_t* payload,
                  uint16_t payload_len, uint8_t* out, size_t capacity,
                  HashFn hash, void* context) {
  size_t len = (size_t)payload_len + STORE_OVERHEAD;
  if (out == nullptr || hash == nullptr || sequence == 0 ||
      (payload_len != 0 && payload == nullptr) || len > capacity || len > STORE_MAX ||
      type < STORE_CONFIG || type > STORE_TIME) {
    return 0;
  }

  memcpy(out, STORE_MAGIC, sizeof(STORE_MAGIC));
  out[4] = type;
  out[5] = STORE_VERSION;
  write32(out + 6, sequence);
  write16(out + 10, payload_len);
  if (payload_len != 0) memmove(out + STORE_HEADER_LEN, payload, payload_len);
  hash(out, STORE_HEADER_LEN + payload_len,
       out + STORE_HEADER_LEN + payload_len, context);
  return len;
}

static bool validSlot(const PairIO& io, uint8_t slot, uint8_t type,
                      uint8_t* scratch, size_t capacity, StoreView& view,
                      HashFn hash, PayloadFn validate, void* context) {
  size_t len = 0;
  if (!io.read(slot, scratch, capacity, len, io.context)) return false;
  if (readStore(scratch, len, type, view, hash, context) != STORE_OK) return false;
  return validate == nullptr ||
         validate(view.payload, view.payload_len, context) == STORE_OK;
}

PairResult readPair(const PairIO& io, uint8_t type, PairView& out,
                    uint8_t* scratch, size_t capacity, HashFn hash,
                    PayloadFn validate, void* context) {
  if (io.exists == nullptr || io.read == nullptr || scratch == nullptr ||
      capacity < STORE_MAX) return PAIR_FAULT;

  bool present[2] = {io.exists(0, io.context), io.exists(1, io.context)};
  if (!present[0] && !present[1]) return PAIR_ABSENT;

  bool valid[2] = {};
  uint32_t sequence[2] = {};
  StoreView view;
  for (uint8_t slot = 0; slot < 2; ++slot) {
    valid[slot] = present[slot] && validSlot(io, slot, type, scratch, capacity,
                                            view, hash, validate, context);
    if (valid[slot]) sequence[slot] = view.sequence;
  }
  if (!valid[0] && !valid[1]) return PAIR_FAULT;

  uint8_t winner = valid[1] ? 1 : 0;
  if (valid[0] && valid[1]) {
    SerialOrder order = serialOrder(sequence[0], sequence[1]);
    if (order == SERIAL_EQUAL || order == SERIAL_AMBIGUOUS) return PAIR_AMBIGUOUS;
    winner = order == SERIAL_BEFORE ? 1 : 0;
  }

  if (winner == 0 && !validSlot(io, winner, type, scratch, capacity, view,
                 hash, validate, context)) return PAIR_FAULT;
  out.slot = winner;
  out.degraded = (present[0] && !valid[0]) || (present[1] && !valid[1]);
  out.record = view;
  return PAIR_OK;
}

PairResult buildPair(const PairIO& io, uint8_t type, PairView& out,
                     uint8_t* scratch, size_t capacity, HashFn hash,
                     PayloadFn validate, PayloadBuildFn build, void* context) {
  if (io.write == nullptr || scratch == nullptr || capacity < STORE_MAX ||
      build == nullptr) return PAIR_FAULT;
  PairView current;
  PairResult found = readPair(io, type, current, scratch, capacity,
                              hash, validate, context);
  if (found != PAIR_OK && found != PAIR_ABSENT) return found;
  uint8_t target = found == PAIR_OK ? (uint8_t)(current.slot ^ 1) : 0;
  uint32_t sequence = found == PAIR_OK ? nextSequence(current.record.sequence) : 1;
  uint8_t* payload = scratch + STORE_HEADER_LEN;
  size_t payload_len = build(payload, capacity - STORE_OVERHEAD, context);
  if (hash == nullptr || payload_len == 0 ||
      payload_len > capacity - STORE_OVERHEAD ||
      payload_len > STORE_MAX - STORE_OVERHEAD) return PAIR_FAULT;

  uint8_t expected[STORE_DIGEST_LEN];
  hash(payload, payload_len, expected, context);
  size_t len = writeStore(type, sequence, payload, (uint16_t)payload_len,
                          scratch, capacity, hash, context);
  if (len == 0 || !io.write(target, scratch, len, io.context)) return PAIR_FAULT;

  StoreView verified;
  if (!validSlot(io, target, type, scratch, capacity, verified,
                 hash, validate, context)) return PAIR_FAULT;
  uint8_t actual[STORE_DIGEST_LEN];
  hash(verified.payload, verified.payload_len, actual, context);
  if (verified.sequence != sequence || verified.payload_len != payload_len ||
      memcmp(actual, expected, sizeof(actual)) != 0) return PAIR_FAULT;

  out.slot = target;
  out.degraded = false;
  out.record = verified;
  return PAIR_OK;
}

StoreResult readConfig(const uint8_t* data, size_t len, Config& out) {
  // A 17-byte record predates repeat.gate; read it and default the flag off.
  if (data == nullptr || (len != 17 && len != 18) || data[0] > CHANNEL_MAX) return STORE_PAYLOAD;
  if (len == 18 && data[17] > 1) return STORE_PAYLOAD;
  uint8_t channel_len = data[0];
  if (channel_len == 0) {
    if (!allZero(data + 1, CHANNEL_MAX)) return STORE_PAYLOAD;
  } else if (!validChannel(data + 1, channel_len) ||
             !allZero(data + 1 + channel_len, CHANNEL_MAX - channel_len)) {
    return STORE_PAYLOAD;
  }

  out.channel_len = channel_len;
  memcpy(out.channel, data + 1, channel_len);
  out.channel[channel_len] = '\0';
  out.repeat_gate = len == 18 ? data[17] : 0;
  return STORE_OK;
}

size_t writeConfig(const Config& config, uint8_t out[18]) {
  if (out == nullptr || config.channel_len > CHANNEL_MAX || config.repeat_gate > 1 ||
      (config.channel_len != 0 &&
       !validChannel((const uint8_t*)config.channel, config.channel_len))) {
    return 0;
  }
  memset(out, 0, 18);
  out[0] = config.channel_len;
  memcpy(out + 1, config.channel, config.channel_len);
  out[17] = config.repeat_gate;
  return 18;
}

void defaultPublishers(Publishers& publishers) {
  memset(&publishers, 0, sizeof(publishers));
  publishers.next_id = 1;
}

static StoreResult publishersValid(const Publishers& publishers) {
  if (publishers.count > PUBLISHER_MAX) return STORE_PAYLOAD;
  uint16_t prior = 0;
  bool pending_found = publishers.pending_forget == 0;
  for (uint8_t i = 0; i < publishers.count; ++i) {
    const Publisher& record = publishers.record[i];
    if (record.id == 0 || record.id <= prior ||
        record.status > PUBLISHER_ACTIVE || allZero(record.key, sizeof(record.key))) {
      return STORE_PAYLOAD;
    }
    if (publishers.next_id != 0 && record.id >= publishers.next_id) {
      return STORE_PAYLOAD;
    }
    for (uint8_t k = 0; k < i; ++k) {
      if (memcmp(record.key, publishers.record[k].key, sizeof(record.key)) == 0) {
        return STORE_PAYLOAD;
      }
    }
    if (record.id == publishers.pending_forget) {
      if (record.status != PUBLISHER_REMOVED) return STORE_PAYLOAD;
      pending_found = true;
    }
    prior = record.id;
  }
  return pending_found ? STORE_OK : STORE_PAYLOAD;
}

StoreResult readPublishers(const uint8_t* data, size_t len, Publishers& out) {
  if (data == nullptr || len < PUBLISHER_FIXED_LEN || data[0] > PUBLISHER_MAX) {
    return STORE_PAYLOAD;
  }
  // A 35-byte entry predates the TIME replay high-water, which then reads as zero.
  size_t record_len = len == PUBLISHER_FIXED_LEN + (size_t)data[0] * PUBLISHER_LEGACY_LEN
                          ? PUBLISHER_LEGACY_LEN : PUBLISHER_RECORD_LEN;
  if (len != PUBLISHER_FIXED_LEN + (size_t)data[0] * record_len) return STORE_PAYLOAD;
  Publishers publishers;
  memset(&publishers, 0, sizeof(publishers));
  publishers.count = data[0];
  publishers.next_id = read16(data + 1);
  publishers.pending_forget = read16(data + 3);
  size_t cursor = PUBLISHER_FIXED_LEN;
  for (uint8_t i = 0; i < publishers.count; ++i) {
    Publisher& record = publishers.record[i];
    record.id = read16(data + cursor);
    record.status = data[cursor + 2];
    memcpy(record.key, data + cursor + 3, sizeof(record.key));
    if (record_len == PUBLISHER_RECORD_LEN) record.time_generation = read32(data + cursor + 35);
    cursor += record_len;
  }
  StoreResult result = publishersValid(publishers);
  if (result == STORE_OK) out = publishers;
  return result;
}

size_t writePublishers(const Publishers& publishers, uint8_t* out, size_t capacity) {
  size_t len = PUBLISHER_FIXED_LEN +
               (size_t)publishers.count * PUBLISHER_RECORD_LEN;
  if (out == nullptr || len > capacity ||
      publishersValid(publishers) != STORE_OK) return 0;
  memset(out, 0, len);
  out[0] = publishers.count;
  write16(out + 1, publishers.next_id);
  write16(out + 3, publishers.pending_forget);
  size_t cursor = PUBLISHER_FIXED_LEN;
  for (uint8_t i = 0; i < publishers.count; ++i) {
    const Publisher& record = publishers.record[i];
    write16(out + cursor, record.id);
    out[cursor + 2] = record.status;
    memcpy(out + cursor + 3, record.key, sizeof(record.key));
    write32(out + cursor + 35, record.time_generation);
    cursor += PUBLISHER_RECORD_LEN;
  }
  return len;
}

int8_t publisherByKey(const Publishers& publishers, const uint8_t key[32]) {
  if (key == nullptr) return -1;
  for (uint8_t i = 0; i < publishers.count; ++i) {
    if (memcmp(publishers.record[i].key, key, sizeof(publishers.record[i].key)) == 0) {
      return (int8_t)i;
    }
  }
  return -1;
}

TrustResult authorizePublisher(Publishers& publishers, const uint8_t key[32]) {
  int8_t found = publisherByKey(publishers, key);
  if (found >= 0) {
    if (publishers.record[found].status == PUBLISHER_ACTIVE) return TRUST_EXISTS;
    publishers.record[found].status = PUBLISHER_ACTIVE;
    return TRUST_OK;
  }
  if (publishers.count == PUBLISHER_MAX || publishers.next_id == 0) {
    return TRUST_CAPACITY;
  }
  Publisher& record = publishers.record[publishers.count++];
  memset(&record, 0, sizeof(record));
  record.id = publishers.next_id;
  record.status = PUBLISHER_ACTIVE;
  memcpy(record.key, key, sizeof(record.key));
  publishers.next_id = publishers.next_id == 0xffff ? 0 :
                       (uint16_t)(publishers.next_id + 1);
  return TRUST_OK;
}

TrustResult removePublisher(Publishers& publishers, const uint8_t key[32]) {
  int8_t found = publisherByKey(publishers, key);
  if (found < 0) return TRUST_NOT_FOUND;
  Publisher& record = publishers.record[found];
  if (record.status != PUBLISHER_ACTIVE) return TRUST_STATE;
  record.status = PUBLISHER_REMOVED;
  return TRUST_OK;
}

TrustResult markPublisherForget(Publishers& publishers, const uint8_t key[32]) {
  if (publishers.pending_forget != 0) return TRUST_STATE;
  int8_t found = publisherByKey(publishers, key);
  if (found < 0) return TRUST_NOT_FOUND;
  const Publisher& record = publishers.record[found];
  if (record.status != PUBLISHER_REMOVED) return TRUST_STATE;
  publishers.pending_forget = record.id;
  return TRUST_OK;
}

TrustResult completePublisherForget(Publishers& publishers) {
  if (publishers.pending_forget == 0) return TRUST_STATE;
  uint8_t index = publishers.count;
  for (uint8_t i = 0; i < publishers.count; ++i) {
    if (publishers.record[i].id == publishers.pending_forget) index = i;
  }
  if (index == publishers.count ||
      publishers.record[index].status != PUBLISHER_REMOVED) return TRUST_STATE;
  for (uint8_t i = index + 1; i < publishers.count; ++i) {
    publishers.record[i - 1] = publishers.record[i];
  }
  --publishers.count;
  memset(&publishers.record[publishers.count], 0, sizeof(publishers.record[0]));
  publishers.pending_forget = 0;
  return TRUST_OK;
}

void erasePublisherReplay(DatasetState& state, uint16_t publisher_id) {
  for (uint8_t i = 0; i < state.replay_count; ++i) {
    if (state.replay[i].publisher_id != publisher_id) continue;
    for (uint8_t k = i + 1; k < state.replay_count; ++k) {
      state.replay[k - 1] = state.replay[k];
    }
    --state.replay_count;
    memset(&state.replay[state.replay_count], 0, sizeof(state.replay[0]));
    return;
  }
}

StoreResult setPublisherReplay(DatasetState& state, uint16_t publisher_id,
                               uint32_t generation,
                               const uint8_t digest[DIGEST_LEN]) {
  if (publisher_id == 0 || generation == 0 || digest == nullptr) return STORE_PAYLOAD;
  uint8_t at = 0;
  while (at < state.replay_count &&
         state.replay[at].publisher_id < publisher_id) ++at;
  if (at == state.replay_count || state.replay[at].publisher_id != publisher_id) {
    if (state.replay_count == STATE_REPLAY_MAX) return STORE_CAPACITY;
    for (uint8_t i = state.replay_count; i > at; --i) {
      state.replay[i] = state.replay[i - 1];
    }
    ++state.replay_count;
    state.replay[at].publisher_id = publisher_id;
  }
  state.replay[at].generation = generation;
  memcpy(state.replay[at].digest, digest, DIGEST_LEN);
  return STORE_OK;
}

StoreResult readRegionRecord(const uint8_t* data, size_t len, RegionRecord& out,
                             HashFn hash, void* context) {
  if (data == nullptr || hash == nullptr || len < REGION_META_LEN + 1 ||
      len > REGION_META_LEN + REGION_DATA_MAX || (data[0] & 0xfc) != 0) {
    return STORE_PAYLOAD;
  }

  bool receipt = (data[0] & 1) != 0;
  bool reset = (data[0] & 2) != 0;
  uint32_t receipt_id = read32(data + 1);
  uint16_t publisher_id = read16(data + 5);
  uint32_t generation = read32(data + 7);
  uint16_t regions_len = read16(data + 43);
  RegionIndex index;
  if ((size_t)regions_len + REGION_META_LEN != len || regions_len == 0 ||
      indexRegions(data + REGION_META_LEN, regions_len, index) != REGION_OK) {
    return STORE_PAYLOAD;
  }

  if (!receipt) {
    if (reset || receipt_id != 0 || publisher_id != 0 || generation != 0 ||
        !allZero(data + 11, DIGEST_LEN)) return STORE_PAYLOAD;
  } else {
    if (receipt_id == 0 || publisher_id == 0 || generation == 0) return STORE_PAYLOAD;
    uint8_t digest[DIGEST_LEN];
    hash(data + REGION_META_LEN, regions_len, digest, context);
    if (memcmp(digest, data + 11, DIGEST_LEN) != 0) return STORE_DIGEST;
  }

  out.receipt = receipt;
  out.reset = reset;
  out.receipt_id = receipt_id;
  out.publisher_id = publisher_id;
  out.generation = generation;
  memcpy(out.digest, data + 11, DIGEST_LEN);
  out.regions = data + REGION_META_LEN;
  out.regions_len = regions_len;
  return STORE_OK;
}

size_t writeRegionRecord(const RegionRecord& record, uint8_t* out, size_t capacity,
                         HashFn hash, void* context) {
  size_t len = REGION_META_LEN + record.regions_len;
  RegionIndex index;
  if (out == nullptr || hash == nullptr || record.regions == nullptr ||
      len > capacity || indexRegions(record.regions, record.regions_len, index) != REGION_OK ||
      (record.receipt &&
       (record.receipt_id == 0 || record.publisher_id == 0 || record.generation == 0)) ||
      (!record.receipt && (record.reset || record.receipt_id != 0 ||
       record.publisher_id != 0 || record.generation != 0))) {
    return 0;
  }

  memset(out, 0, REGION_META_LEN);
  out[0] = (uint8_t)((record.receipt ? 1 : 0) | (record.reset ? 2 : 0));
  write32(out + 1, record.receipt_id);
  write16(out + 5, record.publisher_id);
  write32(out + 7, record.generation);
  write16(out + 43, record.regions_len);
  memmove(out + REGION_META_LEN, record.regions, record.regions_len);
  if (record.receipt) {
    hash(out + REGION_META_LEN, record.regions_len, out + 11, context);
  }
  return len;
}

void defaultState(DatasetState& state) {
  memset(&state, 0, sizeof(state));
  state.interval_hours = 12;
  state.duration_days = 3;
}

static StoreResult stateValid(const DatasetState& state) {
  if (state.interval_hours < 3 || state.interval_hours > 24 ||
      state.duration_days < 1 || state.duration_days > 4 ||
      state.guard > GUARD_QUIET || state.route_kind > 1 ||
      state.warning_reason > 1 || state.abort_sent > 1 ||
      state.replay_count > STATE_REPLAY_MAX) return STORE_PAYLOAD;

  bool has_local_key = anyNonzero(state.local_key, sizeof(state.local_key));
  if ((state.local_generation == 0) != !has_local_key) return STORE_PAYLOAD;

  if (state.guard == GUARD_IDLE) {
    if (state.guard_generation != 0 || state.campaign_start != 0 ||
        state.guard_days != 0 || state.route_kind != 0 ||
        anyNonzero(state.route_key, sizeof(state.route_key)) ||
        anyNonzero(state.manifest_hash, sizeof(state.manifest_hash))) {
      return STORE_PAYLOAD;
    }
  } else {
    if (state.guard_generation == 0 || state.campaign_start == 0 ||
        state.guard_days < 1 || state.guard_days > 4 ||
        !anyNonzero(state.manifest_hash, sizeof(state.manifest_hash)) ||
        (state.route_kind == 0 && anyNonzero(state.route_key, sizeof(state.route_key))) ||
        (state.route_kind == 1 && !anyNonzero(state.route_key, sizeof(state.route_key)))) {
      return STORE_PAYLOAD;
    }
  }

  if (state.warning_reason == 0) {
    if (state.warning_generation != 0 || state.warning_time != 0 ||
        state.warning_expiry != 0 || state.abort_sent != 0) return STORE_PAYLOAD;
  } else if (state.warning_generation == 0 || state.warning_time == 0 ||
             state.warning_expiry == 0) {
    return STORE_PAYLOAD;
  }

  uint16_t prior = 0;
  for (uint8_t i = 0; i < state.replay_count; ++i) {
    const ReplayRecord& replay = state.replay[i];
    if (replay.publisher_id == 0 || replay.publisher_id <= prior ||
        replay.generation == 0) return STORE_PAYLOAD;
    prior = replay.publisher_id;
  }
  return STORE_OK;
}

StoreResult readState(const uint8_t* data, size_t len, DatasetState& out) {
  if (data == nullptr || len < STATE_FIXED_LEN || data[0] > 1 ||
      data[116] > STATE_REPLAY_MAX ||
      len != STATE_FIXED_LEN + (size_t)data[116] * STATE_REPLAY_LEN) {
    return STORE_PAYLOAD;
  }

  DatasetState state;
  memset(&state, 0, sizeof(state));
  state.enabled = (data[0] & 1) != 0;
  state.interval_hours = data[1];
  state.duration_days = data[2];
  state.local_generation = read32(data + 3);
  memcpy(state.local_key, data + 7, sizeof(state.local_key));
  state.guard = data[39];
  state.guard_generation = read32(data + 40);
  state.campaign_start = read32(data + 44);
  state.guard_days = data[48];
  state.route_kind = data[49];
  memcpy(state.route_key, data + 50, sizeof(state.route_key));
  memcpy(state.manifest_hash, data + 66, sizeof(state.manifest_hash));
  state.warning_generation = read32(data + 98);
  state.warning_time = read32(data + 102);
  state.warning_expiry = read32(data + 106);
  state.abort_sent = data[110];
  state.warning_reason = data[111];
  state.settled_receipt = read32(data + 112);
  state.replay_count = data[116];

  size_t cursor = STATE_FIXED_LEN;
  for (uint8_t i = 0; i < state.replay_count; ++i) {
    ReplayRecord& replay = state.replay[i];
    replay.publisher_id = read16(data + cursor);
    replay.generation = read32(data + cursor + 2);
    memcpy(replay.digest, data + cursor + 6, sizeof(replay.digest));
    cursor += STATE_REPLAY_LEN;
  }

  StoreResult result = stateValid(state);
  if (result == STORE_OK) out = state;
  return result;
}

size_t writeState(const DatasetState& state, uint8_t* out, size_t capacity) {
  size_t len = STATE_FIXED_LEN + (size_t)state.replay_count * STATE_REPLAY_LEN;
  if (out == nullptr || len > capacity || stateValid(state) != STORE_OK) return 0;

  memset(out, 0, len);
  out[0] = state.enabled ? 1 : 0;
  out[1] = state.interval_hours;
  out[2] = state.duration_days;
  write32(out + 3, state.local_generation);
  memcpy(out + 7, state.local_key, sizeof(state.local_key));
  out[39] = state.guard;
  write32(out + 40, state.guard_generation);
  write32(out + 44, state.campaign_start);
  out[48] = state.guard_days;
  out[49] = state.route_kind;
  memcpy(out + 50, state.route_key, sizeof(state.route_key));
  memcpy(out + 66, state.manifest_hash, sizeof(state.manifest_hash));
  write32(out + 98, state.warning_generation);
  write32(out + 102, state.warning_time);
  write32(out + 106, state.warning_expiry);
  out[110] = state.abort_sent;
  out[111] = state.warning_reason;
  write32(out + 112, state.settled_receipt);
  out[116] = state.replay_count;

  size_t cursor = STATE_FIXED_LEN;
  for (uint8_t i = 0; i < state.replay_count; ++i) {
    const ReplayRecord& replay = state.replay[i];
    write16(out + cursor, replay.publisher_id);
    write32(out + cursor + 2, replay.generation);
    memcpy(out + cursor + 6, replay.digest, sizeof(replay.digest));
    cursor += STATE_REPLAY_LEN;
  }
  return len;
}

StoreResult readPolicyRecovery(const uint8_t* data, size_t len,
                               PolicyRecovery& out, HashFn hash, void* context) {
  if (data == nullptr || len != 74 || data[0] > RECOVERY_RESTORING) {
    return STORE_PAYLOAD;
  }
  if (data[0] == RECOVERY_IDLE) {
    if (anyNonzero(data + 1, 73)) return STORE_PAYLOAD;
    memset(&out, 0, sizeof(out));
    return STORE_OK;
  }
  if ((data[1] & 0xfe) != 0 || read32(data + 2) == 0 ||
      read16(data + 6) == 0 || read32(data + 8) == 0 ||
      !validPolicyPayload(data + 44, POLICY_DATA_LEN) ||
      !validPolicyPayload(data + 59, POLICY_DATA_LEN) || hash == nullptr) {
    return STORE_PAYLOAD;
  }
  uint8_t digest[DIGEST_LEN];
  hash(data + 59, POLICY_DATA_LEN, digest, context);
  if (memcmp(digest, data + 12, DIGEST_LEN) != 0) return STORE_DIGEST;
  PolicyRecovery value = {};
  value.phase = data[0];
  value.reset = (data[1] & 1) != 0;
  value.receipt_id = read32(data + 2);
  value.publisher_id = read16(data + 6);
  value.generation = read32(data + 8);
  memcpy(value.digest, data + 12, DIGEST_LEN);
  memcpy(value.prior, data + 44, POLICY_DATA_LEN);
  memcpy(value.target, data + 59, POLICY_DATA_LEN);
  out = value;
  return STORE_OK;
}

size_t writePolicyRecovery(const PolicyRecovery& recovery, uint8_t* out,
                           size_t capacity, HashFn hash, void* context) {
  if (out == nullptr || capacity < 74) return 0;
  if (recovery.phase == RECOVERY_IDLE) {
    memset(out, 0, 74);
    return 74;
  }
  if (recovery.phase > RECOVERY_RESTORING || recovery.receipt_id == 0 ||
      recovery.publisher_id == 0 || recovery.generation == 0 || hash == nullptr ||
      !validPolicyPayload(recovery.prior, POLICY_DATA_LEN) ||
      !validPolicyPayload(recovery.target, POLICY_DATA_LEN)) return 0;
  uint8_t digest[DIGEST_LEN];
  hash(recovery.target, POLICY_DATA_LEN, digest, context);
  if (memcmp(digest, recovery.digest, DIGEST_LEN) != 0) return 0;
  memset(out, 0, 74);
  out[0] = recovery.phase;
  out[1] = recovery.reset ? 1 : 0;
  write32(out + 2, recovery.receipt_id);
  write16(out + 6, recovery.publisher_id);
  write32(out + 8, recovery.generation);
  memcpy(out + 12, recovery.digest, DIGEST_LEN);
  memcpy(out + 44, recovery.prior, POLICY_DATA_LEN);
  memcpy(out + 59, recovery.target, POLICY_DATA_LEN);
  return 74;
}

void defaultRadioRecord(RadioRecord& record) {
  memset(&record, 0, sizeof(record));
  record.schedule.campaign_interval = 10;
  record.schedule.campaign_duration = 360;
  record.schedule.confirm_interval = 5;
  record.schedule.confirm_window = 45;
}

static bool radioReplayValid(const RadioRecord& record) {
  if (record.replay_count > STATE_REPLAY_MAX) return false;
  uint16_t prior = 0;
  for (uint8_t i = 0; i < record.replay_count; ++i) {
    const ReplayRecord& replay = record.replay[i];
    if (replay.publisher_id == 0 || replay.publisher_id <= prior ||
        replay.generation == 0) return false;
    prior = replay.publisher_id;
  }
  return true;
}

static bool radioGuardZero(const RadioRecord& record) {
  return record.role == 0 && record.publisher_id == 0 && record.migration_id == 0 &&
         record.target.freq_hz == 0 && record.target.bw_hz == 0 &&
         record.target.sf == 0 && record.target.cr == 0 && record.resolved_cr == 0 &&
         record.prior.freq_hz == 0 && record.prior.bw_hz == 0 &&
         record.prior.sf == 0 && record.prior.cr == 0 &&
         !anyNonzero(record.target_digest, sizeof(record.target_digest)) &&
         record.start == 0 && record.cutover == 0 &&
         record.captured.campaign_interval == 0 && record.captured.test_interval == 0 &&
         record.captured.test_window == 0 && record.captured.campaign_duration == 0 &&
         record.captured.confirm_interval == 0 && record.captured.confirm_window == 0 &&
         record.channel_len == 0 && !anyNonzero((const uint8_t*)record.channel,
                                                sizeof(record.channel)) &&
         record.route_kind == 0 && !anyNonzero(record.route_key, sizeof(record.route_key)) &&
         record.latest_generation == 0 && record.flags == 0 && record.result == 0;
}

// Stored schedules are structural only; execution revalidates current policy.
static StoreResult radioRecordValid(const RadioRecord& record) {
  if (!radioScheduleWellFormed(record.schedule) || !radioReplayValid(record) ||
      record.phase > RADIO_GUARD_ABORT_PENDING || record.role > RADIO_ROLE_RECEIVER ||
      record.route_kind > 1 || (record.flags & 0xfc) != 0 ||
      record.result > RADIO_RESULT_FAULT || record.channel_len > CHANNEL_MAX) {
    return STORE_PAYLOAD;
  }
  bool retained = record.phase != RADIO_GUARD_IDLE ||
                  record.result != RADIO_RESULT_NONE;
  bool publisher_before_first = record.role == RADIO_ROLE_PUBLISHER &&
                                record.latest_generation == 0;
  bool has_local_key = anyNonzero(record.local_key, sizeof(record.local_key));
  if ((record.local_generation == 0) != !has_local_key &&
      !(publisher_before_first && record.local_generation == 0 && has_local_key)) {
    return STORE_PAYLOAD;
  }
  if (!retained) {
    return radioGuardZero(record) ? STORE_OK : STORE_PAYLOAD;
  }

  float freq;
  float bw;

  if (record.role == RADIO_ROLE_NONE || record.migration_id == 0 ||
      !radioValuesValid(record.target, true) ||
      !radioValuesValid(record.prior, false) ||
      !radioToFloats(record.target, freq, bw) ||
      !radioToFloats(record.prior, freq, bw) ||
      record.resolved_cr < 5 || record.resolved_cr > 8 ||
      !anyNonzero(record.target_digest, sizeof(record.target_digest)) ||
      record.start == 0 || record.cutover <= record.start ||
      !radioScheduleWellFormed(record.captured) || record.channel_len == 0 ||
      !validChannel((const uint8_t*)record.channel, record.channel_len) ||
      record.channel[record.channel_len] != 0 ||
      (record.latest_generation == 0 && !publisher_before_first) ||
      (record.route_kind == 0 && anyNonzero(record.route_key, sizeof(record.route_key))) ||
      (record.route_kind == 1 && !anyNonzero(record.route_key, sizeof(record.route_key)))) {
    return STORE_PAYLOAD;
  }
  for (uint8_t i = record.channel_len + 1; i < sizeof(record.channel); ++i) {
    if (record.channel[i] != 0) return STORE_PAYLOAD;
  }
  if ((record.role == RADIO_ROLE_PUBLISHER && record.publisher_id != 0) ||
      (record.role == RADIO_ROLE_RECEIVER && record.publisher_id == 0)) {
    return STORE_PAYLOAD;
  }
  if (record.phase == RADIO_GUARD_IDLE) {
    return record.result == RADIO_RESULT_NONE ? STORE_PAYLOAD : STORE_OK;
  }
  if (record.result != RADIO_RESULT_NONE) return STORE_PAYLOAD;
  if ((record.phase == RADIO_GUARD_PUBLISHING ||
       record.phase == RADIO_GUARD_CONFIRMING ||
       record.phase == RADIO_GUARD_ABORT_PENDING) &&
      record.role != RADIO_ROLE_PUBLISHER) return STORE_PAYLOAD;
  return STORE_OK;
}

StoreResult readRadioRecord(const uint8_t* data, size_t len, RadioRecord& out) {
  if (data == nullptr || len != 784 || data[0] != RADIO_SCHEMA || data[1] > 1 ||
      data[50] > STATE_REPLAY_MAX) return STORE_PAYLOAD;
  RadioRecord value = {};
  value.enabled = data[1] != 0;
  value.schedule.campaign_interval = read16(data + 2);
  value.schedule.test_interval = read16(data + 4);
  value.schedule.test_window = read16(data + 6);
  value.schedule.campaign_duration = read16(data + 8);
  value.schedule.confirm_interval = read16(data + 10);
  value.schedule.confirm_window = read16(data + 12);
  value.local_generation = read32(data + 14);
  memcpy(value.local_key, data + 18, sizeof(value.local_key));
  value.replay_count = data[50];
  size_t cursor = 51;
  for (uint8_t i = 0; i < STATE_REPLAY_MAX; ++i) {
    if (i < value.replay_count) {
      ReplayRecord& replay = value.replay[i];
      replay.publisher_id = read16(data + cursor);
      replay.generation = read32(data + cursor + 2);
      memcpy(replay.digest, data + cursor + 6, sizeof(replay.digest));
    } else if (!allZero(data + cursor, STATE_REPLAY_LEN)) {
      return STORE_PAYLOAD;
    }
    cursor += STATE_REPLAY_LEN;
  }
  value.phase = data[659];
  value.role = data[660];
  value.publisher_id = read16(data + 661);
  value.migration_id = read64(data + 663);
  value.target.freq_hz = read32(data + 671);
  value.target.bw_hz = read32(data + 675);
  value.target.sf = data[679];
  value.target.cr = data[680];
  value.resolved_cr = data[681];
  value.prior.freq_hz = read32(data + 682);
  value.prior.bw_hz = read32(data + 686);
  value.prior.sf = data[690];
  value.prior.cr = data[691];
  memcpy(value.target_digest, data + 692, sizeof(value.target_digest));
  value.start = read32(data + 724);
  value.cutover = read32(data + 728);
  value.captured.campaign_interval = read16(data + 732);
  value.captured.test_interval = read16(data + 734);
  value.captured.test_window = read16(data + 736);
  value.captured.campaign_duration = read16(data + 738);
  value.captured.confirm_interval = read16(data + 740);
  value.captured.confirm_window = read16(data + 742);
  value.channel_len = data[744];
  memcpy(value.channel, data + 745, CHANNEL_MAX);
  value.channel[CHANNEL_MAX] = 0;
  value.route_kind = data[761];
  memcpy(value.route_key, data + 762, sizeof(value.route_key));
  value.latest_generation = read32(data + 778);
  value.flags = data[782];
  value.result = data[783];
  StoreResult result = radioRecordValid(value);
  if (result == STORE_OK) out = value;
  return result;
}

size_t writeRadioRecord(const RadioRecord& record, uint8_t* out, size_t capacity) {
  if (out == nullptr || capacity < 784 || radioRecordValid(record) != STORE_OK) return 0;
  memset(out, 0, 784);
  out[0] = RADIO_SCHEMA;
  out[1] = record.enabled ? 1 : 0;
  write16(out + 2, record.schedule.campaign_interval);
  write16(out + 4, record.schedule.test_interval);
  write16(out + 6, record.schedule.test_window);
  write16(out + 8, record.schedule.campaign_duration);
  write16(out + 10, record.schedule.confirm_interval);
  write16(out + 12, record.schedule.confirm_window);
  write32(out + 14, record.local_generation);
  memcpy(out + 18, record.local_key, sizeof(record.local_key));
  out[50] = record.replay_count;
  size_t cursor = 51;
  for (uint8_t i = 0; i < record.replay_count; ++i) {
    const ReplayRecord& replay = record.replay[i];
    write16(out + cursor, replay.publisher_id);
    write32(out + cursor + 2, replay.generation);
    memcpy(out + cursor + 6, replay.digest, sizeof(replay.digest));
    cursor += STATE_REPLAY_LEN;
  }
  out[659] = record.phase;
  out[660] = record.role;
  write16(out + 661, record.publisher_id);
  write64(out + 663, record.migration_id);
  write32(out + 671, record.target.freq_hz);
  write32(out + 675, record.target.bw_hz);
  out[679] = record.target.sf;
  out[680] = record.target.cr;
  out[681] = record.resolved_cr;
  write32(out + 682, record.prior.freq_hz);
  write32(out + 686, record.prior.bw_hz);
  out[690] = record.prior.sf;
  out[691] = record.prior.cr;
  memcpy(out + 692, record.target_digest, sizeof(record.target_digest));
  write32(out + 724, record.start);
  write32(out + 728, record.cutover);
  write16(out + 732, record.captured.campaign_interval);
  write16(out + 734, record.captured.test_interval);
  write16(out + 736, record.captured.test_window);
  write16(out + 738, record.captured.campaign_duration);
  write16(out + 740, record.captured.confirm_interval);
  write16(out + 742, record.captured.confirm_window);
  out[744] = record.channel_len;
  memcpy(out + 745, record.channel, record.channel_len);
  out[761] = record.route_kind;
  memcpy(out + 762, record.route_key, sizeof(record.route_key));
  write32(out + 778, record.latest_generation);
  out[782] = record.flags;
  out[783] = record.result;
  return 784;
}

static_assert(REGION_META_LEN + REGION_DATA_MAX == 1102,
              "region record bound changed");
static_assert(STORE_OVERHEAD + 1102 == STORE_MAX, "store bound changed");
static_assert(STATE_FIXED_LEN + STATE_REPLAY_MAX * STATE_REPLAY_LEN == 725,
              "state record bound changed");
static_assert(PUBLISHER_FIXED_LEN + PUBLISHER_MAX * PUBLISHER_RECORD_LEN == 629,
              "publisher record bound changed");
static_assert(STORE_OVERHEAD + 74 == 118, "policy recovery bound changed");
static_assert(STORE_OVERHEAD + 784 == 828, "radio record bound changed");

}  // namespace sync
}  // namespace mobmesh
