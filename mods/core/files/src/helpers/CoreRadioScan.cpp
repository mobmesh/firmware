#include <Arduino.h>
#include <helpers/CoreIntegration.h>

bool coreRadioRssiNow(float* out) {
  uint32_t asked = millis();
  while (modRxMode(MOD_RX_PAUSE, nullptr) == 1) {   // a duty-cycled receiver opens a settled continuous window first
    if (millis() - asked > 250) {
      modRxMode(MOD_RX_RESUME, nullptr);
      return false;
    }
    delay(2);
  }
  bool ok = !modRadioIsReceiving();
  if (ok) *out = modRadioCurrentRssi();
  modRxMode(MOD_RX_RESUME, nullptr);
  return ok;
}

void coreRadioScanEnter(float bw) {
  modRadioIdle();
  modRadioSetScanBandwidth(bw);
}

float coreRadioScanRead(float mhz) {
  modRadioRawStandby();
  modRadioRawTune(mhz);
  modRadioRawStartReceive();
  delayMicroseconds(5000);
  float sum = 0;
  for (int i = 0; i < 16; i++) {
    sum += modRadioRawRssi();
    delayMicroseconds(100);
  }
  return sum / 16;
}

void coreRadioScanLeave(float freq, float bw, uint8_t sf, uint8_t cr) {
  modRadioIdle();
  modRadioSetParams(freq, bw, sf, cr);
  modRadioResetAgc();
}
