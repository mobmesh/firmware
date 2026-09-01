#pragma once

#include <helpers/ModHooks.h>

void powerGuardBeforeRadioInit();
void powerGuardLoop();
bool powerGuardWantsPowerSaving();
bool powerGuardHandleCli(const ModCliContext& context, char* command, char* reply);
