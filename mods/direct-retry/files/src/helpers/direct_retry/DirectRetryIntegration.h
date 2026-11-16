#pragma once

#include <helpers/ModHooks.h>
#include <helpers/direct_retry/DirectRetryCore.h>

void directRetryLoop();
bool directRetryDirect(ModDirectEvent event, const mesh::Packet* packet, uint32_t delay_millis);
bool directRetryHandleCli(const ModCliContext& context, char* command, char* reply);

// Read-only counters for other mods, since boot: packets that needed a resend and those then confirmed; false while off.
bool directRetryTotals(uint32_t* retried, uint32_t* won);
// The next-hop records with at least one resend, up to max; 0 while off.
int directRetryHopRecords(DrHop* out, int max);
