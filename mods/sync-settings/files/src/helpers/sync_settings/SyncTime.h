#pragma once

#include "SyncStore.h"

namespace mobmesh {
namespace sync {

static const size_t TIME_RECORD_LEGACY_LEN = 97;
static const size_t TIME_RECORD_LEN = 99;
static const uint16_t TIME_TOLERANCE_DEFAULT = 20;
static const uint16_t TIME_INTERVAL_HOURS_DEFAULT = 168;
static const uint16_t TIME_DURATION_DAYS_DEFAULT = 180;
static const uint16_t TIME_INTERVAL_HOURS_MAX = 720;
static const uint16_t TIME_DURATION_DAYS_MAX = 365;
static const uint16_t TIME_NTP_HOURS_DEFAULT = 168;
static const uint16_t TIME_NTP_HOURS_MAX = 8760;

struct TimeRecord {
  bool enabled;
  uint16_t tolerance;
  uint16_t interval_hours;
  uint16_t duration_days;
  uint32_t local_generation;
  uint8_t local_key[32];
  bool active;
  uint8_t route_kind;
  uint8_t route_key[16];
  uint8_t channel_len;
  char channel[CHANNEL_MAX + 1];
  uint32_t started;
  uint32_t next_sample;
  uint32_t ends;
  uint32_t interval_seconds;
  uint16_t captured_tolerance;
  uint16_t ntp_interval_hours;
};

enum TimeReceiveResult : uint8_t {
  TIME_RECEIVE_IGNORED,
  TIME_RECEIVE_UNTRUSTED,
  TIME_RECEIVE_SIGNATURE,
  TIME_RECEIVE_LOCKED,
  TIME_RECEIVE_REPLAY,
  TIME_RECEIVE_STORAGE,
  TIME_RECEIVE_WITHIN,
  TIME_RECEIVE_CORRECTED,
};

struct TimeReceiveOps {
  bool (*verify)(const uint8_t key[32], const uint8_t* data, size_t len,
                 const uint8_t signature[64], void* context);
  bool (*persist)(const Publishers& next, void* context);
  bool (*locked)(void* context);
  uint32_t (*clock_get)(void* context);
  void (*clock_set)(uint32_t epoch, void* context);
  HashFn hash;
  void* context;
};

struct TimeClockWatch {
  bool valid;
  bool established;
  uint32_t rtc;
  uint32_t ms;
};

static const int64_t TIME_JUMP_SECONDS = 60;

enum TimeStep : uint8_t {
  TIME_STEP_IDLE,
  TIME_STEP_WAIT,
  TIME_STEP_DUE,
  TIME_STEP_CLOSE,
};

int64_t timeDelta(uint32_t receiver_now, uint32_t publisher_epoch);
bool timeNeedsCorrection(uint32_t receiver_now, uint32_t publisher_epoch,
                         uint16_t tolerance_minutes);

int8_t timeTrustedPublisher(const Publishers& publishers,
                            const uint8_t fingerprint[FINGERPRINT_LEN], HashFn hash,
                            void* context);
TimeReceiveResult timeReceive(const uint8_t* frame, size_t len, const char* channel,
                              Publishers& publishers, uint32_t build_epoch,
                              const TimeReceiveOps& ops, int64_t& delta);

void defaultTimeRecord(TimeRecord& record);
StoreResult readTimeRecord(const uint8_t* data, size_t len, TimeRecord& out);
size_t writeTimeRecord(const TimeRecord& record, uint8_t* out, size_t capacity);

bool timeSettingsValid(uint16_t interval_hours, uint16_t duration_days);
bool timeGeneration(const TimeRecord& record, const uint8_t key[32], uint32_t now,
                    uint32_t& generation);
bool timeScheduleStart(TimeRecord& record, uint32_t now, bool scoped,
                       const uint8_t route_key[16], const char* channel);
TimeStep timeScheduleStep(const TimeRecord& record, uint32_t now);
void timeScheduleAdvance(TimeRecord& record, uint32_t now);
bool timeScheduleUnsent(const TimeRecord& record);
bool timeScheduleShift(TimeRecord& record, int64_t delta);
int64_t timeClockJump(TimeClockWatch& watch, uint32_t rtc, uint32_t ms);
bool timeClockCredible(const TimeRecord& record, uint32_t now, uint32_t build_epoch);
uint32_t timeNtpMark(const TimeRecord& record, uint32_t at);
bool timeNtpDue(const TimeRecord& record, bool refreshed, uint32_t refreshed_mark, uint32_t at);
uint32_t timeEstablishRetryMs(uint8_t failures, uint16_t ntp_interval_hours);
void timeScheduleClear(TimeRecord& record);

}  // namespace sync
}  // namespace mobmesh
