#pragma once

#include <helpers/ModHooks.h>

void diagReportLoop();
void diagReportRecv(const mesh::Packet* packet, bool accepted, const uint8_t scope_key[16]);
bool diagReportHandleCli(const ModCliContext& context, char* command, char* reply);
