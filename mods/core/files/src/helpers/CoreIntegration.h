#pragma once

#include <helpers/ModHooks.h>

// Runs after every other mod: names the build in `ver` and refuses a poweroff no mod can wake from.
bool coreHandleCli(const ModCliContext& context, char* command, char* reply);
