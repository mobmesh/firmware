#include "SyncTime.h"

#include <string.h>

namespace mobmesh {
namespace sync {

static const uint8_t TIME_SCHEMA = 0x01;
static const uint32_t HOUR = 3600u;
static const uint32_t DAY = 86400u;

static uint16_t read16(const uint8_t* p) {
  return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static uint32_t read32(const uint8_t* p) {
  return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 |
         (uint32_t)p[2] << 8 | p[3];
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

static bool allZero(const uint8_t* data, size_t len) {
  for (size_t i = 0; i < len; ++i) {
    if (data[i] != 0) return false;
  }
  return true;
}

int64_t timeDelta(uint32_t receiver_now, uint32_t publisher_epoch) {
  return (int64_t)receiver_now - (int64_t)publisher_epoch;
}

bool timeNeedsCorrection(uint32_t receiver_now, uint32_t publisher_epoch,
                         uint16_t tolerance_minutes) {
  int64_t delta = timeDelta(receiver_now, publisher_epoch);
  if (delta < 0) delta = -delta;
  return delta > (int64_t)tolerance_minutes * 60;
}

int8_t timeTrustedPublisher(const Publishers& publishers,
                            const uint8_t fingerprint[FINGERPRINT_LEN], HashFn hash,
                            void* context) {
  int8_t found = -1;
  for (uint8_t i = 0; i < publishers.count && i < PUBLISHER_MAX; ++i) {
    const Publisher& candidate = publishers.record[i];
    if (candidate.status != PUBLISHER_ACTIVE) continue;
    uint8_t digest[STORE_DIGEST_LEN];
    hash(candidate.key, sizeof(candidate.key), digest, context);
    if (memcmp(digest, fingerprint, FINGERPRINT_LEN) != 0) continue;
    if (found >= 0) return -1;
    found = (int8_t)i;
  }
  return found;
}

TimeReceiveResult timeReceive(const uint8_t* frame, size_t len, const char* channel,
                              Publishers& publishers, uint32_t build_epoch,
                              const TimeReceiveOps& ops, int64_t& delta) {
  TimeSampleView value;
  if (channel == nullptr || channel[0] == 0 ||
      readTimeSample(frame, len, build_epoch, value) != WIRE_OK ||
      strcmp(value.channel, channel) != 0) return TIME_RECEIVE_IGNORED;
  int8_t index = timeTrustedPublisher(publishers, value.publisher, ops.hash, ops.context);
  if (index < 0) return TIME_RECEIVE_UNTRUSTED;
  Publisher& publisher = publishers.record[index];
  if (!ops.verify(publisher.key, frame, TIME_SAMPLE_SIGNED_LEN, value.signature, ops.context)) {
    return TIME_RECEIVE_SIGNATURE;
  }
  if (ops.locked(ops.context)) return TIME_RECEIVE_LOCKED;
  if (value.generation <= publisher.time_generation) return TIME_RECEIVE_REPLAY;
  Publishers next = publishers;
  next.record[index].time_generation = value.generation;
  if (!ops.persist(next, ops.context)) return TIME_RECEIVE_STORAGE;
  publishers = next;
  uint32_t now = ops.clock_get(ops.context);
  delta = timeDelta(now, value.epoch);
  if (!timeNeedsCorrection(now, value.epoch, value.tolerance)) return TIME_RECEIVE_WITHIN;
  ops.clock_set(value.epoch, ops.context);
  return TIME_RECEIVE_CORRECTED;
}

bool timeSettingsValid(uint16_t interval_hours, uint16_t duration_days) {
  return interval_hours >= 1 && interval_hours <= TIME_INTERVAL_HOURS_MAX &&
         duration_days >= 1 && duration_days <= TIME_DURATION_DAYS_MAX &&
         (uint32_t)interval_hours <= (uint32_t)duration_days * 24u;
}

void defaultTimeRecord(TimeRecord& record) {
  memset(&record, 0, sizeof(record));
  record.tolerance = TIME_TOLERANCE_DEFAULT;
  record.interval_hours = TIME_INTERVAL_HOURS_DEFAULT;
  record.duration_days = TIME_DURATION_DAYS_DEFAULT;
}

static StoreResult timeRecordValid(const TimeRecord& value) {
  if (value.tolerance > TIME_TOLERANCE_MAX ||
      !timeSettingsValid(value.interval_hours, value.duration_days) ||
      (value.local_generation == 0 && !allZero(value.local_key, sizeof(value.local_key)))) {
    return STORE_PAYLOAD;
  }
  if (!value.active) {
    return value.route_kind == 0 && allZero(value.route_key, sizeof(value.route_key)) &&
           value.channel_len == 0 && value.started == 0 && value.next_sample == 0 &&
           value.ends == 0 && value.interval_seconds == 0 &&
           value.captured_tolerance == 0 ? STORE_OK : STORE_PAYLOAD;
  }
  bool scoped = value.route_kind == 1;
  uint32_t span = value.ends - value.started;
  if (value.route_kind > 1 || scoped == allZero(value.route_key, sizeof(value.route_key)) ||
      value.channel_len == 0 || value.channel_len > CHANNEL_MAX ||
      !validChannel((const uint8_t*)value.channel, value.channel_len) ||
      value.started == 0 || value.next_sample < value.started ||
      value.ends <= value.started || span > TIME_DURATION_DAYS_MAX * DAY ||
      value.interval_seconds < HOUR || value.interval_seconds % HOUR != 0 ||
      value.interval_seconds > TIME_INTERVAL_HOURS_MAX * HOUR ||
      value.interval_seconds > span || value.captured_tolerance == 0 ||
      value.captured_tolerance > TIME_TOLERANCE_MAX) {
    return STORE_PAYLOAD;
  }
  return STORE_OK;
}

StoreResult readTimeRecord(const uint8_t* data, size_t len, TimeRecord& out) {
  if (data == nullptr || len != TIME_RECORD_LEN || data[0] != TIME_SCHEMA ||
      data[1] > 1 || data[44] > 1 || data[62] > CHANNEL_MAX ||
      !allZero(data + 63 + data[62], CHANNEL_MAX - data[62])) return STORE_PAYLOAD;
  TimeRecord value;
  memset(&value, 0, sizeof(value));
  value.enabled = data[1] != 0;
  value.tolerance = read16(data + 2);
  value.interval_hours = read16(data + 4);
  value.duration_days = read16(data + 6);
  value.local_generation = read32(data + 8);
  memcpy(value.local_key, data + 12, sizeof(value.local_key));
  value.active = data[44] != 0;
  value.route_kind = data[45];
  memcpy(value.route_key, data + 46, sizeof(value.route_key));
  value.channel_len = data[62];
  memcpy(value.channel, data + 63, value.channel_len);
  value.started = read32(data + 79);
  value.next_sample = read32(data + 83);
  value.ends = read32(data + 87);
  value.interval_seconds = read32(data + 91);
  value.captured_tolerance = read16(data + 95);
  StoreResult result = timeRecordValid(value);
  if (result == STORE_OK) out = value;
  return result;
}

size_t writeTimeRecord(const TimeRecord& record, uint8_t* out, size_t capacity) {
  if (out == nullptr || capacity < TIME_RECORD_LEN ||
      timeRecordValid(record) != STORE_OK) return 0;
  memset(out, 0, TIME_RECORD_LEN);
  out[0] = TIME_SCHEMA;
  out[1] = record.enabled ? 1 : 0;
  write16(out + 2, record.tolerance);
  write16(out + 4, record.interval_hours);
  write16(out + 6, record.duration_days);
  write32(out + 8, record.local_generation);
  memcpy(out + 12, record.local_key, sizeof(record.local_key));
  out[44] = record.active ? 1 : 0;
  out[45] = record.route_kind;
  memcpy(out + 46, record.route_key, sizeof(record.route_key));
  out[62] = record.channel_len;
  memcpy(out + 63, record.channel, record.channel_len);
  write32(out + 79, record.started);
  write32(out + 83, record.next_sample);
  write32(out + 87, record.ends);
  write32(out + 91, record.interval_seconds);
  write16(out + 95, record.captured_tolerance);
  return TIME_RECORD_LEN;
}

bool timeGeneration(const TimeRecord& record, const uint8_t key[32], uint32_t now,
                    uint32_t& generation) {
  if (key == nullptr || now == 0) return false;
  generation = now;
  if (record.local_generation == 0 ||
      memcmp(record.local_key, key, sizeof(record.local_key)) != 0) return true;
  if (record.local_generation == 0xffffffffu) return false;
  uint32_t next = record.local_generation + 1;
  if (next > generation) generation = next;
  return true;
}

bool timeScheduleStart(TimeRecord& record, uint32_t now, bool scoped,
                       const uint8_t route_key[16], const char* channel) {
  size_t channel_len = channel == nullptr ? 0 : strnlen(channel, CHANNEL_MAX + 1);
  uint64_t ends = (uint64_t)now + (uint64_t)record.duration_days * DAY;
  if (now == 0 || record.tolerance == 0 || record.tolerance > TIME_TOLERANCE_MAX ||
      !timeSettingsValid(record.interval_hours, record.duration_days) ||
      ends >= 0xffffffffull || channel_len == 0 || channel_len > CHANNEL_MAX ||
      !validChannel((const uint8_t*)channel, (uint8_t)channel_len) ||
      (scoped && (route_key == nullptr || allZero(route_key, 16)))) return false;
  TimeRecord next = record;
  timeScheduleClear(next);
  next.active = true;
  next.route_kind = scoped ? 1 : 0;
  if (scoped) memcpy(next.route_key, route_key, sizeof(next.route_key));
  next.channel_len = (uint8_t)channel_len;
  memcpy(next.channel, channel, channel_len);
  next.started = now;
  next.next_sample = now;
  next.ends = (uint32_t)ends;
  next.interval_seconds = (uint32_t)record.interval_hours * HOUR;
  next.captured_tolerance = record.tolerance;
  record = next;
  return true;
}

TimeStep timeScheduleStep(const TimeRecord& record, uint32_t now) {
  if (!record.active) return TIME_STEP_IDLE;
  if (now > record.ends || record.next_sample > record.ends) return TIME_STEP_CLOSE;
  if (now < record.next_sample) return TIME_STEP_WAIT;
  return TIME_STEP_DUE;
}

void timeScheduleAdvance(TimeRecord& record, uint32_t now) {
  if (!record.active || record.interval_seconds == 0) return;
  uint64_t elapsed = now > record.started ? (uint64_t)now - record.started : 0;
  uint64_t next = (uint64_t)record.started +
                  (elapsed / record.interval_seconds + 1) * record.interval_seconds;
  record.next_sample = next > 0xffffffffull ? 0xffffffffu : (uint32_t)next;
}

bool timeScheduleUnsent(const TimeRecord& record) {
  return record.active && record.next_sample == record.started;
}

bool timeScheduleShift(TimeRecord& record, int64_t delta) {
  if (!record.active) return false;
  int64_t started = (int64_t)record.started + delta;
  int64_t next = (int64_t)record.next_sample + delta;
  int64_t ends = (int64_t)record.ends + delta;
  if (started <= 0 || ends >= 0xffffffffLL || next > 0xffffffffLL) return false;
  record.started = (uint32_t)started;
  record.next_sample = (uint32_t)next;
  record.ends = (uint32_t)ends;
  return true;
}

int64_t timeClockJump(TimeClockWatch& watch, uint32_t rtc, uint32_t ms) {
  int64_t jump = 0;
  if (watch.valid && watch.established) {
    int64_t expected = (int64_t)watch.rtc + (uint32_t)(ms - watch.ms) / 1000u;
    jump = (int64_t)rtc - expected;
    if (jump <= TIME_JUMP_SECONDS && jump >= -TIME_JUMP_SECONDS) jump = 0;
  }
  watch.valid = true;
  watch.rtc = rtc;
  watch.ms = ms;
  return jump;
}

bool timeClockCredible(const TimeRecord& record, uint32_t now, uint32_t build_epoch) {
  if (now < build_epoch) return false;
  return !record.active || timeScheduleUnsent(record) || now >= record.started;
}

void timeScheduleClear(TimeRecord& record) {
  record.active = false;
  record.route_kind = 0;
  memset(record.route_key, 0, sizeof(record.route_key));
  record.channel_len = 0;
  memset(record.channel, 0, sizeof(record.channel));
  record.started = 0;
  record.next_sample = 0;
  record.ends = 0;
  record.interval_seconds = 0;
  record.captured_tolerance = 0;
}

static_assert(STORE_OVERHEAD + TIME_RECORD_LEN == 141, "time record bound changed");

}  // namespace sync
}  // namespace mobmesh
