#include <helpers/rx_duty_cycle/RxDutyCycleIntegration.h>

#include <Arduino.h>
#include <stdio.h>
#include <string.h>
#include <helpers/ModFS.h>
#include <helpers/ConfigSerializer.h>
#include <helpers/rx_duty_cycle/RxdcEngine.h>

#define SETTINGS_PATH "/rxdc.json"

#if defined(MOBMESH_HAS_SX1262)
#include <RadioLib.h>
#if defined(ESP32)
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#else
#include <FreeRTOS.h>
#include <task.h>
#endif

// The task the radio wrapper runs on; mods' loop task asks it for windows rather than touching the radio.
static TaskHandle_t radio_task;
static bool onRadioTask() { return radio_task != nullptr && xTaskGetCurrentTaskHandle() == radio_task; }

// The SX1262 as the engine sees it; the wrapper hands its radio in with every event.
class Sx1262Radio : public RxdcRadio {
public:
  SX126x* r = nullptr;
  int16_t arm(uint32_t rx_us, uint32_t sleep_us) override {
    keepThroughSleep();
    // The preamble flag is reported as upstream's continuous receive does, so listen-before-talk sees a packet starting.
    return r->startReceiveDutyCycle(rx_us, sleep_us, RADIOLIB_IRQ_RX_DEFAULT_FLAGS | (1UL << RADIOLIB_IRQ_PREAMBLE_DETECTED),
                                    RADIOLIB_IRQ_RX_DEFAULT_MASK);
  }
  int16_t continuous() override { return ((PhysicalLayer*)r)->startReceive(); }

  // Every duty-cycle sleep is a warm sleep: registers off the retention list come back at their defaults.
  // The AGC lock-up guard (0x08B5 bit 0) is one of them, so it joins the RX gain register (0x08AC) on the list.
  void keepThroughSleep() {
#if defined(SX126X_REGISTER_PATCH) && SX126X_REGISTER_PATCH
    uint8_t guard = 0;
    if (r->readRegister(0x08B5, &guard, 1) == RADIOLIB_ERR_NONE && !(guard & 0x01)) {
      guard |= 0x01;
      r->writeRegister(0x08B5, &guard, 1);
    }
    const uint8_t keep[] = {0x02, 0x08, 0xAC, 0x08, 0xB5};
    r->writeRegister(RADIOLIB_SX126X_REG_RX_GAIN_RETENTION_0, keep, sizeof(keep));
#endif
  }
  void stop() override {
    r->standby();
    // The sleep timer is stopped and its event cleared, or it can wake the chip into receive later.
    uint8_t zero = 0, ev = 0;
    r->writeRegister(RADIOLIB_SX126X_REG_RTC_CTRL, &zero, 1);
    r->readRegister(RADIOLIB_SX126X_REG_EVENT_MASK, &ev, 1);
    ev |= 0x02;
    r->writeRegister(RADIOLIB_SX126X_REG_EVENT_MASK, &ev, 1);
  }
  bool busy() override { return digitalRead(P_LORA_BUSY) == HIGH; }
  uint8_t sf() override { return r->spreadingFactor; }
  float bw() override { return r->bandwidthKhz; }
  uint32_t transitionUs() override { return r->tcxoDelay + 1000; }
};

static Sx1262Radio radio_io;
#endif

static RxdcEngine engine;
static bool booted;

class SettingsFile : public ConfigSerializer {
  uint8_t& _auto;
public:
  explicit SettingsFile(uint8_t& auto_on) : _auto(auto_on) { }
protected:
  void structure() override {
    def("auto", _auto);
  }
};

static void boot() {
  booted = true;
  uint8_t auto_on = 1;
  if (ModFS.begin(true) && ModFS.exists(SETTINGS_PATH)) {
    File f = ModFS.open(SETTINGS_PATH, "r");
    if (f) {
      SettingsFile file(auto_on);
      file.loadSerial(f);
      f.close();
    }
  }
  engine.setMode(auto_on != 0);
}

static bool saveSettings() {
  uint8_t auto_on = engine.modeAuto() ? 1 : 0;
  File f = ModFS.open(SETTINGS_PATH, "w", true);
  if (!f) return false;
  SettingsFile file(auto_on);
  bool ok = file.saveSerial(f);
  f.close();
  return ok;
}

int16_t rxdcRxMode(ModRxEvent event, PhysicalLayer* radio) {
#if defined(MOBMESH_HAS_SX1262)
  if (!booted) boot();
  if (radio != nullptr) radio_io.r = (SX126x*)radio;
  if (radio_io.r == nullptr) return event == MOD_RX_START ? MOD_RX_PASS : event == MOD_RX_SAMPLE_OK ? 1 : 0;
  uint32_t now = millis();
  switch (event) {
    case MOD_RX_START:      return engine.start(radio_io, now);
    case MOD_RX_STOP:       engine.stop(radio_io); return 0;
    case MOD_RX_ASLEEP:     return engine.asleep(radio_io) ? 1 : 0;
    case MOD_RX_SAMPLE_OK:  return engine.sampleOk(now) ? 1 : 0;
    case MOD_RX_LOOP:
      radio_task = xTaskGetCurrentTaskHandle();
      engine.setPowerSaving(modPowerSavingOn());
      return engine.loop(radio_io, now);
    case MOD_RX_PARAMS:     engine.params(radio_io); return 0;
    case MOD_RX_PAUSE:
      if (onRadioTask()) engine.pauseNow(radio_io, now);
      return engine.pauseState(now);
    case MOD_RX_RESUME:
      if (onRadioTask()) engine.resumeNow(radio_io, now);
      else engine.resume();
      return 0;
    case MOD_RX_FLOOR_DONE: engine.floorDone(now); return 0;
    case MOD_RX_BUSY:       return engine.busyHold() ? 1 : 0;
  }
  return 0;
#else
  (void)radio;
  return event == MOD_RX_START ? MOD_RX_PASS : event == MOD_RX_SAMPLE_OK ? 1 : 0;
#endif
}

void rxdcAgcReset() {
#if defined(MOBMESH_HAS_SX1262)
  if (radio_io.r != nullptr) engine.agcReset(radio_io);
#endif
}

static const char* stateText(RxdcState s) {
  switch (s) {
    case RXDC_S_OFF:         return "idle (power saving off)";
    case RXDC_S_ARMED:       return "armed";
    case RXDC_S_CALIBRATING: return "calibrating";
    case RXDC_S_PAUSED:      return "paused for a reading";
    case RXDC_S_NO_GEOMETRY: return "continuous (no safe timing at these radio settings)";
    case RXDC_S_FALLBACK:    return "continuous (arming failed)";
  }
  return "?";
}

bool rxdcHandleCli(const ModCliContext& context, char* command, char* reply) {
  (void)context;
  if (strncmp(command, "powersaving rxdc", 16) != 0 || (command[16] != 0 && command[16] != ' ')) return false;
#if defined(MOBMESH_HAS_SX1262)
  if (!booted) boot();
  const char* arg = command[16] == ' ' ? command + 17 : "";
  if (strcmp(arg, "auto") == 0 || strcmp(arg, "off") == 0) {
    engine.setMode(arg[0] == 'a');
    sprintf(reply, saveSettings() ? "OK - rxdc %s" : "ERR: rxdc %s, not saved", engine.modeAuto() ? "auto" : "off");
    return true;
  }
  if (*arg != 0) {
    strcpy(reply, "ERR: usage: powersaving rxdc [auto|off]");
    return true;
  }
  if (!engine.modeAuto()) {
    strcpy(reply, "rxdc off");
    return true;
  }
  RxdcState s = engine.state();
  const RxdcGeometry& g = engine.geometry();
  uint32_t calib_s = engine.lastCalibMs() ? (millis() - engine.lastCalibMs()) / 1000 : 0;
  if (s == RXDC_S_ARMED || s == RXDC_S_CALIBRATING || s == RXDC_S_PAUSED) {
    snprintf(reply, 160, "rxdc auto, %s, rx %luus sleep %luus, arms %lu, fails %lu, calib %lus ago", stateText(s),
             (unsigned long)g.rx_us, (unsigned long)g.sleep_us, (unsigned long)engine.arms(), (unsigned long)engine.armFailures(),
             (unsigned long)calib_s);
  } else if (s == RXDC_S_FALLBACK) {
    snprintf(reply, 160, "rxdc auto, %s, err %d", stateText(s), engine.lastError());
  } else {
    snprintf(reply, 160, "rxdc auto, %s", stateText(s));
  }
#else
  strcpy(reply, "ERR: rx duty cycle needs an SX1262");
#endif
  return true;
}
