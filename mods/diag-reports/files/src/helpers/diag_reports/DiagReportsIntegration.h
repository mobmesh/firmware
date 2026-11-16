#pragma once

#include <helpers/ModHooks.h>

void diagReportsLoop();
void diagReportsRecv(const mesh::Packet* packet, bool accepted, const uint8_t scope_key[16]);
bool diagReportsHandleCli(const ModCliContext& context, char* command, char* reply);
void diagReportsAgcReset();
