#pragma once

#include <stddef.h>
#include <stdint.h>

class RegionMap;
struct ModRegionMatch;
namespace mesh { class Packet; }
struct ModCliContext;

bool syncRoute(mesh::Packet* packet, RegionMap* base, ModRegionMatch* out);
bool syncAllowFlood(const mesh::Packet* packet, bool scope_known);
int syncExportRegions(RegionMap* base, char* out, size_t capacity,
                      uint8_t excluded_flags);
size_t syncOwnerInfoMarker(uint8_t out[4]);
void syncRecv(const mesh::Packet* packet, bool accepted,
              const uint8_t scope_key[16]);
void syncTx(uint32_t packet_id, bool succeeded);
void syncLoop();
bool syncCli(const ModCliContext& context, char* command, char* reply);
