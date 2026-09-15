#include "SyncRegion.h"

#include <string.h>

namespace mobmesh {
namespace sync {

static bool nameByte(uint8_t c) {
  return c == '-' || c == '$' || c == '#' ||
         (c >= '0' && c <= '9') || c >= 'A';
}

static bool nameValid(const uint8_t* name, uint8_t len) {
  if (len == 0 || len > REGION_NAME_MAX || name == nullptr) return false;
  if (!nameByte(name[0]) || name[0] == '#' || name[0] == '$' || name[0] == '*') {
    return false;
  }
  for (uint8_t i = 1; i < len; ++i) {
    if (!nameByte(name[i])) return false;
  }
  return true;
}

static bool followsDepthFirst(uint8_t parent, uint8_t position,
                              const uint8_t parents[REGION_MAX]) {
  if (position == 0) return parent == 0;
  if (parent == position) return true;

  uint8_t ancestor = parents[position - 1];
  for (;;) {
    if (parent == ancestor) return true;
    if (ancestor == 0) return false;
    ancestor = parents[ancestor - 1];
  }
}

Regions::Regions() { clear(); }

RegionResult Regions::replace(const uint8_t* data, size_t len,
                              RegionKeyFn make_key, void* context) {
  RegionIndex next;
  RegionResult result = indexRegions(data, len, next);
  if (result != REGION_OK) return result;
  if (next.count != 0 && make_key == nullptr) return REGION_MALFORMED;

  memmove(data_, data, len);
  index_ = next;
  size_ = (uint16_t)len;
  for (uint8_t i = 0; i < index_.count; ++i) {
    RegionView entry;
    regionAt(data_, index_, i, entry);
    make_key(entry.name, entry.name_len, keys_[i], context);
  }
  return REGION_OK;
}

void Regions::clear() {
  data_[0] = 0;
  index_.count = 0;
  size_ = 1;
}

bool Regions::get(uint8_t position, RegionView& out) const {
  return regionAt(data_, index_, position, out);
}

const uint8_t* Regions::key(uint8_t position) const {
  return position < index_.count ? keys_[position] : nullptr;
}

int8_t Regions::find(const uint8_t* name, uint8_t name_len) const {
  return findRegion(data_, index_, name, name_len);
}

RegionResult indexRegions(const uint8_t* data, size_t len, RegionIndex& index) {
  index.count = 0;
  if (data == nullptr || len == 0 || len > REGION_DATA_MAX) return REGION_MALFORMED;

  uint8_t count = data[0];
  if (count > REGION_MAX) return REGION_CAPACITY;

  uint8_t parents[REGION_MAX] = {};
  uint8_t depths[REGION_MAX] = {};
  size_t cursor = 1;

  for (uint8_t i = 0; i < count; ++i) {
    if (cursor + 3 > len) return REGION_MALFORMED;

    uint8_t parent = data[cursor];
    uint8_t flags = data[cursor + 1];
    uint8_t name_len = data[cursor + 2];
    if (parent > i) return REGION_HIERARCHY;
    if ((flags & ~REGION_DENY) != 0) return REGION_FLAGS;
    if (cursor + 3 + name_len > len) return REGION_MALFORMED;

    const uint8_t* name = data + cursor + 3;
    if (!nameValid(name, name_len)) return REGION_NAME;
    if (!followsDepthFirst(parent, i, parents)) return REGION_HIERARCHY;

    uint8_t depth = parent == 0 ? 1 : (uint8_t)(depths[parent - 1] + 1);
    if (depth > REGION_DEPTH_MAX) return REGION_HIERARCHY;

    for (uint8_t prior = 0; prior < i; ++prior) {
      size_t old = index.offset[prior];
      uint8_t old_len = data[old + 2];
      if (old_len == name_len && memcmp(data + old + 3, name, name_len) == 0) {
        return REGION_DUPLICATE;
      }
    }

    index.offset[i] = (uint16_t)cursor;
    parents[i] = parent;
    depths[i] = depth;
    cursor += 3 + name_len;
  }

  if (cursor != len) return REGION_MALFORMED;
  index.count = count;
  return REGION_OK;
}

bool regionAt(const uint8_t* data, const RegionIndex& index, uint8_t position,
              RegionView& out) {
  if (data == nullptr || position >= index.count) return false;
  size_t offset = index.offset[position];
  out.parent = data[offset];
  out.denied = (data[offset + 1] & REGION_DENY) != 0;
  out.name_len = data[offset + 2];
  out.name = data + offset + 3;
  return true;
}

int8_t findRegion(const uint8_t* data, const RegionIndex& index,
                  const uint8_t* name, uint8_t name_len) {
  if (!nameValid(name, name_len)) return -1;
  for (uint8_t i = 0; i < index.count; ++i) {
    size_t offset = index.offset[i];
    if (data[offset + 2] == name_len &&
        memcmp(data + offset + 3, name, name_len) == 0) {
      return (int8_t)i;
    }
  }
  return -1;
}

ScopeMatch matchRegions(const Regions& regions, RegionMatchFn matches,
                        void* context) {
  ScopeMatch allowed = {SCOPE_NONE, -1, nullptr};
  if (matches == nullptr) return allowed;

  for (uint8_t i = 0; i < regions.count(); ++i) {
    const uint8_t* key = regions.key(i);
    if (!matches(key, context)) continue;

    RegionView entry;
    regions.get(i, entry);
    if (entry.denied) {
      ScopeMatch denied = {SCOPE_DENY, (int8_t)i, key};
      return denied;
    }
    if (allowed.state == SCOPE_NONE) {
      allowed.state = SCOPE_ALLOW;
      allowed.position = (int8_t)i;
      allowed.key = key;
    }
  }
  return allowed;
}

ScopeMatch chooseScope(const ScopeMatch& overlay, const ScopeMatch& base) {
  if (overlay.state == SCOPE_DENY) return overlay;
  if (base.state == SCOPE_DENY) return base;
  if (overlay.state == SCOPE_ALLOW) return overlay;
  return base;
}

static bool appendName(char* out, size_t capacity, size_t& at,
                       const uint8_t* name, uint8_t len) {
  size_t needed = len + (at == 0 ? 0 : 1);
  if (at + needed + 1 > capacity) return false;
  if (at != 0) out[at++] = ',';
  memcpy(out + at, name, len);
  at += len;
  out[at] = 0;
  return true;
}

int writeRegionList(const Regions& overlay, const RegionSource& base,
                    char* out, size_t capacity) {
  if (out == nullptr || capacity == 0 || base.find == nullptr ||
      base.get == nullptr) return -1;
  size_t at = 0;
  out[0] = 0;

  for (uint8_t i = 0; i < overlay.count(); ++i) {
    RegionView entry;
    if (!overlay.get(i, entry) || entry.denied ||
        base.find(entry.name, entry.name_len, base.context) == SCOPE_DENY) {
      continue;
    }
    appendName(out, capacity, at, entry.name, entry.name_len);
  }

  if (base.wildcard) {
    static const uint8_t star = '*';
    appendName(out, capacity, at, &star, 1);
  }
  for (uint8_t i = 0; i < base.count; ++i) {
    RegionView entry;
    if (!base.get(i, entry, base.context) || entry.denied) continue;
    bool private_name = entry.name_len != 0 && entry.name[0] == '$';
    if (!private_name && overlay.find(entry.name, entry.name_len) >= 0) continue;
    appendName(out, capacity, at, entry.name, entry.name_len);
  }
  return (int)at;
}

struct EditNode {
  int8_t parent;
  bool denied;
  bool removed;
  const uint8_t* name;
  uint8_t name_len;
};

static RegionResult loadNodes(const Regions& regions, EditNode nodes[REGION_MAX],
                              uint8_t& count) {
  count = regions.count();
  for (uint8_t i = 0; i < count; ++i) {
    RegionView entry;
    if (!regions.get(i, entry)) return REGION_MALFORMED;
    nodes[i].parent = entry.parent == 0 ? -1 : (int8_t)(entry.parent - 1);
    nodes[i].denied = entry.denied;
    nodes[i].removed = false;
    nodes[i].name = entry.name;
    nodes[i].name_len = entry.name_len;
  }
  return REGION_OK;
}

static int8_t nodeByName(const EditNode nodes[REGION_MAX], uint8_t count,
                         const char* name) {
  if (name == nullptr) return -1;
  if (*name == '#') ++name;
  size_t len = strnlen(name, REGION_NAME_MAX + 1);
  if (len == 0 || len > REGION_NAME_MAX) return -1;
  for (uint8_t i = 0; i < count; ++i) {
    if (!nodes[i].removed && nodes[i].name_len == len &&
        memcmp(nodes[i].name, name, len) == 0) return (int8_t)i;
  }
  return -1;
}

static bool emitNodes(const EditNode nodes[REGION_MAX], uint8_t count, int8_t parent,
                      uint8_t parent_position, uint8_t* out, size_t capacity, size_t& cursor,
                      uint8_t& emitted) {
  for (uint8_t i = 0; i < count; ++i) {
    if (nodes[i].removed || nodes[i].parent != parent) continue;
    size_t needed = 3 + nodes[i].name_len;
    if (cursor + needed > capacity) return false;
    uint8_t position = emitted++;
    out[cursor] = parent < 0 ? 0 : parent_position;
    out[cursor + 1] = nodes[i].denied ? REGION_DENY : 0;
    out[cursor + 2] = nodes[i].name_len;
    memcpy(out + cursor + 3, nodes[i].name, nodes[i].name_len);
    cursor += needed;
    if (!emitNodes(nodes, count, (int8_t)i, (uint8_t)(position + 1),
                   out, capacity, cursor, emitted)) return false;
  }
  return true;
}

static RegionResult encodeNodes(const EditNode nodes[REGION_MAX], uint8_t count,
                                uint8_t* out, size_t capacity, uint16_t& len) {
  if (out == nullptr || capacity == 0) return REGION_CAPACITY;
  uint8_t live = 0;
  for (uint8_t i = 0; i < count; ++i) if (!nodes[i].removed) ++live;
  out[0] = live;
  size_t cursor = 1;
  uint8_t emitted = 0;
  if (!emitNodes(nodes, count, -1, 0, out, capacity, cursor, emitted) ||
      emitted != live) return REGION_HIERARCHY;
  RegionIndex index;
  RegionResult result = indexRegions(out, cursor, index);
  if (result != REGION_OK) return result;
  len = (uint16_t)cursor;
  return REGION_OK;
}

RegionResult putRegion(const Regions& regions, const char* name, const char* parent,
                       uint8_t* out, size_t capacity, uint16_t& len) {
  EditNode nodes[REGION_MAX];
  uint8_t count;
  RegionResult result = loadNodes(regions, nodes, count);
  if (result != REGION_OK) return result;

  if (name != nullptr && *name == '#') ++name;
  size_t name_len = name == nullptr ? 0 : strnlen(name, REGION_NAME_MAX + 1);
  if (name_len == 0 || name_len > REGION_NAME_MAX) return REGION_NAME;
  int8_t parent_index = parent == nullptr ? -1 : nodeByName(nodes, count, parent);
  if (parent != nullptr && parent_index < 0) return REGION_HIERARCHY;

  int8_t target = nodeByName(nodes, count, name);
  if (target < 0) {
    if (count >= REGION_MAX) return REGION_CAPACITY;
    target = (int8_t)count++;
    nodes[target].name = (const uint8_t*)name;
    nodes[target].name_len = (uint8_t)name_len;
    nodes[target].removed = false;
  }
  if (target == parent_index) return REGION_HIERARCHY;
  nodes[target].parent = parent_index;
  nodes[target].denied = false;
  return encodeNodes(nodes, count, out, capacity, len);
}

RegionResult setRegionFlood(const Regions& regions, const char* name, bool allow,
                            uint8_t* out, size_t capacity, uint16_t& len) {
  EditNode nodes[REGION_MAX];
  uint8_t count;
  RegionResult result = loadNodes(regions, nodes, count);
  if (result != REGION_OK) return result;
  int8_t target = nodeByName(nodes, count, name);
  if (target < 0) return REGION_NAME;
  nodes[target].denied = !allow;
  return encodeNodes(nodes, count, out, capacity, len);
}

RegionResult removeRegion(const Regions& regions, const char* name,
                          uint8_t* out, size_t capacity, uint16_t& len) {
  EditNode nodes[REGION_MAX];
  uint8_t count;
  RegionResult result = loadNodes(regions, nodes, count);
  if (result != REGION_OK) return result;
  int8_t target = nodeByName(nodes, count, name);
  if (target < 0) return REGION_NAME;
  for (uint8_t i = 0; i < count; ++i) {
    if (nodes[i].parent == target) return REGION_HIERARCHY;
  }
  nodes[target].removed = true;
  return encodeNodes(nodes, count, out, capacity, len);
}

RegionResult defineRegions(const Regions& regions, char* definition,
                           uint8_t* out, size_t capacity, uint16_t& len) {
  EditNode nodes[REGION_MAX];
  uint8_t count;
  RegionResult result = loadNodes(regions, nodes, count);
  if (result != REGION_OK) return result;
  if (definition == nullptr) return REGION_NAME;

  int8_t cursor = -1;
  bool any = false;
  while (*definition != 0) {
    while (*definition == ' ') ++definition;
    if (*definition == 0) break;
    char* name = definition;
    while (*definition != 0 && *definition != ' ') ++definition;
    if (*definition != 0) *definition++ = 0;
    if (*name == '#') ++name;

    char* jump = nullptr;
    for (char* p = name; *p != 0; ++p) {
      if (*p == '|' || *p == ',') {
        *p = 0;
        jump = p + 1;
        break;
      }
    }
    size_t name_len = strnlen(name, REGION_NAME_MAX + 1);
    if (name_len == 0 || name_len > REGION_NAME_MAX ||
        (jump != nullptr && *jump == 0)) return REGION_NAME;

    int8_t target = nodeByName(nodes, count, name);
    if (target < 0) {
      if (count >= REGION_MAX) return REGION_CAPACITY;
      target = (int8_t)count++;
      nodes[target].name = (const uint8_t*)name;
      nodes[target].name_len = (uint8_t)name_len;
      nodes[target].removed = false;
    }
    if (target == cursor) return REGION_HIERARCHY;
    nodes[target].parent = cursor;
    nodes[target].denied = false;
    if (jump == nullptr) cursor = target;
    else {
      cursor = nodeByName(nodes, count, jump);
      if (cursor < 0) return REGION_HIERARCHY;
    }
    any = true;
  }
  return any ? encodeNodes(nodes, count, out, capacity, len) : REGION_NAME;
}

static_assert(1 + REGION_MAX * (3 + REGION_NAME_MAX) == REGION_DATA_MAX,
              "region layout changed");

}  // namespace sync
}  // namespace mobmesh
