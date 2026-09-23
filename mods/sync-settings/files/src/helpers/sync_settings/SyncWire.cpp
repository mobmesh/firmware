#include "SyncWire.h"

#include <math.h>
#include <string.h>

namespace mobmesh {
namespace sync {

static const uint8_t MAGIC[] = {0x53, 0x59};
static const uint8_t RESET_BIT = 0x01;
static const uint8_t DAYS_MASK = 0x06;
static const uint8_t RESERVED_MASK = 0xF8;

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

static bool knownDataset(uint8_t dataset) {
  return dataset == REGION || dataset == POLICY || dataset == RADIO;
}

// Payload formats the wire layer will parse; the receiver decides what it can decode.
static bool knownFormat(uint8_t format) {
  return format == FORMAT_RAW || format == FORMAT_DEFLATE;
}

static bool channelChar(char c) {
  return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
}

bool validChannel(const uint8_t* channel, uint8_t len) {
  if (channel == nullptr || len == 0 || len > CHANNEL_MAX) {
    return false;
  }
  for (uint8_t i = 0; i < len; ++i) {
    if (!channelChar((char)channel[i])) return false;
  }
  return true;
}

bool normalizeChannel(const char* value, char out[CHANNEL_MAX + 1]) {
  if (value == nullptr || out == nullptr) return false;
  size_t len = 0;
  while (len < CHANNEL_MAX && value[len] != 0) {
    char c = value[len];
    out[len++] = c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c;
  }
  bool fits = value[len] == 0;
  out[len] = 0;
  return fits && validChannel((const uint8_t*)out, (uint8_t)len);
}

static float policyFloat(const uint8_t* data) {
  uint32_t bits = read32(data);
  float value;
  memcpy(&value, &bits, sizeof(value));
  return value;
}

bool validPolicyPayload(const uint8_t* data, size_t len) {
  if (data == nullptr || len != POLICY_DATA_LEN ||
      data[0] > 64 || data[1] > 64 || data[2] > 64 ||
      (data[3] != 0 && (data[3] < 30 || data[3] > 120)) ||
      (data[4] != 0 && (data[4] < 3 || data[4] > 168)) ||
      (data[5] & 0xc0) != 0 || (data[5] & 3) > 2) return false;
  uint32_t duty_bits = read32(data + 6);
  uint32_t delay_bits = read32(data + 10);
  float duty = policyFloat(data + 6);
  float delay = policyFloat(data + 10);
  return (duty_bits & 0x80000000u) == 0 && (delay_bits & 0x80000000u) == 0 &&
         isfinite(duty) && isfinite(delay) && duty <= 9.0f && delay <= 2.0f;
}

bool readPolicyPayload(const uint8_t data[POLICY_DATA_LEN], PolicyProfile& out) {
  if (!validPolicyPayload(data, POLICY_DATA_LEN)) return false;
  PolicyProfile value = {};
  value.flood_max = data[0];
  value.flood_unscoped = data[1];
  value.flood_advert = data[2];
  value.advert_ticks = data[3];
  value.flood_advert_hours = data[4];
  value.path_mode = data[5] & 3;
  value.loop_detect = (data[5] >> 2) & 3;
  value.multi_acks = (data[5] & 0x10) != 0;
  value.repeat_gate = (data[5] & 0x20) != 0;
  value.airtime_factor = policyFloat(data + 6);
  value.tx_delay_factor = policyFloat(data + 10);
  value.agc_ticks = data[14];
  out = value;
  return true;
}

bool writePolicyPayload(const PolicyProfile& profile,
                        uint8_t out[POLICY_DATA_LEN]) {
  if (out == nullptr || sizeof(float) != 4 || profile.path_mode > 2 ||
      profile.loop_detect > 3) return false;
  memset(out, 0, POLICY_DATA_LEN);
  out[0] = profile.flood_max;
  out[1] = profile.flood_unscoped;
  out[2] = profile.flood_advert;
  out[3] = profile.advert_ticks;
  out[4] = profile.flood_advert_hours;
  out[5] = (uint8_t)(profile.path_mode | (profile.loop_detect << 2) |
                     (profile.multi_acks ? 0x10 : 0) | (profile.repeat_gate ? 0x20 : 0));
  uint32_t bits;
  memcpy(&bits, &profile.airtime_factor, sizeof(bits));
  write32(out + 6, bits);
  memcpy(&bits, &profile.tx_delay_factor, sizeof(bits));
  write32(out + 10, bits);
  out[14] = profile.agc_ticks;
  return validPolicyPayload(out, POLICY_DATA_LEN);
}

WireResult readRadioPayload(const uint8_t* data, size_t len,
                            uint32_t build_epoch, RadioPayload& out) {
  if (data == nullptr || len != RADIO_PAYLOAD_LEN || data[0] != RADIO_SCHEMA ||
      data[1] != 0) return WIRE_MALFORMED;
  RadioPayload value = {};
  value.migration_id = read64(data + 2);
  value.target.freq_hz = read32(data + 10);
  value.target.bw_hz = read32(data + 14);
  value.target.sf = data[18];
  value.target.cr = data[19];
  value.start = read32(data + 20);
  value.cutover = read32(data + 24);
  value.stamp = read32(data + 28);
  value.schedule.campaign_interval = read16(data + 32);
  value.schedule.test_interval = read16(data + 34);
  value.schedule.test_window = read16(data + 36);
  value.schedule.campaign_duration = read16(data + 38);
  value.schedule.confirm_interval = read16(data + 40);
  value.schedule.confirm_window = read16(data + 42);
  float freq;
  float bw;
  if (!radioPayloadValid(value, build_epoch) ||
      !radioToFloats(value.target, freq, bw)) return WIRE_MALFORMED;
  out = value;
  return WIRE_OK;
}

size_t writeRadioPayload(const RadioPayload& value, uint32_t build_epoch,
                         uint8_t* out, size_t capacity) {
  float freq;
  float bw;
  if (out == nullptr || capacity < RADIO_PAYLOAD_LEN ||
      !radioPayloadValid(value, build_epoch) ||
      !radioToFloats(value.target, freq, bw)) return 0;
  out[0] = RADIO_SCHEMA;
  out[1] = 0;
  write64(out + 2, value.migration_id);
  write32(out + 10, value.target.freq_hz);
  write32(out + 14, value.target.bw_hz);
  out[18] = value.target.sf;
  out[19] = value.target.cr;
  write32(out + 20, value.start);
  write32(out + 24, value.cutover);
  write32(out + 28, value.stamp);
  write16(out + 32, value.schedule.campaign_interval);
  write16(out + 34, value.schedule.test_interval);
  write16(out + 36, value.schedule.test_window);
  write16(out + 38, value.schedule.campaign_duration);
  write16(out + 40, value.schedule.confirm_interval);
  write16(out + 42, value.schedule.confirm_window);
  return RADIO_PAYLOAD_LEN;
}

static uint8_t channelLength(const char* channel) {
  if (channel == nullptr) return 0;
  size_t len = strnlen(channel, CHANNEL_MAX + 1);
  if (!validChannel((const uint8_t*)channel, (uint8_t)len)) return 0;
  return (uint8_t)len;
}

static uint16_t dataLimit(uint8_t dataset) {
  if (dataset == REGION) return REGION_DATA_MAX;
  if (dataset == POLICY) return POLICY_DATA_LEN;
  if (dataset == RADIO) return RADIO_PAYLOAD_LEN;
  return 0;
}

static void preamble(uint8_t* out, uint8_t type) {
  out[0] = MAGIC[0];
  out[1] = MAGIC[1];
  out[2] = WIRE_VERSION;
  out[3] = type;
}

WireResult classify(const uint8_t* frame, size_t len, uint8_t& type) {
  if (frame == nullptr || len < 4 || frame[0] != MAGIC[0] || frame[1] != MAGIC[1]) {
    return WIRE_FOREIGN;
  }
  if (frame[2] != WIRE_VERSION) return WIRE_VERSION_UNSUPPORTED;
  type = frame[3];
  if (type < MANIFEST || type > RADIO_ABORT) return WIRE_TYPE_UNSUPPORTED;
  return WIRE_OK;
}

uint8_t chunkCount(uint16_t data_len) {
  if (data_len == 0 || data_len > REGION_DATA_MAX) return 0;
  return (uint8_t)((data_len + CHUNK_DATA_MAX - 1) / CHUNK_DATA_MAX);
}

uint8_t chunkLength(uint8_t index, uint16_t data_len, uint8_t chunks) {
  if (chunks == 0 || chunks != chunkCount(data_len) || index >= chunks) return 0;
  if (index + 1 < chunks) return CHUNK_DATA_MAX;
  return (uint8_t)(data_len - (uint16_t)(chunks - 1) * CHUNK_DATA_MAX);
}

WireResult readManifest(const uint8_t* frame, size_t len, Manifest& out) {
  uint8_t type = 0;
  WireResult result = classify(frame, len, type);
  if (result != WIRE_OK) return result;
  if (type != MANIFEST) return WIRE_TYPE_UNSUPPORTED;
  if (len != MANIFEST_LEN) return WIRE_MALFORMED;
  if (!knownDataset(frame[4])) return WIRE_DATASET_UNSUPPORTED;
  if (!knownFormat(frame[5])) return WIRE_FORMAT_UNSUPPORTED;
  if ((frame[6] & RESERVED_MASK) != 0) return WIRE_MALFORMED;

  uint32_t generation = read32(frame + 7);
  if (generation == 0) return WIRE_MALFORMED;

  size_t channel_len = 0;
  while (channel_len < CHANNEL_MAX && frame[11 + channel_len] != 0) ++channel_len;
  if (!validChannel(frame + 11, (uint8_t)channel_len)) {
    return WIRE_MALFORMED;
  }
  for (size_t i = channel_len; i < CHANNEL_MAX; ++i) {
    if (frame[11 + i] != 0) return WIRE_MALFORMED;
  }

  uint16_t data_len = read16(frame + 27);
  uint16_t limit = dataLimit(frame[4]);
  if (data_len == 0 || data_len > limit ||
      (frame[4] == POLICY && data_len != POLICY_DATA_LEN) ||
      (frame[4] == RADIO &&
       (frame[5] != FORMAT_RAW || data_len != RADIO_PAYLOAD_LEN))) {
    return WIRE_MALFORMED;
  }
  uint8_t chunks = frame[29];
  if (chunks == 0 || chunks > CHUNK_MAX || chunks != chunkCount(data_len)) {
    return WIRE_MALFORMED;
  }

  out.dataset = frame[4];
  out.format = frame[5];
  out.reset = (frame[6] & RESET_BIT) != 0;
  out.days = (uint8_t)(((frame[6] & DAYS_MASK) >> 1) + 1);
  out.generation = generation;
  memcpy(out.channel, frame + 11, channel_len);
  out.channel[channel_len] = '\0';
  out.data_len = data_len;
  out.chunks = chunks;
  memcpy(out.publisher, frame + 30, FINGERPRINT_LEN);
  memcpy(out.digest, frame + 38, DIGEST_LEN);
  out.signature = frame + MANIFEST_SIGNED_LEN;
  return WIRE_OK;
}

WireResult readChunk(const uint8_t* frame, size_t len, ChunkView& out) {
  uint8_t type = 0;
  WireResult result = classify(frame, len, type);
  if (result != WIRE_OK) return result;
  if (type != CHUNK) return WIRE_TYPE_UNSUPPORTED;
  if (len < CHUNK_MIN_LEN || len > FRAME_MAX) return WIRE_MALFORMED;
  if (!knownDataset(frame[4])) return WIRE_DATASET_UNSUPPORTED;

  uint32_t generation = read32(frame + 5);
  uint8_t data_len = (uint8_t)(len - CHUNK_HEADER_LEN - SIGNATURE_LEN);
  if (generation == 0 || data_len == 0 || data_len > CHUNK_DATA_MAX ||
      frame[9] >= CHUNK_MAX) {
    return WIRE_MALFORMED;
  }

  out.dataset = frame[4];
  out.generation = generation;
  out.index = frame[9];
  out.data = frame + CHUNK_HEADER_LEN;
  out.data_len = data_len;
  out.signature = frame + CHUNK_HEADER_LEN + data_len;
  return WIRE_OK;
}

WireResult readAbort(const uint8_t* frame, size_t len, AbortView& out) {
  uint8_t type = 0;
  WireResult result = classify(frame, len, type);
  if (result != WIRE_OK) return result;
  if (type != ABORT) return WIRE_TYPE_UNSUPPORTED;
  if (len != ABORT_LEN) return WIRE_MALFORMED;
  if (!knownDataset(frame[4])) return WIRE_DATASET_UNSUPPORTED;

  out.dataset = frame[4];
  out.manifest_hash = frame + 5;
  out.signature = frame + ABORT_SIGNED_LEN;
  return WIRE_OK;
}

WireResult readRadioConfirm(const uint8_t* frame, size_t len,
                            RadioConfirmView& out) {
  uint8_t type = 0;
  WireResult result = classify(frame, len, type);
  if (result != WIRE_OK) return result;
  if (type != RADIO_CONFIRM) return WIRE_TYPE_UNSUPPORTED;
  if (len != RADIO_CONFIRM_LEN || frame[4] != RADIO) return WIRE_MALFORMED;
  uint64_t migration_id = read64(frame + 13);
  uint32_t cutover = read32(frame + 53);
  if (migration_id == 0 || cutover == 0) return WIRE_MALFORMED;
  out.publisher = frame + 5;
  out.migration_id = migration_id;
  out.target_digest = frame + 21;
  out.cutover = cutover;
  out.signature = frame + RADIO_CONFIRM_SIGNED_LEN;
  return WIRE_OK;
}

WireResult readRadioAbort(const uint8_t* frame, size_t len,
                          RadioAbortView& out) {
  uint8_t type = 0;
  WireResult result = classify(frame, len, type);
  if (result != WIRE_OK) return result;
  if (type != RADIO_ABORT) return WIRE_TYPE_UNSUPPORTED;
  if (len != RADIO_ABORT_LEN || frame[4] != RADIO) return WIRE_MALFORMED;
  uint64_t migration_id = read64(frame + 13);
  if (migration_id == 0) return WIRE_MALFORMED;
  out.publisher = frame + 5;
  out.migration_id = migration_id;
  out.signature = frame + RADIO_ABORT_SIGNED_LEN;
  return WIRE_OK;
}

WireResult match(const ChunkView& chunk, const Manifest& manifest, uint16_t& offset) {
  if (chunk.dataset != manifest.dataset || chunk.generation != manifest.generation) {
    return WIRE_MISMATCH;
  }
  uint8_t expected = chunkLength(chunk.index, manifest.data_len, manifest.chunks);
  if (expected == 0 || chunk.data_len != expected) return WIRE_MALFORMED;
  offset = (uint16_t)chunk.index * CHUNK_DATA_MAX;
  return WIRE_OK;
}

size_t writeManifestPrefix(const Manifest& manifest, uint8_t* out, size_t capacity) {
  uint8_t channel_len = channelLength(manifest.channel);
  uint16_t limit = dataLimit(manifest.dataset);
  if (out == nullptr || capacity < MANIFEST_LEN || !knownDataset(manifest.dataset) ||
      !knownFormat(manifest.format) || manifest.days < 1 || manifest.days > 4 ||
      manifest.generation == 0 || channel_len == 0 || manifest.data_len == 0 ||
      manifest.data_len > limit ||
      (manifest.dataset == POLICY && manifest.data_len != POLICY_DATA_LEN) ||
      (manifest.dataset == RADIO &&
       (manifest.format != FORMAT_RAW || manifest.data_len != RADIO_PAYLOAD_LEN)) ||
      manifest.chunks != chunkCount(manifest.data_len)) {
    return 0;
  }

  preamble(out, MANIFEST);
  out[4] = manifest.dataset;
  out[5] = manifest.format;
  out[6] = (uint8_t)(((manifest.days - 1) << 1) | (manifest.reset ? RESET_BIT : 0));
  write32(out + 7, manifest.generation);
  memset(out + 11, 0, CHANNEL_MAX);
  memcpy(out + 11, manifest.channel, channel_len);
  write16(out + 27, manifest.data_len);
  out[29] = manifest.chunks;
  memcpy(out + 30, manifest.publisher, FINGERPRINT_LEN);
  memcpy(out + 38, manifest.digest, DIGEST_LEN);
  return MANIFEST_SIGNED_LEN;
}

size_t writeChunkPrefix(uint8_t dataset, uint32_t generation, uint8_t index,
                        const uint8_t* data, uint8_t data_len,
                        uint8_t* out, size_t capacity) {
  size_t frame_len = CHUNK_HEADER_LEN + data_len + SIGNATURE_LEN;
  if (out == nullptr || data == nullptr || !knownDataset(dataset) || generation == 0 ||
      index >= CHUNK_MAX || data_len == 0 || data_len > CHUNK_DATA_MAX ||
      capacity < frame_len) {
    return 0;
  }

  preamble(out, CHUNK);
  out[4] = dataset;
  write32(out + 5, generation);
  out[9] = index;
  memcpy(out + CHUNK_HEADER_LEN, data, data_len);
  return CHUNK_HEADER_LEN + data_len;
}

size_t writeAbortPrefix(uint8_t dataset, const uint8_t manifest_hash[DIGEST_LEN],
                        uint8_t* out, size_t capacity) {
  if (out == nullptr || manifest_hash == nullptr || !knownDataset(dataset) ||
      capacity < ABORT_LEN) {
    return 0;
  }
  preamble(out, ABORT);
  out[4] = dataset;
  memcpy(out + 5, manifest_hash, DIGEST_LEN);
  return ABORT_SIGNED_LEN;
}

size_t writeRadioConfirmPrefix(const uint8_t publisher[FINGERPRINT_LEN],
                               uint64_t migration_id,
                               const uint8_t target_digest[DIGEST_LEN],
                               uint32_t cutover, uint8_t* out, size_t capacity) {
  if (publisher == nullptr || migration_id == 0 || target_digest == nullptr ||
      cutover == 0 || out == nullptr || capacity < RADIO_CONFIRM_LEN) return 0;
  preamble(out, RADIO_CONFIRM);
  out[4] = RADIO;
  memcpy(out + 5, publisher, FINGERPRINT_LEN);
  write64(out + 13, migration_id);
  memcpy(out + 21, target_digest, DIGEST_LEN);
  write32(out + 53, cutover);
  return RADIO_CONFIRM_SIGNED_LEN;
}

size_t writeRadioAbortPrefix(const uint8_t publisher[FINGERPRINT_LEN],
                             uint64_t migration_id,
                             uint8_t* out, size_t capacity) {
  if (publisher == nullptr || migration_id == 0 || out == nullptr ||
      capacity < RADIO_ABORT_LEN) return 0;
  preamble(out, RADIO_ABORT);
  out[4] = RADIO;
  memcpy(out + 5, publisher, FINGERPRINT_LEN);
  write64(out + 13, migration_id);
  return RADIO_ABORT_SIGNED_LEN;
}

CarrierResult readCarrier(const uint8_t* payload, size_t payload_len,
                          uint8_t* frame, size_t capacity, uint8_t& frame_len,
                          DecryptFn decrypt, void* context) {
  if (payload == nullptr || frame == nullptr || decrypt == nullptr ||
      payload_len < 3 || payload[0] != CARRIER_HASH) return CARRIER_FOREIGN;
  uint8_t plain[FRAME_MAX + 19];
  if (payload_len - 1 > sizeof(plain)) return CARRIER_MALFORMED;
  int len = decrypt(CARRIER_KEY, plain, payload + 1, payload_len - 1, context);
  if (len <= 0) return CARRIER_AUTH;
  if (len < 3 || ((uint16_t)plain[0] | (uint16_t)plain[1] << 8) != APP_ID) {
    return CARRIER_FOREIGN;
  }
  uint8_t inner_len = plain[2];
  if ((int)inner_len + 3 > len) return CARRIER_MALFORMED;
  uint8_t type;
  if (classify(plain + 3, inner_len, type) != WIRE_OK) return CARRIER_FOREIGN;
  if (inner_len > capacity) return CARRIER_CAPACITY;
  memcpy(frame, plain + 3, inner_len);
  frame_len = inner_len;
  return CARRIER_OK;
}

size_t writeCarrier(const uint8_t* frame, size_t frame_len,
                    uint8_t* out, size_t capacity) {
  uint8_t type;
  if (frame == nullptr || out == nullptr || frame_len > FRAME_MAX ||
      frame_len > 255 || capacity < frame_len + 3 ||
      classify(frame, frame_len, type) != WIRE_OK) return 0;
  out[0] = (uint8_t)APP_ID;
  out[1] = (uint8_t)(APP_ID >> 8);
  out[2] = (uint8_t)frame_len;
  memcpy(out + 3, frame, frame_len);
  return frame_len + 3;
}

static_assert(MANIFEST_SIGNED_LEN + SIGNATURE_LEN == MANIFEST_LEN,
              "manifest layout changed");
static_assert(RADIO_CONFIRM_SIGNED_LEN + SIGNATURE_LEN == RADIO_CONFIRM_LEN,
              "radio confirmation layout changed");
static_assert(RADIO_ABORT_SIGNED_LEN + SIGNATURE_LEN == RADIO_ABORT_LEN,
              "radio abort layout changed");
static_assert(CHUNK_HEADER_LEN + CHUNK_DATA_MAX + SIGNATURE_LEN == FRAME_MAX,
              "chunk layout changed");
static_assert(ABORT_SIGNED_LEN + SIGNATURE_LEN == ABORT_LEN,
              "abort layout changed");
static_assert((REGION_DATA_MAX + CHUNK_DATA_MAX - 1) / CHUNK_DATA_MAX == CHUNK_MAX,
              "region chunk bound changed");

}  // namespace sync
}  // namespace mobmesh
