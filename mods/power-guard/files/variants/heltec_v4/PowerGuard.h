#pragma once

// heltec_v4 power-down for power-guard's deep sleeps; the mod's policy stays
// board-independent. A missed hold release leaves a node that transmits and is never heard.

// No file-scope state here: two translation units include this, so a static would
// become two independent copies. State belongs in ModHooks.cpp, as the boot counters do.

#include <Arduino.h>
#include <SPI.h>
#include <driver/rtc_io.h>
#include <target.h>   // board, radio_driver

// Vfem (TLV75733PDBVR, U3). EN carries a 5.1M pull-up to VDD_3V3, so at sleep entry the
// pull-up switches the rail back on -- driven low AND latched, not merely written low.
static inline void powerGuardFemOff() {
  // CSD low before its rail drops, so no logic high is driven into an unpowered input.
  board.loRaFEMControl.setSleepModeEnable();
  digitalWrite(P_LORA_PA_POWER, LOW);
  rtc_gpio_hold_en((gpio_num_t)P_LORA_PA_POWER);
}

// NSS high and latched after the radio is already asleep, which blocks enterDeepSleep()'s
// own powerOff() and hold harmlessly -- its ordering stops mattering.
static inline void powerGuardHoldNss() {
  digitalWrite(P_LORA_NSS, HIGH);
  rtc_gpio_hold_en((gpio_num_t)P_LORA_NSS);
}

// Pre-radio_init: the SPI bus was never begun, so radio_driver.powerOff() silently does
// nothing. SetSleep (0x84) is valid straight from STDBY_RC and needs no TCXO or PLL.
static inline void powerGuardDownPreRadio() {
  powerGuardFemOff();

  // Level before direction: pinMode(OUTPUT) drives whatever the output register holds,
  // never written on this path, so setting the level first stops NSS asserting briefly.
  digitalWrite(P_LORA_NSS, HIGH);
  pinMode(P_LORA_NSS, OUTPUT);
  pinMode(P_LORA_BUSY, INPUT);
  {
    SPIClass spi(FSPI);
    spi.begin(P_LORA_SCLK, P_LORA_MISO, P_LORA_MOSI);
    uint32_t t0 = millis();
    while (digitalRead(P_LORA_BUSY) && (millis() - t0) < 10) { }
    if (digitalRead(P_LORA_BUSY)) {
      // Command would be discarded and the chip left in STDBY_RC at ~600uA, with
      // nothing else to say so.
      Serial.println("power-guard: SX1262 BUSY stuck, sleep command not issued");
    } else {
      spi.beginTransaction(SPISettings(2000000, MSBFIRST, SPI_MODE0));
      digitalWrite(P_LORA_NSS, LOW);
      spi.transfer(0x84);   // SetSleep
      spi.transfer(0x00);   // cold start, no RTC wake
      digitalWrite(P_LORA_NSS, HIGH);   // sleep begins on this edge
      spi.endTransaction();
    }
    spi.end();
  }
  powerGuardHoldNss();
}

// Post-radio_init path. The bus is up, so upstream's own sleep works -- call it here
// rather than leaving it to enterDeepSleep, then latch NSS behind it.
static inline void powerGuardDownPostRadio() {
  powerGuardFemOff();
  radio_driver.powerOff();
  powerGuardHoldNss();
}

// First thing on every boot, whatever the reset reason, so every path out leaves the pins
// usable. No-ops today -- LoRaFEMControl::init() and HeltecV4Board::begin() already ran.
static inline void powerGuardReleaseHolds() {
  rtc_gpio_hold_dis((gpio_num_t)P_LORA_PA_POWER);
  rtc_gpio_hold_dis((gpio_num_t)P_LORA_NSS);
}
