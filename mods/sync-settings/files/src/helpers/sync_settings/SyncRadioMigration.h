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

}  // namespace sync
}  // namespace mobmesh
