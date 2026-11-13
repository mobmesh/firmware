#pragma once

#include <helpers/ModHooks.h>

// Runs after every other mod: names the build in `ver`, serves stats-* over the remote CLI and refuses a poweroff no mod can wake from.
bool coreHandleCli(const ModCliContext& context, char* command, char* reply);
