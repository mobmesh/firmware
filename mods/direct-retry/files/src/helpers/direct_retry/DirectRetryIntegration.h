#pragma once

#include <helpers/ModHooks.h>

void directRetryLoop();
bool directRetryDirect(ModDirectEvent event, const mesh::Packet* packet, uint32_t delay_millis);
bool directRetryHandleCli(const ModCliContext& context, char* command, char* reply);
