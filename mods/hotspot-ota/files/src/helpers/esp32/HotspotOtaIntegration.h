#pragma once

#include <helpers/ModHooks.h>

void hotspotOtaBeforeRadioInit();
bool hotspotOtaRadioInit(const char* build_id);
void hotspotOtaLoop();
bool hotspotOtaHandleCli(const ModCliContext& context, char* command, char* reply);
