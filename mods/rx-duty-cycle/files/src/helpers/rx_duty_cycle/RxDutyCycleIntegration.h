#pragma once

#include <stdint.h>
#include <helpers/ModHooks.h>

// rx-duty-cycle's hooks: the receive-mode owner, the AGC reset and `set`/`get radio.rxdc`.
int16_t rxdcRxMode(ModRxEvent event, PhysicalLayer* radio);
void rxdcAgcReset();
bool rxdcHandleCli(const ModCliContext& context, char* command, char* reply);
