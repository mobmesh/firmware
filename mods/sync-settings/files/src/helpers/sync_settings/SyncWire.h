#pragma once

#include <stddef.h>
#include <stdint.h>

namespace mobmesh {
namespace sync {

static const uint16_t APP_ID = 0x4D4D;
static const uint8_t WIRE_VERSION = 0x02;
static const size_t FRAME_MAX = 165;
static const size_t SIGNATURE_LEN = 64;
static const size_t DIGEST_LEN = 32;
static const size_t FINGERPRINT_LEN = 8;
static const size_t CHANNEL_MAX = 16;
static const size_t MANIFEST_LEN = 134;
static const size_t MANIFEST_SIGNED_LEN = 70;
static const size_t CHUNK_HEADER_LEN = 10;
static const size_t CHUNK_DATA_MAX = 91;
static const size_t CHUNK_MIN_LEN = 75;
static const size_t ABORT_LEN = 101;
static const size_t ABORT_SIGNED_LEN = 37;
static const size_t REGION_DATA_MAX = 1057;
static const size_t POLICY_DATA_LEN = 15;
static const uint8_t CHUNK_MAX = 12;
// High nibble is the payload schema, low nibble the codec; a new schema bumps it and old nodes reject the manifest.
static const uint8_t FORMAT_RAW = 0x01;
static const uint8_t FORMAT_DEFLATE = 0x02;
static const uint8_t CARRIER_HASH = 0x35;
static const uint8_t CARRIER_KEY[32] = {
  0x17, 0xb7, 0xff, 0x4f, 0x4d, 0x5e, 0x06, 0xe1,
  0xe1, 0x48, 0xcf, 0x09, 0xae, 0x58, 0x42, 0x42,
  0x8e, 0x15, 0xc4, 0xcd, 0xf7, 0xd9, 0x3a, 0x6a,
  0x18, 0xf7, 0x3b, 0xd3, 0xec, 0x04, 0x8d, 0x12,
};

enum FrameType : uint8_t {
  MANIFEST = 0x01,
  CHUNK = 0x02,
  ABORT = 0x03,
};

enum Dataset : uint8_t {
  REGION = 0x01,
  POLICY = 0x02,
};

enum WireResult : uint8_t {
  WIRE_OK,
  WIRE_FOREIGN,
  WIRE_VERSION_UNSUPPORTED,
  WIRE_TYPE_UNSUPPORTED,
  WIRE_DATASET_UNSUPPORTED,
  WIRE_FORMAT_UNSUPPORTED,
  WIRE_MALFORMED,
  WIRE_MISMATCH,
};

enum CarrierResult : uint8_t {
  CARRIER_OK,
  CARRIER_FOREIGN,
  CARRIER_AUTH,
  CARRIER_MALFORMED,
  CARRIER_CAPACITY,
};

typedef int (*DecryptFn)(const uint8_t key[32], uint8_t* out,
                         const uint8_t* data, size_t len, void* context);

struct Manifest {
  uint8_t dataset;
  uint8_t format;
  bool reset;
  uint8_t days;
  uint32_t generation;
  char channel[CHANNEL_MAX + 1];
  uint16_t data_len;
  uint8_t chunks;
  uint8_t publisher[FINGERPRINT_LEN];
  uint8_t digest[DIGEST_LEN];
  const uint8_t* signature;
};

struct ChunkView {
  uint8_t dataset;
  uint32_t generation;
  uint8_t index;
  const uint8_t* data;
  uint8_t data_len;
  const uint8_t* signature;
};

struct AbortView {
  uint8_t dataset;
  const uint8_t* manifest_hash;
  const uint8_t* signature;
};

struct PolicyProfile {
  uint8_t flood_max;
  uint8_t flood_unscoped;
  uint8_t flood_advert;
  uint8_t advert_ticks;
  uint8_t flood_advert_hours;
  uint8_t path_mode;
  uint8_t loop_detect;
  bool multi_acks;
  bool repeat_gate;
  float airtime_factor;
  float tx_delay_factor;
  uint8_t agc_ticks;
};

WireResult classify(const uint8_t* frame, size_t len, uint8_t& type);
WireResult readManifest(const uint8_t* frame, size_t len, Manifest& out);
WireResult readChunk(const uint8_t* frame, size_t len, ChunkView& out);
WireResult readAbort(const uint8_t* frame, size_t len, AbortView& out);
WireResult match(const ChunkView& chunk, const Manifest& manifest, uint16_t& offset);

uint8_t chunkCount(uint16_t data_len);
uint8_t chunkLength(uint8_t index, uint16_t data_len, uint8_t chunks);
bool validChannel(const uint8_t* channel, uint8_t len);
bool normalizeChannel(const char* value, char out[CHANNEL_MAX + 1]);
bool validPolicyPayload(const uint8_t* data, size_t len);
bool readPolicyPayload(const uint8_t data[POLICY_DATA_LEN], PolicyProfile& out);
bool writePolicyPayload(const PolicyProfile& profile,
                        uint8_t out[POLICY_DATA_LEN]);

size_t writeManifestPrefix(const Manifest& manifest, uint8_t* out, size_t capacity);
size_t writeChunkPrefix(uint8_t dataset, uint32_t generation, uint8_t index,
                        const uint8_t* data, uint8_t data_len,
                        uint8_t* out, size_t capacity);
size_t writeAbortPrefix(uint8_t dataset, const uint8_t manifest_hash[DIGEST_LEN],
                        uint8_t* out, size_t capacity);
CarrierResult readCarrier(const uint8_t* payload, size_t payload_len,
                          uint8_t* frame, size_t capacity, uint8_t& frame_len,
                          DecryptFn decrypt, void* context);
size_t writeCarrier(const uint8_t* frame, size_t frame_len,
                    uint8_t* out, size_t capacity);

}  // namespace sync
}  // namespace mobmesh
