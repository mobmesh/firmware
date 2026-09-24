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
      confirm_end_ms_(0), confirmed_(false), trial_fallback_(false),
      fallback_result_(RADIO_RESULT_FALLBACK) {
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
    if (next.publisher_id != stage_.publisher_id) return RADIO_MIG_LOCKED;
    if (next.generation <= stage_.generation) return RADIO_MIG_REPLAY;
    if (sameStage(next)) {
      stage_.generation = next.generation;
      stage_.payload.stamp = next.payload.stamp;
      sync_ms_ = now_ms;
      sync_epoch_ = next.payload.stamp;
      cutover_ms_ = cutover;
      if (state_ == RADIO_MIG_ARMED_R1) nextTest(now_ms);
      return RADIO_MIG_OK;
    }
    if (state_ != RADIO_MIG_ARMED_R1 ||
        next.payload.migration_id == stage_.payload.migration_id) {
      return RADIO_MIG_LOCKED;
    }
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
  fallback_result_ = RADIO_RESULT_FALLBACK;
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
  if (record_.phase == RADIO_GUARD_COMMIT_PENDING) return RADIO_MIG_TOO_LATE;
  if (state_ == RADIO_MIG_TEST_R2 || state_ == RADIO_MIG_TEST_PENDING ||
      state_ == RADIO_MIG_CONFIRM_R2 || state_ == RADIO_MIG_CUTOVER_PENDING) {
    if (ops_.temporary == nullptr ||
        !ops_.temporary(prior_, 1, ops_.context)) {
      state_ = RADIO_MIG_FAULT;
      return RADIO_MIG_NATIVE;
    }
    trial_fallback_ = false;
    fallback_result_ = RADIO_RESULT_ABORTED;
    state_ = RADIO_MIG_FALLBACK;
    return RADIO_MIG_OK;
  }
  if (state_ == RADIO_MIG_FALLBACK) {
    trial_fallback_ = false;
    fallback_result_ = RADIO_RESULT_ABORTED;
    return RADIO_MIG_OK;
  }
  if (state_ != RADIO_MIG_ARMED_R1) return RADIO_MIG_NATIVE;
  RadioRecord next = retained(RADIO_GUARD_IDLE, RADIO_RESULT_ABORTED);
  if (!save(next)) {
    state_ = RADIO_MIG_FAULT;
    return RADIO_MIG_STORAGE;
  }
  state_ = RADIO_MIG_COMMITTED;
  return RADIO_MIG_OK;
}

RadioMigrationResult RadioMigration::recover() {
  if (record_.phase != RADIO_GUARD_COMMIT_PENDING) return RADIO_MIG_OK;
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
        fallback_result_ = RADIO_RESULT_FALLBACK;
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
      finish(fallback_result_, false);
    }
  }
}

static const uint32_t RADIO_TX_TIMEOUT_MS = 60000;
static const uint32_t RADIO_RETRY_MS = 2000;
static const uint32_t RADIO_ABORT_DEADLINE_MS = 300000;

RadioPublisher::RadioPublisher(RadioRecord& record,
                               const RadioPublisherOps& ops)
    : record_(record), ops_(ops), state_(RADIO_PUB_IDLE), sync_ms_(0),
      sync_epoch_(0), cutover_ms_(0), test_start_ms_(0), test_end_ms_(0),
      confirm_end_ms_(0), prepare_due_ms_(0), control_due_ms_(0),
      control_sent_ms_(0), control_id_(0), prepare_in_flight_(false),
      control_in_flight_(false), fallback_result_(RADIO_RESULT_FALLBACK) {
  memset(&payload_, 0, sizeof(payload_));
  memset(&target_, 0, sizeof(target_));
}

bool RadioPublisher::save(const RadioRecord& next) {
  if (ops_.persist == nullptr || !ops_.persist(next, ops_.context)) return false;
  record_ = next;
  return true;
}

uint32_t RadioPublisher::epochAt(uint32_t now_ms) const {
  return sync_epoch_ + (uint32_t)(now_ms - sync_ms_) / 1000u;
}

bool RadioPublisher::nextTest(uint32_t now_ms) {
  RadioPayload value = payload_;
  value.stamp = epochAt(now_ms);
  return radioNextTestWindow(value, now_ms, test_start_ms_, test_end_ms_);
}

RadioPublisherResult RadioPublisher::begin(const RadioPublishPlan& plan,
                                           uint32_t now_epoch,
                                           uint32_t now_ms) {
  if (active() || record_.phase != RADIO_GUARD_IDLE) return RADIO_PUB_BUSY;
  if (plan.payload.migration_id == 0 ||
      plan.payload.stamp != now_epoch || plan.channel_len == 0 ||
      plan.channel_len > CHANNEL_MAX || plan.channel[plan.channel_len] != 0 ||
      !radioValuesValid(plan.prior, false) ||
      !radioPayloadValid(plan.payload, MOBMESH_BUILD_EPOCH)) {
    return RADIO_PUB_MALFORMED;
  }

  uint32_t cutover;
  if (!radioLocalDeadline(now_epoch, plan.payload.cutover, now_ms, cutover)) {
    return RADIO_PUB_MALFORMED;
  }
  payload_ = plan.payload;
  target_ = plan.payload.target;
  target_.cr = radioResolvedCr(target_, plan.prior);
  sync_ms_ = now_ms;
  sync_epoch_ = now_epoch;
  cutover_ms_ = cutover;
  confirm_end_ms_ = cutover +
      (uint32_t)plan.payload.schedule.confirm_window * 60000u;
  prepare_due_ms_ = now_ms;
  control_due_ms_ = 0;
  control_sent_ms_ = 0;
  control_id_ = 0;
  prepare_in_flight_ = false;
  control_in_flight_ = false;
  fallback_result_ = RADIO_RESULT_FALLBACK;
  test_start_ms_ = 0;
  test_end_ms_ = 0;
  nextTest(now_ms);

  RadioRecord next = record_;
  next.phase = RADIO_GUARD_PUBLISHING;
  next.role = RADIO_ROLE_PUBLISHER;
  next.publisher_id = 0;
  next.migration_id = plan.payload.migration_id;
  next.target = plan.payload.target;
  next.resolved_cr = target_.cr;
  next.prior = plan.prior;
  memcpy(next.target_digest, plan.target_digest, DIGEST_LEN);
  next.start = plan.payload.start;
  next.cutover = plan.payload.cutover;
  next.captured = plan.payload.schedule;
  next.channel_len = plan.channel_len;
  memset(next.channel, 0, sizeof(next.channel));
  memcpy(next.channel, plan.channel, plan.channel_len);
  next.route_kind = plan.scoped ? 1 : 0;
  memset(next.route_key, 0, sizeof(next.route_key));
  if (plan.scoped) memcpy(next.route_key, plan.route_key, sizeof(next.route_key));
  next.latest_generation = 0;
  next.flags = 0;
  next.result = RADIO_RESULT_NONE;
  if (memcmp(next.local_key, plan.publisher_key, sizeof(next.local_key)) != 0) {
    next.local_generation = 0;
  }
  memcpy(next.local_key, plan.publisher_key, sizeof(next.local_key));
  if (!save(next)) return RADIO_PUB_STORAGE;
  state_ = RADIO_PUB_R1;
  return RADIO_PUB_OK;
}

RadioPublisherResult RadioPublisher::recover(uint32_t now_ms) {
  if (record_.role != RADIO_ROLE_PUBLISHER ||
      record_.phase == RADIO_GUARD_IDLE) return RADIO_PUB_OK;
  if (record_.phase == RADIO_GUARD_COMMIT_PENDING) return RADIO_PUB_OK;
  RadioRecord next = record_;
  next.phase = RADIO_GUARD_ABORT_PENDING;
  if (!save(next)) return RADIO_PUB_STORAGE;
  state_ = RADIO_PUB_ABORTING;
  confirm_end_ms_ = now_ms + RADIO_ABORT_DEADLINE_MS;
  control_due_ms_ = now_ms;
  return RADIO_PUB_OK;
}

bool RadioPublisher::startPrepare(uint32_t now_ms) {
  uint32_t stamp = epochAt(now_ms);
  uint32_t generation = stamp;
  if (record_.local_generation != 0) {
    if (record_.local_generation == 0xffffffffu) return false;
    uint32_t serial = record_.local_generation + 1u;
    if (serial > generation) generation = serial;
  }
  RadioRecord next = record_;
  next.local_generation = generation;
  next.latest_generation = generation;
  if (!save(next)) return false;
  RadioPayload value = payload_;
  value.stamp = stamp;
  if (ops_.prepare == nullptr ||
      !ops_.prepare(record_, value, generation, ops_.context)) {
    prepare_due_ms_ = now_ms + RADIO_RETRY_MS;
    return false;
  }
  prepare_in_flight_ = true;
  return true;
}

bool RadioPublisher::startControl(bool aborting, uint32_t now_ms) {
  uint32_t id = 0;
  if (ops_.control == nullptr ||
      !ops_.control(record_, aborting, id, ops_.context) || id == 0) {
    control_due_ms_ = now_ms + RADIO_RETRY_MS;
    return false;
  }
  control_id_ = id;
  control_sent_ms_ = now_ms;
  control_in_flight_ = true;
  if (!aborting) {
    control_due_ms_ = now_ms +
        (uint32_t)record_.captured.confirm_interval * 60000u;
  }
  return true;
}

void RadioPublisher::finish(uint8_t result, bool reboot) {
  RadioRecord next = record_;
  next.phase = RADIO_GUARD_IDLE;
  next.result = result;
  if (!save(next)) {
    state_ = RADIO_PUB_FAULT;
    return;
  }
  state_ = RADIO_PUB_COMPLETE;
  if (reboot && ops_.reboot != nullptr) ops_.reboot(ops_.context);
}

void RadioPublisher::beginCommit() {
  RadioRecord next = record_;
  next.phase = RADIO_GUARD_COMMIT_PENDING;
  next.result = RADIO_RESULT_NONE;
  if (!save(next)) {
    state_ = RADIO_PUB_FAULT;
    return;
  }
  state_ = RADIO_PUB_COMMITTING;
  if (ops_.commit == nullptr || !ops_.commit(target_, ops_.context)) {
    state_ = RADIO_PUB_FAULT;
    return;
  }
  finish(RADIO_RESULT_COMMITTED, true);
}

void RadioPublisher::beginFallback(uint8_t result) {
  fallback_result_ = result;
  if (ops_.temporary == nullptr ||
      !ops_.temporary(record_.prior, 1, ops_.context)) {
    state_ = RADIO_PUB_FAULT;
    return;
  }
  state_ = RADIO_PUB_FALLBACK;
}

RadioPublisherResult RadioPublisher::abort(uint32_t now_ms) {
  if (!active()) return RADIO_PUB_BUSY;
  if (record_.phase == RADIO_GUARD_COMMIT_PENDING) return RADIO_PUB_TOO_LATE;
  RadioRecord next = record_;
  next.phase = RADIO_GUARD_ABORT_PENDING;
  if (!save(next)) return RADIO_PUB_STORAGE;
  prepare_in_flight_ = false;
  control_in_flight_ = false;
  state_ = RADIO_PUB_ABORTING;
  control_due_ms_ = now_ms;
  confirm_end_ms_ = now_ms + RADIO_ABORT_DEADLINE_MS;
  return RADIO_PUB_OK;
}

void RadioPublisher::roundFinished(bool success, uint32_t now_ms) {
  if (!prepare_in_flight_) return;
  prepare_in_flight_ = false;
  if (!success) prepare_due_ms_ = now_ms + RADIO_RETRY_MS;
}

void RadioPublisher::complete(uint32_t packet_id, bool success,
                              uint32_t now_ms) {
  if (!control_in_flight_ || packet_id != control_id_) return;
  control_in_flight_ = false;
  if (state_ == RADIO_PUB_CONFIRM_R2) {
    if (success) {
      if ((record_.flags & RADIO_FLAG_CONFIRM_TX) == 0) {
        RadioRecord next = record_;
        next.flags |= RADIO_FLAG_CONFIRM_TX;
        if (!save(next)) state_ = RADIO_PUB_FAULT;
      }
    } else {
      control_due_ms_ = now_ms;
    }
  } else if (state_ == RADIO_PUB_ABORTING) {
    if (success) {
      fallback_result_ = RADIO_RESULT_ABORTED;
      control_due_ms_ = 0;
      state_ = RADIO_PUB_FALLBACK;
    }
    else control_due_ms_ = now_ms;
  }
}

void RadioPublisher::tick(uint32_t now_ms, const RadioValues& live,
                          bool temporary) {
  if (control_in_flight_ && reached(now_ms, control_sent_ms_ + RADIO_TX_TIMEOUT_MS)) {
    control_in_flight_ = false;
    control_due_ms_ = now_ms;
  }

  if (state_ == RADIO_PUB_R1) {
    if (reached(now_ms, cutover_ms_)) {
      if (prepare_in_flight_) return;
      if (ops_.temporary == nullptr ||
          !ops_.temporary(target_, record_.captured.confirm_window,
                          ops_.context)) {
        state_ = RADIO_PUB_FAULT;
      } else {
        RadioRecord next = record_;
        next.phase = RADIO_GUARD_CONFIRMING;
        if (!save(next)) state_ = RADIO_PUB_FAULT;
        else state_ = RADIO_PUB_CUTOVER_PENDING;
      }
      return;
    }
    if (test_start_ms_ != 0 && reached(now_ms, test_start_ms_)) {
      if (prepare_in_flight_) return;
      if (ops_.temporary == nullptr ||
          !ops_.temporary(target_, record_.captured.test_window,
                          ops_.context)) {
        state_ = RADIO_PUB_FAULT;
      } else {
        state_ = RADIO_PUB_TEST_PENDING;
      }
      return;
    }
    if (!prepare_in_flight_ && reached(now_ms, prepare_due_ms_)) {
      if (startPrepare(now_ms)) {
        prepare_due_ms_ = now_ms +
            (uint32_t)record_.captured.campaign_interval * 60000u;
      }
    }
    return;
  }

  if (state_ == RADIO_PUB_TEST_PENDING || state_ == RADIO_PUB_TEST_R2) {
    if (radioValuesEqual(live, target_) && temporary) state_ = RADIO_PUB_TEST_R2;
    if (reached(now_ms, test_end_ms_)) {
      if (ops_.temporary == nullptr ||
          !ops_.temporary(record_.prior, 1, ops_.context)) {
        state_ = RADIO_PUB_FAULT;
      } else {
        state_ = RADIO_PUB_TEST_FALLBACK;
      }
    }
    return;
  }

  if (state_ == RADIO_PUB_TEST_FALLBACK) {
    if (radioValuesEqual(live, record_.prior) && !temporary) {
      state_ = RADIO_PUB_R1;
      test_start_ms_ = 0;
      test_end_ms_ = 0;
      nextTest(now_ms);
    }
    return;
  }

  if (state_ == RADIO_PUB_CUTOVER_PENDING || state_ == RADIO_PUB_CONFIRM_R2) {
    if (radioValuesEqual(live, target_) && temporary) {
      state_ = RADIO_PUB_CONFIRM_R2;
      if (control_due_ms_ == 0) control_due_ms_ = now_ms;
    }
    if (reached(now_ms, confirm_end_ms_)) {
      control_in_flight_ = false;
      if (state_ == RADIO_PUB_CONFIRM_R2 &&
          (record_.flags & RADIO_FLAG_CONFIRM_TX) != 0 &&
          radioValuesEqual(live, target_) && temporary) beginCommit();
      else beginFallback(RADIO_RESULT_FALLBACK);
      return;
    }
    if (state_ == RADIO_PUB_CONFIRM_R2 && !control_in_flight_ &&
        reached(now_ms, control_due_ms_)) startControl(false, now_ms);
    return;
  }

  if (state_ == RADIO_PUB_ABORTING) {
    if (reached(now_ms, confirm_end_ms_)) {
      beginFallback(RADIO_RESULT_FAULT);
    } else if (!control_in_flight_ && reached(now_ms, control_due_ms_)) {
      startControl(true, now_ms);
    }
    return;
  }

  if (state_ == RADIO_PUB_FALLBACK &&
      radioValuesEqual(live, record_.prior) && !temporary) {
    finish(fallback_result_, false);
  } else if (state_ == RADIO_PUB_FALLBACK &&
             fallback_result_ != RADIO_RESULT_FALLBACK &&
             control_due_ms_ == 0) {
    if (ops_.temporary == nullptr ||
        !ops_.temporary(record_.prior, 1, ops_.context)) {
      state_ = RADIO_PUB_FAULT;
    } else {
      control_due_ms_ = 0xffffffffu;
    }
  }
}

}  // namespace sync
}  // namespace mobmesh
