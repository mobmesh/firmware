#pragma once

#include "SyncSelect.h"
#include "SyncStore.h"

namespace mobmesh {
namespace sync {

static const uint8_t INBOX_CAPACITY = 4;

struct InboxFrame {
  uint8_t len;
  bool scoped;
  uint8_t key[16];
  uint8_t data[FRAME_MAX];
};

class Inbox {
 public:
  Inbox();

  bool push(const uint8_t* data, size_t len, bool scoped = false,
            const uint8_t* key = nullptr);
  const InboxFrame* front() const;
  void drop();
  void clear();
  uint8_t size() const { return count_; }

 private:
  InboxFrame frame_[INBOX_CAPACITY];
  uint8_t head_;
  uint8_t count_;
};

enum ReceiveResult : uint8_t {
  RECEIVE_OK,
  RECEIVE_FOREIGN,
  RECEIVE_DISABLED,
  RECEIVE_CHANNEL,
  RECEIVE_PUBLISHER,
  RECEIVE_SIGNATURE,
  RECEIVE_REPLAY,
  RECEIVE_LOCKED,
  RECEIVE_MEMORY,
  RECEIVE_STORAGE,
  RECEIVE_MALFORMED,
  RECEIVE_INCOMPLETE,
  RECEIVE_DECODE,
};

enum ReceiveState : uint8_t {
  RECEIVE_IDLE,
  RECEIVE_ACTIVE,
  RECEIVE_STAGED,
};

typedef bool (*AcceptFn)(uint8_t dataset, const char* channel, void* context);
typedef bool (*VerifyFn)(const uint8_t key[32], const uint8_t* message,
                         size_t len, const uint8_t signature[64], void* context);
typedef uint8_t* (*AllocFn)(size_t len, void* context);
typedef void (*FreeFn)(uint8_t* data, void* context);
typedef bool (*PersistFn)(uint8_t dataset, const DatasetState& state, void* context);
typedef bool (*ValidateFn)(uint8_t dataset, const uint8_t* data,
                           size_t len, void* context);
struct Campaign;
typedef bool (*ApplyFn)(uint8_t dataset, const Campaign& campaign,
                        DatasetState& next, void* context);
// Raw deflate decompressor; null means this build rejects compressed payloads.
typedef bool (*InflateFn)(const uint8_t* in, size_t in_len, uint8_t* out,
                          size_t out_cap, size_t& out_len, void* context);

struct ReceiveOps {
  AcceptFn accepts;
  VerifyFn verify;
  HashFn hash;
  AllocFn alloc;
  FreeFn release;
  PersistFn persist;
  ValidateFn validate;
  ApplyFn apply;
  InflateFn inflate;
  void* context;
};

struct Campaign {
  uint8_t state;
  uint8_t format;
  uint8_t publisher_key[32];
  uint16_t publisher_id;
  uint32_t generation;
  bool reset;
  uint8_t chunks;
  uint16_t data_len;
  uint16_t received;
  uint32_t deadline;
  bool scoped;
  uint8_t route_key[16];
  uint8_t digest[DIGEST_LEN];
  uint8_t manifest_hash[DIGEST_LEN];
  uint8_t* data;
};

class Receiver {
 public:
  Receiver(Publishers& publishers, DatasetState* region, DatasetState* policy,
           const ReceiveOps& ops);
  ~Receiver();

  ReceiveResult take(const uint8_t* frame, size_t len,
                     uint32_t now_ms, uint32_t epoch, bool scoped = false,
                     const uint8_t* route_key = nullptr);
  void tick(uint32_t now_ms);
  ReceiveResult cancel(uint8_t dataset);
  void cancelPublisher(const uint8_t key[32]);
  ReceiveResult finish(uint8_t dataset);
  const Campaign& campaign(uint8_t dataset) const;

 private:
  Publishers& publishers_;
  DatasetState* state_[SYNC_SETTINGS_DATASET_COUNT];
  ReceiveOps ops_;
  Campaign campaign_[SYNC_SETTINGS_DATASET_COUNT];

  ReceiveResult manifest(const uint8_t* frame, size_t len,
                         uint32_t now_ms, uint32_t epoch, bool scoped,
                         const uint8_t* route_key);
  ReceiveResult chunk(const uint8_t* frame, size_t len);
  ReceiveResult expand(const Campaign& active, uint8_t*& plain, uint16_t& plain_len);
  bool canDecode() const;
  ReceiveResult abort(const uint8_t* frame, size_t len);
  void clear(Campaign& campaign);
};

}  // namespace sync
}  // namespace mobmesh
