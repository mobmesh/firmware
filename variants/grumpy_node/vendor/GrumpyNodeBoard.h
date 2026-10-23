#pragma once

#include <Arduino.h>
#include <helpers/ESP32Board.h>

class GrumpyNodeBoard : public ESP32Board {
public:
  void begin();
  void enterDeepSleep(uint32_t seconds, int wake_pin = -1);
  uint16_t getBattMilliVolts() override;
  const char *getManufacturerName() const override;

private:
  void enableRadio();
  bool readFuelGaugeRegister(uint8_t register_address, uint16_t &value);
};
