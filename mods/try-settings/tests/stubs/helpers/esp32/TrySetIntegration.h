#pragma once

#include <helpers/ModHooks.h>

void trySetLoop();
bool trySetHandleCli(const ModCliContext& context, char* command, char* reply);
