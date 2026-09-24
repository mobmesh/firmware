#pragma once

#include "SyncStore.h"

namespace mobmesh {
namespace sync {

enum RadioMigrationState : uint8_t {
  RADIO_MIG_IDLE,
  RADIO_MIG_ARMED_R1,
  RADIO_MIG_TEST_PENDING,
  RADIO_MIG_TEST_R2,
  RADIO_MIG_CUTOVER_PENDING,
  RADIO_MIG_CONFIRM_R2,
  RADIO_MIG_COMMITTING,
  RADIO_MIG_COMMITTED,
  RADIO_MIG_FALLBACK,
  RADIO_MIG_FAULT,
};

enum RadioMigrationResult : uint8_t {
  RADIO_MIG_OK,
  RADIO_MIG_REPLAY,
  RADIO_MIG_LOCKED,
  RADIO_MIG_EXPIRED,
  RADIO_MIG_STORAGE,
  RADIO_MIG_NATIVE,
  RADIO_MIG_MALFORMED,
  RADIO_MIG_TOO_LATE,
};

struct RadioStage {
  RadioPayload payload;
  uint16_t publisher_id;
  uint32_t generation;
  uint8_t target_digest[DIGEST_LEN];
  uint8_t channel_len;
  char channel[CHANNEL_MAX + 1];
  bool scoped;
  uint8_t route_key[16];
};

typedef bool (*RadioPersistFn)(const RadioRecord& record, void* context);
typedef bool (*RadioTempFn)(const RadioValues& target, uint16_t minutes,
                            void* context);
typedef bool (*RadioCommitFn)(const RadioValues& target, void* context);
typedef bool (*RadioReadFn)(RadioValues& value, void* context);
typedef void (*RadioRebootFn)(void* context);

struct RadioMigrationOps {
  RadioPersistFn persist;
  RadioTempFn temporary;
  RadioCommitFn commit;
  RadioReadFn read;
  RadioRebootFn reboot;
  void* context;
};

class RadioMigration {
 public:
  RadioMigration(RadioRecord& record, const RadioMigrationOps& ops);

  RadioMigrationResult stage(const RadioStage& stage,
                             const RadioValues& current, uint32_t now_ms);
  RadioMigrationResult confirm(uint64_t migration_id,
                               const uint8_t target_digest[DIGEST_LEN],
                               uint32_t cutover);
  RadioMigrationResult abort(uint64_t migration_id);
  RadioMigrationResult recover();
  void tick(uint32_t now_ms, const RadioValues& live, bool temporary);

  RadioMigrationState state() const { return state_; }
  bool active() const { return state_ != RADIO_MIG_IDLE &&
                               state_ != RADIO_MIG_COMMITTED; }
  bool awaitingCutover() const {
    return state_ == RADIO_MIG_ARMED_R1 || state_ == RADIO_MIG_TEST_PENDING ||
           state_ == RADIO_MIG_TEST_R2 ||
           (state_ == RADIO_MIG_FALLBACK && trial_fallback_) ||
           state_ == RADIO_MIG_CUTOVER_PENDING;
  }
  const RadioStage& staged() const { return stage_; }

 private:
  RadioRecord& record_;
  RadioMigrationOps ops_;
  RadioStage stage_;
  RadioValues prior_;
  RadioValues target_;
  RadioMigrationState state_;
  uint32_t sync_ms_;
  uint32_t sync_epoch_;
  uint32_t cutover_ms_;
  uint32_t test_start_ms_;
  uint32_t test_end_ms_;
  uint32_t confirm_end_ms_;
  bool confirmed_;
  bool trial_fallback_;

  bool save(const RadioRecord& next);
  bool sameStage(const RadioStage& next) const;
  bool nextTest(uint32_t now_ms);
  RadioRecord retained(uint8_t phase, uint8_t result) const;
  void finish(uint8_t result, bool reboot);
  void beginCommit(uint8_t result);
};

enum RadioPublisherState : uint8_t {
  RADIO_PUB_IDLE,
  RADIO_PUB_R1,
  RADIO_PUB_TEST_PENDING,
  RADIO_PUB_TEST_R2,
  RADIO_PUB_TEST_FALLBACK,
  RADIO_PUB_CUTOVER_PENDING,
  RADIO_PUB_CONFIRM_R2,
  RADIO_PUB_COMMITTING,
  RADIO_PUB_FALLBACK,
  RADIO_PUB_ABORTING,
  RADIO_PUB_COMPLETE,
  RADIO_PUB_FAULT,
};

enum RadioPublisherResult : uint8_t {
  RADIO_PUB_OK,
  RADIO_PUB_BUSY,
  RADIO_PUB_STORAGE,
  RADIO_PUB_NATIVE,
  RADIO_PUB_SEND,
  RADIO_PUB_TOO_LATE,
  RADIO_PUB_MALFORMED,
};

struct RadioPublishPlan {
  RadioPayload payload;
  RadioValues prior;
  uint64_t migration_id;
  uint8_t target_digest[DIGEST_LEN];
  uint8_t publisher_key[32];
  uint8_t channel_len;
  char channel[CHANNEL_MAX + 1];
  bool scoped;
  uint8_t route_key[16];
};

typedef bool (*RadioPrepareFn)(const RadioRecord& record,
                               const RadioPayload& payload,
                               uint32_t generation, void* context);
typedef bool (*RadioControlFn)(const RadioRecord& record, bool abort,
                               uint32_t& packet_id, void* context);

struct RadioPublisherOps {
  RadioPersistFn persist;
  RadioTempFn temporary;
  RadioCommitFn commit;
  RadioReadFn read;
  RadioPrepareFn prepare;
  RadioControlFn control;
  RadioRebootFn reboot;
  void* context;
};

class RadioPublisher {
 public:
  RadioPublisher(RadioRecord& record, const RadioPublisherOps& ops);

  RadioPublisherResult begin(const RadioPublishPlan& plan, uint32_t now_epoch,
                             uint32_t now_ms);
  RadioPublisherResult recover(uint32_t now_ms);
  RadioPublisherResult abort(uint32_t now_ms);
  void tick(uint32_t now_ms, const RadioValues& live, bool temporary);
  void roundFinished(bool success, uint32_t now_ms);
  void complete(uint32_t packet_id, bool success, uint32_t now_ms);

  RadioPublisherState state() const { return state_; }
  bool active() const { return state_ != RADIO_PUB_IDLE &&
                               state_ != RADIO_PUB_COMPLETE; }

 private:
  RadioRecord& record_;
  RadioPublisherOps ops_;
  RadioPayload payload_;
  RadioValues target_;
  RadioPublisherState state_;
  uint32_t sync_ms_;
  uint32_t sync_epoch_;
  uint32_t cutover_ms_;
  uint32_t test_start_ms_;
  uint32_t test_end_ms_;
  uint32_t confirm_end_ms_;
  uint32_t prepare_due_ms_;
  uint32_t control_due_ms_;
  uint32_t control_sent_ms_;
  uint32_t control_id_;
  bool prepare_in_flight_;
  bool control_in_flight_;
  bool confirmation_sent_;
  uint8_t fallback_result_;

  bool save(const RadioRecord& next);
  bool nextTest(uint32_t now_ms);
  uint32_t epochAt(uint32_t now_ms) const;
  bool startPrepare(uint32_t now_ms);
  bool startControl(bool abort, uint32_t now_ms);
  void finish(uint8_t result, bool reboot);
  void beginCommit();
  void beginFallback(uint8_t result);
};

}  // namespace sync
}  // namespace mobmesh
