#pragma once

#include <helpers/ModHooks.h>

void floodGateLoop();
bool floodGateAllowFlood(const mesh::Packet* packet, bool scope_known);
bool floodGateHandleCli(const ModCliContext& context, char* command, char* reply);
