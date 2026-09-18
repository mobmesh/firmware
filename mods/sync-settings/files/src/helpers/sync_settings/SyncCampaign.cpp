#include "SyncCampaign.h"

#include <string.h>

namespace mobmesh {
namespace sync {

Inbox::Inbox() { clear(); }

bool Inbox::push(const uint8_t* data, size_t len, bool scoped,
                 const uint8_t* key) {
  if (data == nullptr || len == 0 || len > FRAME_MAX || count_ == INBOX_CAPACITY) {
    return false;
  }
  uint8_t tail = (uint8_t)((head_ + count_) % INBOX_CAPACITY);
  frame_[tail].len = (uint8_t)len;
  frame_[tail].scoped = scoped;
  memset(frame_[tail].key, 0, sizeof(frame_[tail].key));
  if (scoped && key != nullptr) memcpy(frame_[tail].key, key, sizeof(frame_[tail].key));
  memcpy(frame_[tail].data, data, len);
  ++count_;
  return true;
}

const InboxFrame* Inbox::front() const {
  return count_ == 0 ? nullptr : &frame_[head_];
}

void Inbox::drop() {
  if (count_ == 0) return;
  head_ = (uint8_t)((head_ + 1) % INBOX_CAPACITY);
  --count_;
}

void Inbox::clear() {
  head_ = 0;
  count_ = 0;
}

static_assert(sizeof(InboxFrame) <= FRAME_MAX + 18, "inbox frame changed");

static const uint32_t DAY_MS = 86400000UL;
static const uint32_t CLOCK_FLOOR = 1715770351UL;
static const uint32_t RESET_AHEAD = 432000UL;
static const uint32_t CLOCK_SKEW = 86400UL;

static uint8_t slotOf(uint8_t dataset) {
#if SYNC_SETTINGS_WITH_REGION
  if (dataset == REGION) return 0;
#endif
#if SYNC_SETTINGS_WITH_POLICY
  if (dataset == POLICY) return SYNC_SETTINGS_WITH_REGION;
#endif
  return 0xff;
}

static bool same(const uint8_t* a, const uint8_t* b, size_t len) {
  return a != nullptr && b != nullptr && memcmp(a, b, len) == 0;
}

static int replayAt(const DatasetState& state, uint16_t publisher_id) {
  for (uint8_t i = 0; i < state.replay_count; ++i) {
    if (state.replay[i].publisher_id == publisher_id) return i;
  }
  return -1;
}

enum Freshness : uint8_t { FRESH, SEEN, REFUSED };

static Freshness freshness(const DatasetState& state, uint16_t publisher_id,
                           const Manifest& manifest, uint32_t epoch) {
  int at = replayAt(state, publisher_id);
  const ReplayRecord* prior = at < 0 ? nullptr : &state.replay[at];
  bool ordinary = prior == nullptr || manifest.generation > prior->generation;
  if (prior != nullptr && manifest.generation == prior->generation &&
      same(manifest.digest, prior->digest, DIGEST_LEN) && !manifest.reset) return SEEN;
  if (!manifest.reset) return ordinary ? FRESH : REFUSED;
  if (epoch <= CLOCK_FLOOR || prior == nullptr || ordinary ||
      (int64_t)prior->generation <= (int64_t)epoch + RESET_AHEAD) return REFUSED;
  int64_t window = (int64_t)(manifest.days + 1) * 86400;
  int64_t generation = manifest.generation;
  return generation >= (int64_t)epoch - window &&
         generation <= (int64_t)epoch + CLOCK_SKEW ? FRESH : REFUSED;
}

Receiver::Receiver(Publishers& publishers, DatasetState* region,
                   DatasetState* policy, const ReceiveOps& ops)
    : publishers_(publishers), ops_(ops) {
#if SYNC_SETTINGS_WITH_REGION
  state_[slotOf(REGION)] = region;
#else
  (void)region;
#endif
#if SYNC_SETTINGS_WITH_POLICY
  state_[slotOf(POLICY)] = policy;
#else
  (void)policy;
#endif
  memset(campaign_, 0, sizeof(campaign_));
}

Receiver::~Receiver() {
  for (uint8_t i = 0; i < SYNC_SETTINGS_DATASET_COUNT; ++i) clear(campaign_[i]);
}

void Receiver::clear(Campaign& campaign) {
  if (campaign.data != nullptr && ops_.release != nullptr) {
    ops_.release(campaign.data, ops_.context);
  }
  memset(&campaign, 0, sizeof(campaign));
}

const Campaign& Receiver::campaign(uint8_t dataset) const {
  static const Campaign none = {};
  uint8_t slot = slotOf(dataset);
  return slot < SYNC_SETTINGS_DATASET_COUNT ? campaign_[slot] : none;
}

ReceiveResult Receiver::take(const uint8_t* frame, size_t len,
                             uint32_t now_ms, uint32_t epoch, bool scoped,
                             const uint8_t* route_key) {
  uint8_t type = 0;
  WireResult wire = classify(frame, len, type);
  if (wire != WIRE_OK) return wire == WIRE_FOREIGN ? RECEIVE_FOREIGN : RECEIVE_MALFORMED;
  if (type == MANIFEST) return manifest(frame, len, now_ms, epoch, scoped, route_key);
  if (type == CHUNK) return chunk(frame, len);
  return abort(frame, len);
}

ReceiveResult Receiver::manifest(const uint8_t* frame, size_t len,
                                 uint32_t now_ms, uint32_t epoch, bool scoped,
                                 const uint8_t* route_key) {
  if (len < 5 || ops_.accepts == nullptr ||
      !ops_.accepts(frame[4], nullptr, ops_.context)) return RECEIVE_DISABLED;
  Manifest value;
  if (readManifest(frame, len, value) != WIRE_OK) return RECEIVE_MALFORMED;
  uint8_t slot = slotOf(value.dataset);
  if (slot >= SYNC_SETTINGS_DATASET_COUNT || state_[slot] == nullptr ||
      !ops_.accepts(value.dataset, value.channel, ops_.context)) return RECEIVE_DISABLED;

  // A compressed payload is unusable without a decompressor, so refuse it before locking.
  if (value.format == FORMAT_DEFLATE && !canDecode()) return RECEIVE_DECODE;

  uint8_t manifest_hash[DIGEST_LEN];
  if (ops_.hash == nullptr) return RECEIVE_STORAGE;
  ops_.hash(frame, len, manifest_hash, ops_.context);
  Campaign& active = campaign_[slot];
  if (active.state != RECEIVE_IDLE) {
    return same(active.manifest_hash, manifest_hash, DIGEST_LEN)
               ? RECEIVE_OK : RECEIVE_LOCKED;
  }

  const Publisher* publisher = nullptr;
  for (uint8_t i = 0; i < publishers_.count; ++i) {
    const Publisher& candidate = publishers_.record[i];
    if (candidate.status != PUBLISHER_ACTIVE) continue;
    uint8_t digest[DIGEST_LEN];
    ops_.hash(candidate.key, sizeof(candidate.key), digest, ops_.context);
    if (!same(digest, value.publisher, FINGERPRINT_LEN)) continue;
    if (publisher != nullptr) return RECEIVE_PUBLISHER;
    publisher = &candidate;
  }
  if (publisher == nullptr) return RECEIVE_PUBLISHER;
  if (ops_.verify == nullptr ||
      !ops_.verify(publisher->key, frame, MANIFEST_SIGNED_LEN,
                   value.signature, ops_.context)) return RECEIVE_SIGNATURE;

  Freshness fresh = freshness(*state_[slot], publisher->id, value, epoch);
  if (fresh == SEEN) return RECEIVE_OK;
  if (fresh == REFUSED) return RECEIVE_REPLAY;
  if (ops_.alloc == nullptr || ops_.release == nullptr) return RECEIVE_MEMORY;
  uint8_t* data = ops_.alloc(value.data_len, ops_.context);
  if (data == nullptr) return RECEIVE_MEMORY;

  if (!value.reset) {
    DatasetState next = *state_[slot];
    if (setPublisherReplay(next, publisher->id, value.generation, value.digest) != STORE_OK ||
        ops_.persist == nullptr ||
        !ops_.persist(value.dataset, next, ops_.context)) {
      ops_.release(data, ops_.context);
      return RECEIVE_STORAGE;
    }
    *state_[slot] = next;
  }

  active.state = RECEIVE_ACTIVE;
  active.format = value.format;
  memcpy(active.publisher_key, publisher->key, sizeof(active.publisher_key));
  active.publisher_id = publisher->id;
  active.generation = value.generation;
  active.reset = value.reset;
  active.chunks = value.chunks;
  active.data_len = value.data_len;
  active.received = 0;
  active.deadline = now_ms + (uint32_t)(value.days + 1) * DAY_MS;
  active.scoped = scoped;
  memset(active.route_key, 0, sizeof(active.route_key));
  if (scoped && route_key != nullptr) {
    memcpy(active.route_key, route_key, sizeof(active.route_key));
  }
  memcpy(active.digest, value.digest, sizeof(active.digest));
  memcpy(active.manifest_hash, manifest_hash, sizeof(active.manifest_hash));
  active.data = data;
  return RECEIVE_OK;
}

// The one place that decides whether this build can read a compressed payload.
bool Receiver::canDecode() const { return ops_.inflate != nullptr; }

// Decompresses a complete payload into a fresh buffer without disturbing the
// campaign, so a later failure can still fall back to the retry path.
ReceiveResult Receiver::expand(const Campaign& active, uint8_t*& plain,
                               uint16_t& plain_len) {
  if (!canDecode()) return RECEIVE_DECODE;
  uint8_t* buffer = ops_.alloc(REGION_DATA_MAX, ops_.context);
  if (buffer == nullptr) return RECEIVE_MEMORY;
  size_t written = 0;
  bool ok = ops_.inflate(active.data, active.data_len, buffer, REGION_DATA_MAX,
                         written, ops_.context);
  if (!ok || written == 0 || written > REGION_DATA_MAX) {
    ops_.release(buffer, ops_.context);
    return RECEIVE_DECODE;
  }
  plain = buffer;
  plain_len = (uint16_t)written;
  return RECEIVE_OK;
}

ReceiveResult Receiver::chunk(const uint8_t* frame, size_t len) {
  ChunkView value;
  if (readChunk(frame, len, value) != WIRE_OK) return RECEIVE_MALFORMED;
  uint8_t slot = slotOf(value.dataset);
  if (slot >= SYNC_SETTINGS_DATASET_COUNT || ops_.accepts == nullptr ||
      !ops_.accepts(value.dataset, nullptr, ops_.context)) {
    if (slot < SYNC_SETTINGS_DATASET_COUNT) clear(campaign_[slot]);
    return RECEIVE_DISABLED;
  }
  Campaign& active = campaign_[slot];
  if (active.state != RECEIVE_ACTIVE) return RECEIVE_INCOMPLETE;

  Manifest manifest = {};
  manifest.dataset = value.dataset;
  manifest.generation = active.generation;
  manifest.data_len = active.data_len;
  manifest.chunks = active.chunks;
  uint16_t offset = 0;
  if (match(value, manifest, offset) != WIRE_OK) return RECEIVE_MALFORMED;
  uint16_t bit = (uint16_t)1u << value.index;
  if ((active.received & bit) != 0 &&
      same(active.data + offset, value.data, value.data_len)) return RECEIVE_OK;
  if (ops_.verify == nullptr ||
      !ops_.verify(active.publisher_key, frame, len - SIGNATURE_LEN,
                   value.signature, ops_.context)) return RECEIVE_SIGNATURE;
  memcpy(active.data + offset, value.data, value.data_len);
  active.received |= bit;

  uint16_t complete = (uint16_t)(((uint16_t)1u << active.chunks) - 1u);
  if (active.received != complete) return RECEIVE_OK;
  if (ops_.hash == nullptr) return RECEIVE_STORAGE;

  uint8_t* plain = nullptr;
  uint16_t plain_len = 0;
  if (active.format == FORMAT_DEFLATE) {
    ReceiveResult expanded = expand(active, plain, plain_len);
    if (expanded == RECEIVE_MEMORY) return RECEIVE_MEMORY;
    if (expanded != RECEIVE_OK) {
      clear(active);
      return RECEIVE_DECODE;
    }
  }
  const uint8_t* payload = plain != nullptr ? plain : active.data;
  uint16_t payload_len = plain != nullptr ? plain_len : active.data_len;

  uint8_t digest[DIGEST_LEN];
  ops_.hash(payload, payload_len, digest, ops_.context);
  if (!same(digest, active.digest, DIGEST_LEN)) {
    if (plain != nullptr) ops_.release(plain, ops_.context);
    active.received = 0;
    return RECEIVE_MALFORMED;
  }
  if (ops_.validate == nullptr ||
      !ops_.validate(value.dataset, payload, payload_len, ops_.context)) {
    if (plain != nullptr) ops_.release(plain, ops_.context);
    clear(active);
    return RECEIVE_MALFORMED;
  }
  if (plain != nullptr) {
    // After this swap data_len is the plaintext length while chunks stays the wire count.
    ops_.release(active.data, ops_.context);
    active.data = plain;
    active.data_len = plain_len;
  }
  active.state = RECEIVE_STAGED;
  return RECEIVE_OK;
}

ReceiveResult Receiver::abort(const uint8_t* frame, size_t len) {
  AbortView value;
  if (readAbort(frame, len, value) != WIRE_OK) return RECEIVE_MALFORMED;
  uint8_t slot = slotOf(value.dataset);
  if (slot >= SYNC_SETTINGS_DATASET_COUNT) return RECEIVE_MALFORMED;
  Campaign& active = campaign_[slot];
  if (active.state == RECEIVE_IDLE) return RECEIVE_INCOMPLETE;
  if (!same(value.manifest_hash, active.manifest_hash, DIGEST_LEN)) {
    return RECEIVE_LOCKED;
  }
  if (ops_.verify == nullptr ||
      !ops_.verify(active.publisher_key, frame, ABORT_SIGNED_LEN,
                   value.signature, ops_.context)) return RECEIVE_SIGNATURE;
  clear(active);
  return RECEIVE_OK;
}

void Receiver::tick(uint32_t now_ms) {
  for (uint8_t i = 0; i < SYNC_SETTINGS_DATASET_COUNT; ++i) {
    Campaign& active = campaign_[i];
    if (active.state != RECEIVE_IDLE &&
        (int32_t)(now_ms - active.deadline) >= 0) clear(active);
  }
}

ReceiveResult Receiver::cancel(uint8_t dataset) {
  uint8_t slot = slotOf(dataset);
  if (slot >= SYNC_SETTINGS_DATASET_COUNT) return RECEIVE_MALFORMED;
  if (campaign_[slot].state == RECEIVE_IDLE) return RECEIVE_INCOMPLETE;
  clear(campaign_[slot]);
  return RECEIVE_OK;
}

void Receiver::cancelPublisher(const uint8_t key[32]) {
  if (key == nullptr) return;
  for (uint8_t i = 0; i < SYNC_SETTINGS_DATASET_COUNT; ++i) {
    if (campaign_[i].state != RECEIVE_IDLE &&
        same(campaign_[i].publisher_key, key, 32)) clear(campaign_[i]);
  }
}

ReceiveResult Receiver::finish(uint8_t dataset) {
  uint8_t slot = slotOf(dataset);
  if (slot >= SYNC_SETTINGS_DATASET_COUNT || state_[slot] == nullptr) {
    return RECEIVE_MALFORMED;
  }
  Campaign& active = campaign_[slot];
  if (active.state != RECEIVE_STAGED) return RECEIVE_INCOMPLETE;
  DatasetState next = *state_[slot];
  if (active.reset) {
    if (setPublisherReplay(next, active.publisher_id, active.generation,
                           active.digest) != STORE_OK) {
      clear(active);
      return RECEIVE_STORAGE;
    }
  }
  if (ops_.apply == nullptr || !ops_.apply(dataset, active, next, ops_.context)) {
    clear(active);
    return RECEIVE_STORAGE;
  }
  *state_[slot] = next;
  clear(active);
  return RECEIVE_OK;
}

}  // namespace sync
}  // namespace mobmesh
