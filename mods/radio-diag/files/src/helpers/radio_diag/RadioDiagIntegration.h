#pragma once

#include <helpers/ModHooks.h>

void radioDiagLoop();
void radioDiagRecv(const mesh::Packet* packet, bool accepted, const uint8_t scope_key[16]);
bool radioDiagHandleCli(const ModCliContext& context, char* command, char* reply);
