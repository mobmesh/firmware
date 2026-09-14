#pragma once

#include <stddef.h>
#include <stdint.h>

class RegionMap;
struct ModRegionMatch;
namespace mesh { class Packet; }
struct ModCliContext;

namespace mobmesh {
namespace sync {

bool installRegions(const uint8_t* data, size_t len);
void enableRegions(bool enabled);
bool regionsEnabled();
bool storageReady();

}  // namespace sync
}  // namespace mobmesh

bool syncRoute(mesh::Packet* packet, RegionMap* base, ModRegionMatch* out);
int syncExportRegions(RegionMap* base, char* out, size_t capacity,
                      uint8_t excluded_flags);
void syncRecv(const mesh::Packet* packet, bool accepted,
              const uint8_t scope_key[16]);
void syncTx(uint32_t packet_id, bool succeeded);
void syncLoop();
bool syncCli(const ModCliContext& context, char* command, char* reply);
