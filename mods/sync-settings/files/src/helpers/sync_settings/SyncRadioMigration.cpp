#include "SyncRadioMigration.h"

#include <string.h>

namespace mobmesh {
namespace sync {

static bool reached(uint32_t now, uint32_t target) {
  return (int32_t)(now - target) >= 0;
}

RadioMigration::RadioMigration(RadioRecord& record, const RadioMigrationOps& ops)
    : record_(record), ops_(ops), state_(RADIO_MIG_IDLE), sync_ms_(0),
      sync_epoch_(0), cutover_ms_(0), test_start_ms_(0), test_end_ms_(0),
      confirm_end_ms_(0), confirmed_(false), trial_fallback_(false) {
  memset(&stage_, 0, sizeof(stage_));
  memset(&prior_, 0, sizeof(prior_));
  memset(&target_, 0, sizeof(target_));
}

bool RadioMigration::save(const RadioRecord& next) {
  if (ops_.persist == nullptr || !ops_.persist(next, ops_.context)) return false;
  record_ = next;
  return true;
}

bool RadioMigration::sameStage(const RadioStage& next) const {
  return stage_.publisher_id == next.publisher_id &&
         radioSamePlan(stage_.payload, next.payload) &&
         stage_.channel_len == next.channel_len &&
         memcmp(stage_.channel, next.channel, next.channel_len) == 0 &&
         stage_.scoped == next.scoped &&
         (!next.scoped || memcmp(stage_.route_key, next.route_key, 16) == 0) &&
         memcmp(stage_.target_digest, next.target_digest, DIGEST_LEN) == 0;
}

bool RadioMigration::nextTest(uint32_t now_ms) {
  RadioPayload value = stage_.payload;
  value.stamp = sync_epoch_ + (uint32_t)(now_ms - sync_ms_) / 1000u;
  return radioNextTestWindow(value, now_ms, test_start_ms_, test_end_ms_);
}

RadioMigrationResult RadioMigration::stage(const RadioStage& next,
                                           const RadioValues& current,
                                           uint32_t now_ms) {
  if (next.publisher_id == 0 || next.generation == 0 ||
      next.channel_len == 0 || next.channel_len > CHANNEL_MAX ||
      next.channel[next.channel_len] != 0 ||
      !radioValuesValid(current, false) ||
      !radioPayloadValid(next.payload, MOBMESH_BUILD_EPOCH)) {
    return RADIO_MIG_MALFORMED;
  }
  uint32_t cutover;
  if (!radioLocalDeadline(next.payload.stamp, next.payload.cutover,
                          now_ms, cutover)) return RADIO_MIG_EXPIRED;

  if (active()) {
    if (!sameStage(next)) return RADIO_MIG_LOCKED;
    if (next.generation <= stage_.generation) return RADIO_MIG_REPLAY;
    stage_.generation = next.generation;
    stage_.payload.stamp = next.payload.stamp;
    sync_ms_ = now_ms;
    sync_epoch_ = next.payload.stamp;
    cutover_ms_ = cutover;
    if (state_ == RADIO_MIG_ARMED_R1) nextTest(now_ms);
    return RADIO_MIG_OK;
  }

  stage_ = next;
  prior_ = current;
  target_ = next.payload.target;
  target_.cr = radioResolvedCr(next.payload.target, current);
  sync_ms_ = now_ms;
  sync_epoch_ = next.payload.stamp;
  cutover_ms_ = cutover;
  test_start_ms_ = 0;
  test_end_ms_ = 0;
  confirm_end_ms_ = cutover +
      (uint32_t)next.payload.schedule.confirm_window * 60000u;
  confirmed_ = false;
  trial_fallback_ = false;
  state_ = RADIO_MIG_ARMED_R1;
  if (radioClassify(current, next.payload.target) == RADIO_RETUNE) nextTest(now_ms);
  return RADIO_MIG_OK;
}

RadioMigrationResult RadioMigration::confirm(
    uint64_t migration_id, const uint8_t target_digest[DIGEST_LEN],
    uint32_t cutover) {
  if (state_ != RADIO_MIG_CONFIRM_R2) return RADIO_MIG_TOO_LATE;
  if (migration_id != stage_.payload.migration_id ||
      cutover != stage_.payload.cutover || target_digest == nullptr ||
      memcmp(target_digest, stage_.target_digest, DIGEST_LEN) != 0) {
    return RADIO_MIG_LOCKED;
  }
  confirmed_ = true;
  return RADIO_MIG_OK;
}

RadioMigrationResult RadioMigration::abort(uint64_t migration_id) {
  if (!active()) return RADIO_MIG_TOO_LATE;
  if (migration_id != stage_.payload.migration_id) return RADIO_MIG_LOCKED;
  if (state_ == RADIO_MIG_COMMITTING) return RADIO_MIG_TOO_LATE;
  if (state_ == RADIO_MIG_TEST_R2 || state_ == RADIO_MIG_TEST_PENDING ||
      state_ == RADIO_MIG_CONFIRM_R2 || state_ == RADIO_MIG_CUTOVER_PENDING) {
    if (ops_.temporary == nullptr ||
        !ops_.temporary(prior_, 1, ops_.context)) {
      state_ = RADIO_MIG_FAULT;
      return RADIO_MIG_NATIVE;
    }
    trial_fallback_ = false;
    state_ = RADIO_MIG_FALLBACK;
    return RADIO_MIG_OK;
  }
  state_ = RADIO_MIG_IDLE;
  return RADIO_MIG_OK;
}

RadioMigrationResult RadioMigration::recover() {
  if (record_.phase != RADIO_GUARD_COMMIT_PENDING ||
      record_.role != RADIO_ROLE_RECEIVER) return RADIO_MIG_OK;
  if (ops_.read == nullptr) return RADIO_MIG_NATIVE;

  RadioValues target = record_.target;
  target.cr = record_.resolved_cr;
  RadioValues current;
  if (!ops_.read(current, ops_.context)) return RADIO_MIG_NATIVE;

  RadioRecord next = record_;
  next.phase = RADIO_GUARD_IDLE;
  if (radioValuesEqual(current, target)) {
    next.result = RADIO_RESULT_COMMITTED;
    return save(next) ? RADIO_MIG_OK : RADIO_MIG_STORAGE;
  }

  if (ops_.commit == nullptr || !ops_.commit(record_.prior, ops_.context) ||
      !ops_.read(current, ops_.context) ||
      !radioValuesEqual(current, record_.prior)) return RADIO_MIG_NATIVE;
  next.result = RADIO_RESULT_FALLBACK;
  if (!save(next)) return RADIO_MIG_STORAGE;
  if (ops_.reboot != nullptr) ops_.reboot(ops_.context);
  return RADIO_MIG_OK;
}

RadioRecord RadioMigration::retained(uint8_t phase, uint8_t result) const {
  RadioRecord next = record_;
  next.phase = phase;
  next.role = RADIO_ROLE_RECEIVER;
  next.publisher_id = stage_.publisher_id;
  next.migration_id = stage_.payload.migration_id;
  next.target = stage_.payload.target;
  next.resolved_cr = target_.cr;
  next.prior = prior_;
  memcpy(next.target_digest, stage_.target_digest, DIGEST_LEN);
  next.start = stage_.payload.start;
  next.cutover = stage_.payload.cutover;
  next.captured = stage_.payload.schedule;
  next.channel_len = stage_.channel_len;
  memset(next.channel, 0, sizeof(next.channel));
  memcpy(next.channel, stage_.channel, stage_.channel_len);
  next.route_kind = stage_.scoped ? 1 : 0;
  memset(next.route_key, 0, sizeof(next.route_key));
  if (stage_.scoped) memcpy(next.route_key, stage_.route_key, 16);
  next.latest_generation = stage_.generation;
  next.flags = confirmed_ ? RADIO_FLAG_CONFIRM_RX : 0;
  next.result = result;
  return next;
}

void RadioMigration::finish(uint8_t result, bool reboot) {
  RadioRecord next = retained(RADIO_GUARD_IDLE, result);
  if (!save(next)) {
    state_ = RADIO_MIG_FAULT;
    return;
  }
  state_ = RADIO_MIG_COMMITTED;
  if (reboot && ops_.reboot != nullptr) ops_.reboot(ops_.context);
}

void RadioMigration::beginCommit(uint8_t result) {
  RadioRecord next = retained(RADIO_GUARD_COMMIT_PENDING, RADIO_RESULT_NONE);
  if (!save(next)) {
    state_ = RADIO_MIG_FAULT;
    return;
  }
  state_ = RADIO_MIG_COMMITTING;
  if (ops_.commit == nullptr || !ops_.commit(target_, ops_.context)) {
    state_ = RADIO_MIG_FAULT;
    return;
  }
  finish(result, true);
}

void RadioMigration::tick(uint32_t now_ms, const RadioValues& live,
                          bool temporary) {
  if (state_ == RADIO_MIG_ARMED_R1) {
    if (reached(now_ms, cutover_ms_)) {
      RadioClass kind = radioClassify(prior_, stage_.payload.target);
      if (kind == RADIO_IDENTICAL) {
        finish(RADIO_RESULT_IDENTICAL, false);
      } else if (kind == RADIO_CR_ONLY) {
        beginCommit(RADIO_RESULT_CR_ONLY);
      } else if (ops_.temporary == nullptr ||
                 !ops_.temporary(target_, stage_.payload.schedule.confirm_window,
                                 ops_.context)) {
        state_ = RADIO_MIG_FAULT;
      } else {
        state_ = RADIO_MIG_CUTOVER_PENDING;
      }
    } else if (test_start_ms_ != 0 && reached(now_ms, test_start_ms_)) {
      if (ops_.temporary == nullptr ||
          !ops_.temporary(target_, stage_.payload.schedule.test_window,
                          ops_.context)) {
        state_ = RADIO_MIG_FAULT;
      } else {
        state_ = RADIO_MIG_TEST_PENDING;
      }
    }
    return;
  }

  if (state_ == RADIO_MIG_TEST_PENDING || state_ == RADIO_MIG_TEST_R2) {
    if (radioValuesEqual(live, target_) && temporary) state_ = RADIO_MIG_TEST_R2;
    if (reached(now_ms, test_end_ms_)) {
      trial_fallback_ = true;
      state_ = RADIO_MIG_FALLBACK;
    }
    return;
  }

  if (state_ == RADIO_MIG_CUTOVER_PENDING || state_ == RADIO_MIG_CONFIRM_R2) {
    if (radioValuesEqual(live, target_) && temporary) state_ = RADIO_MIG_CONFIRM_R2;
    if (reached(now_ms, confirm_end_ms_)) {
      if (state_ == RADIO_MIG_CONFIRM_R2 && confirmed_) {
        beginCommit(RADIO_RESULT_COMMITTED);
      } else {
        trial_fallback_ = false;
        state_ = RADIO_MIG_FALLBACK;
      }
    }
    return;
  }

  if (state_ == RADIO_MIG_FALLBACK && radioValuesEqual(live, prior_) && !temporary) {
    if (trial_fallback_) {
      state_ = RADIO_MIG_ARMED_R1;
      test_start_ms_ = 0;
      test_end_ms_ = 0;
      nextTest(now_ms);
    } else {
      state_ = RADIO_MIG_IDLE;
    }
  }
}

}  // namespace sync
}  // namespace mobmesh
