#pragma once

#include <helpers/ModHooks.h>

void autoRouteLoop();
bool autoRouteDirect(ModDirectEvent event, const mesh::Packet* packet, uint32_t delay_millis);
bool autoRouteHandleCli(const ModCliContext& context, char* command, char* reply);
