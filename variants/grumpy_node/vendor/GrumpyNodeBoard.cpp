#include "GrumpyNodeBoard.h"

#include <Wire.h>
#include <driver/gpio.h>
#include <esp_sleep.h>

namespace {
constexpr uint8_t kFuelGaugeAddress = 0x36;
constexpr uint8_t kVCellRegister = 0x02;
constexpr uint32_t kI2cTimeoutMs = 20;
constexpr uint64_t kMicrosPerSecond = 1000000ULL;
} // namespace

void GrumpyNodeBoard::begin() {
  // GPIO21 is UART0 TX after reset. Claim it before initializing peripherals so
  // the E22P power amplifier remains enabled throughout startup.
  gpio_deep_sleep_hold_dis();
  enableRadio();
  ESP32Board::begin();

  // A disconnected or unhealthy I2C device must not stall the main loop.
  Wire.setTimeOut(kI2cTimeoutMs);

  if (esp_reset_reason() == ESP_RST_DEEPSLEEP) {
    const uint64_t wakeup_source = esp_sleep_get_gpio_wakeup_status();
    if ((wakeup_source & (1ULL << P_LORA_DIO_1)) != 0) {
      startup_reason = BD_STARTUP_RX_PACKET;
    }
  }
}

void GrumpyNodeBoard::enterDeepSleep(uint32_t seconds, int wake_pin) {
  gpio_set_direction(static_cast<gpio_num_t>(P_LORA_DIO_1), GPIO_MODE_INPUT);

  uint64_t wakeup_mask = 1ULL << P_LORA_DIO_1;
  if (wake_pin >= 0 && wake_pin < GPIO_NUM_MAX && GPIO_IS_VALID_GPIO(wake_pin)) {
    gpio_set_direction(static_cast<gpio_num_t>(wake_pin), GPIO_MODE_INPUT);
    wakeup_mask |= 1ULL << wake_pin;
  }

  // The radio must remain powered for DIO1 to wake the ESP32-C3.
  enableRadio();
  gpio_hold_en(static_cast<gpio_num_t>(PIN_RADIO_EN));
  gpio_deep_sleep_hold_en();

  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
  esp_deep_sleep_enable_gpio_wakeup(wakeup_mask, ESP_GPIO_WAKEUP_GPIO_HIGH);

  if (seconds > 0) {
    esp_sleep_enable_timer_wakeup(static_cast<uint64_t>(seconds) * kMicrosPerSecond);
  }

  esp_deep_sleep_start();
}

uint16_t GrumpyNodeBoard::getBattMilliVolts() {
  uint16_t raw_vcell = 0;
  if (!readFuelGaugeRegister(kVCellRegister, raw_vcell)) {
    return 0;
  }

  // MAX17048 VCELL is a 12-bit value with a 1.25 mV least-significant bit.
  return static_cast<uint16_t>((static_cast<uint32_t>(raw_vcell >> 4) * 5U) / 4U);
}

const char *GrumpyNodeBoard::getManufacturerName() const {
  return "Grumpy Node";
}

void GrumpyNodeBoard::enableRadio() {
  const gpio_num_t enable_pin = static_cast<gpio_num_t>(PIN_RADIO_EN);

  // Stage the active state before releasing a deep-sleep hold. Releasing a
  // held pin while it is still in its reset state would briefly drive EN low.
  gpio_reset_pin(enable_pin);
  gpio_set_direction(enable_pin, GPIO_MODE_OUTPUT);
  gpio_set_level(enable_pin, 1);
  gpio_hold_dis(enable_pin);
}

bool GrumpyNodeBoard::readFuelGaugeRegister(uint8_t register_address, uint16_t &value) {
  Wire.beginTransmission(kFuelGaugeAddress);
  Wire.write(register_address);
  if (Wire.endTransmission(false) != 0) {
    return false;
  }

  const uint8_t bytes_read = Wire.requestFrom(kFuelGaugeAddress, static_cast<uint8_t>(2));
  if (bytes_read != 2 || Wire.available() < 2) {
    while (Wire.available() > 0) {
      Wire.read();
    }
    return false;
  }

  value = static_cast<uint16_t>(Wire.read()) << 8;
  value |= static_cast<uint16_t>(Wire.read());
  return true;
}
