#pragma once

// Board-specific power-down for mods/power-guard's deep sleeps. Everything here is
// heltec_v4 hardware; the mod's policy stays board-independent. A board that ships no
// PowerGuard.h simply does not call these.
//
// Both pins are in the S3's RTC range, so one hold mechanism covers them, and both
// releases are ours. A hold survives the wake reset by design, so a missed release
// leaves a node that transmits and is never heard.

// No file-scope state here: two translation units include this, so a static would
// become two independent copies. State belongs in ModHooks.cpp, as the boot counters do.

#include <Arduino.h>
#include <SPI.h>
#include <driver/rtc_io.h>
#include <target.h>   // board, radio_driver

// Vfem, the FEM's own supply (TLV75733PDBVR, schematic U3). EN carries a 5.1M pull-up
// to VDD_3V3, so at sleep entry the pad driver powers down and the pull-up switches
// the rail back on -- it must be driven low AND latched, not merely written low.
//
// Not Vext: that is a separate rail feeding the display, and this board has no RF
// switch to keep alive (the PE4259 belonged to V2.1, not V4).
static inline void powerGuardFemOff() {
  // CSD low before its rail drops, so we are not driving a logic high into an
  // unpowered chip's input while enterDeepSleep() flushes serial.
  board.loRaFEMControl.setSleepModeEnable();
  digitalWrite(P_LORA_PA_POWER, LOW);
  rtc_gpio_hold_en((gpio_num_t)P_LORA_PA_POWER);
}

// NSS high and latched. Doing this ourselves, after the radio is already asleep,
// means enterDeepSleep()'s own powerOff() and hold are harmlessly blocked -- so the
// order it does things in stops being something we depend on.
static inline void powerGuardHoldNss() {
  digitalWrite(P_LORA_NSS, HIGH);
  rtc_gpio_hold_en((gpio_num_t)P_LORA_NSS);
}

// Pre-radio_init path. The SPI bus was never begun, so radio_driver.powerOff()
// silently does nothing -- we issue SetSleep (0x84) directly, which is valid straight
// from STDBY_RC after reset and needs no TCXO, calibration or PLL.
static inline void powerGuardDownPreRadio() {
  powerGuardFemOff();

  // Level before direction: pinMode(OUTPUT) enables the driver with whatever is in the
  // output register, which on this path has never been written, so setting the level
  // first is what stops NSS asserting for an instant.
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

// Called first thing on every boot, whatever the reset reason, so every path out
// leaves the pins usable. Releasing our own holds rather than relying on
// LoRaFEMControl::init() and HeltecV4Board::begin() to do it: both already have, by
// the time this runs, which makes these no-ops today and correct if that changes.
static inline void powerGuardReleaseHolds() {
  rtc_gpio_hold_dis((gpio_num_t)P_LORA_PA_POWER);
  rtc_gpio_hold_dis((gpio_num_t)P_LORA_NSS);
}
