#pragma once

#include <helpers/ModHooks.h>

// Runs after every other mod: names the build in `ver`, serves stats-* over the remote CLI and refuses a poweroff no mod can wake from.
bool coreHandleCli(const ModCliContext& context, char* command, char* reply);

// ping and trace: CLI, plus the loop and receive hooks that time out and match the one TRACE in flight.
bool coreProbeCli(const ModCliContext& context, char* command, char* reply);
void coreLoop();
void coreRecv(const mesh::Packet* packet, bool accepted, const uint8_t scope_key[16]);

const ModBindings* modBindings();
uint32_t modCliStamp(uint32_t sender_timestamp);
