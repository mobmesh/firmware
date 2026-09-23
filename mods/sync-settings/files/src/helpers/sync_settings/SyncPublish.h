#pragma once

#include "SyncSelect.h"
#include "SyncWire.h"

namespace mobmesh {
namespace sync {

static const uint32_t TX_WAIT_MS = 60000;
static const uint32_t TX_RETRY_MS = 2000;
static const uint32_t STORAGE_RETRY_MAX_MS = 900000;
inline uint32_t storageRetryDelay(uint8_t& failures) {
  if (failures < 10) ++failures;
  uint32_t delay = TX_RETRY_MS << (failures - 1);
  return delay < STORAGE_RETRY_MAX_MS ? delay : STORAGE_RETRY_MAX_MS;
}
static const uint8_t TX_ATTEMPTS = 3;
static const uint32_t FLOOD_WAIT_BASE_MS = 500;
static const uint8_t FLOOD_WAIT_FACTOR = 16;
static const uint32_t ABORT_INTERVAL_MS = 30000;
static const uint32_t ABORT_DEADLINE_MS = 300000;

enum TxEnd : uint8_t {
  TX_QUIET,
  TX_ABORTED,
  TX_ABORT_FAILED,
  TX_SIGN_FAILED,
};

typedef bool (*TxSignFn)(const uint8_t* data, size_t len,
                         uint8_t signature[SIGNATURE_LEN], void* context);
typedef bool (*TxSendFn)(const uint8_t* frame, size_t len, bool scoped,
                         const uint8_t key[16], uint32_t& id,
                         uint32_t& airtime_ms, void* context);
typedef void (*TxFreeFn)(uint8_t* data, void* context);
typedef bool (*TxEndFn)(uint8_t dataset, TxEnd ending,
                        uint8_t notices, void* context);

struct TxOps {
  TxSignFn sign;
  TxSendFn send;
  TxFreeFn release;
  TxEndFn finish;
  void* context;
};

struct TxStart {
  uint8_t dataset;
  uint32_t generation;
  uint8_t interval_hours;
  uint8_t duration_days;
  bool scoped;
  uint8_t key[16];
  uint8_t manifest[MANIFEST_LEN];
  uint8_t* data;
  uint16_t data_len;
  bool one_shot;
};

struct TxState {
  bool active;
  bool aborting;
  uint8_t dataset;
  uint32_t generation;
  uint8_t interval_hours;
  uint8_t duration_days;
  bool scoped;
  uint8_t key[16];
  uint8_t manifest[MANIFEST_LEN];
  uint8_t* data;
  uint16_t data_len;
  uint8_t chunks;
  uint8_t rounds;
  uint8_t rounds_done;
  bool one_shot;
  bool round_open;
  uint8_t frame;
  uint8_t attempts;
  bool sign_failed;
  uint8_t finish_failures;
  uint32_t started;
  uint32_t retry_at;
  bool in_flight;
  bool abort_flight;
  uint32_t packet_id;
  uint32_t sent_at;
  uint32_t flight_airtime;
  uint8_t abort_frame[ABORT_LEN];
  uint8_t abort_sent;
  uint32_t abort_next;
  uint32_t abort_deadline;
};

class Transmitter {
 public:
  explicit Transmitter(const TxOps& ops);
  ~Transmitter();

  bool begin(const TxStart& start, uint32_t now);
  bool abort(uint8_t dataset, const uint8_t manifest_hash[DIGEST_LEN],
             bool scoped, const uint8_t key[16], uint32_t now);
  void tick(uint32_t now);
  void complete(uint32_t id, bool success, uint32_t now);
  void discard(uint8_t dataset);
  bool active(uint8_t dataset) const;
  bool aborting(uint8_t dataset) const;
  const TxState* state(uint8_t dataset) const;

 private:
  TxOps ops_;
  TxState slot_[SYNC_SETTINGS_DATASET_COUNT];

  TxState* find(uint8_t dataset);
  const TxState* find(uint8_t dataset) const;
  void clear(TxState& state);
  void advance(TxState& state);
  TxState* choose(uint32_t now);
  void send(TxState& state, uint32_t now);
  void sendAbort(TxState& state, uint32_t now);
};

}  // namespace sync
}  // namespace mobmesh
