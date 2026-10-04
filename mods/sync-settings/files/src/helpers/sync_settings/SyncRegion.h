#pragma once

#include "SyncWire.h"

namespace mobmesh {
namespace sync {

static const uint8_t REGION_MAX = 32;
static const uint8_t REGION_NAME_MAX = 30;
static const uint8_t REGION_DEPTH_MAX = 7;
static const uint8_t REGION_DENY = 0x01;

enum RegionResult : uint8_t {
  REGION_OK,
  REGION_MALFORMED,
  REGION_CAPACITY,
  REGION_NAME,
  REGION_DUPLICATE,
  REGION_HIERARCHY,
  REGION_FLAGS,
};

enum ScopeState : uint8_t {
  SCOPE_NONE,
  SCOPE_ALLOW,
  SCOPE_DENY,
};

typedef void (*RegionKeyFn)(const uint8_t* name, uint8_t name_len,
                            uint8_t out[16], void* context);
typedef bool (*RegionMatchFn)(const uint8_t key[16], void* context);
typedef ScopeState (*RegionLookupFn)(const uint8_t* name, uint8_t name_len,
                                     void* context);

struct RegionIndex {
  uint8_t count;
  uint16_t offset[REGION_MAX];
};

struct RegionView {
  uint8_t parent;
  bool denied;
  const uint8_t* name;
  uint8_t name_len;
};

typedef bool (*RegionReadFn)(uint8_t position, RegionView& out, void* context);

class Regions {
 public:
  Regions();

  RegionResult replace(const uint8_t* data, size_t len,
                       RegionKeyFn make_key, void* context);
  void clear();

  const uint8_t* data() const { return data_; }
  uint16_t size() const { return size_; }
  uint8_t count() const { return index_.count; }
  bool get(uint8_t position, RegionView& out) const;
  const uint8_t* key(uint8_t position) const;
  int8_t find(const uint8_t* name, uint8_t name_len) const;

 private:
  uint8_t data_[REGION_DATA_MAX];
  uint8_t keys_[REGION_MAX][16];
  RegionIndex index_;
  uint16_t size_;
};

struct ScopeMatch {
  ScopeState state;
  int8_t position;
  const uint8_t* key;
};

struct RegionSource {
  uint8_t count;
  bool wildcard;
  RegionLookupFn find;
  RegionReadFn get;
  void* context;
};

RegionResult indexRegions(const uint8_t* data, size_t len, RegionIndex& index);
bool regionAt(const uint8_t* data, const RegionIndex& index, uint8_t position,
              RegionView& out);
int8_t findRegion(const uint8_t* data, const RegionIndex& index,
                  const uint8_t* name, uint8_t name_len);
ScopeMatch matchRegions(const Regions& regions, RegionMatchFn matches,
                        void* context);
ScopeMatch chooseScope(const ScopeMatch& overlay, const ScopeMatch& base);
int writeRegionList(const Regions& overlay, const RegionSource& base,
                    char* out, size_t capacity);
RegionResult putRegion(const Regions& regions, const char* name, const char* parent,
                       uint8_t* out, size_t capacity, uint16_t& len);
RegionResult setRegionFlood(const Regions& regions, const char* name, bool allow,
                            uint8_t* out, size_t capacity, uint16_t& len);
RegionResult removeRegion(const Regions& regions, const char* name,
                          uint8_t* out, size_t capacity, uint16_t& len);
RegionResult defineRegions(const Regions& regions, char* definition,
                           uint8_t* out, size_t capacity, uint16_t& len);

}  // namespace sync
}  // namespace mobmesh
