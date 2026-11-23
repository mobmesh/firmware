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
#include <Mesh.h>
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
  bool timeout_enabled = false;
  int16_t arm(uint32_t rx_us, uint32_t sleep_us) override {
    const uint8_t symbols = timeout_enabled ? rxdcSymbolTimeout(rx_us, sf(), bw(), sf() <= 8 ? 32 : 16) : 0;
    if (symbols != 0) {
      int16_t err = r->standby();
      if (err != RADIOLIB_ERR_NONE) return err;
      timeout_set = true;
      err = setSymbolTimeout(symbols);
      if (err != RADIOLIB_ERR_NONE) return err;
    }
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
    if (timeout_set) {
      const int16_t err = setSymbolTimeout(0);
      timeout_set = err != RADIOLIB_ERR_NONE;
      if (timeout_set) MESH_DEBUG_PRINTLN("RXDC symbol timeout clear failed: %d", err);
    }
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

private:
  bool timeout_set = false;

  int16_t setSymbolTimeout(uint8_t symbols) {
    const int16_t err = r->getMod()->SPIwriteStream(RADIOLIB_SX126X_CMD_SET_LORA_SYMB_NUM_TIMEOUT,
                                                    &symbols, 1);
    if (err != RADIOLIB_ERR_NONE || symbols == 0) return err;
    // The 96-symbol sync timeout encodes mantissa 12 and exponent 1.
    const uint8_t sync_timeout = 0x61;
    return r->writeRegister(RADIOLIB_SX126X_REG_LORA_SYNC_TIMEOUT, &sync_timeout, 1);
  }
};

static Sx1262Radio radio_io;
#endif

#if defined(MOBMESH_HAS_SX1262) && defined(P_LORA_KCT8103L_PA_CTX) && defined(CONFIG_IDF_TARGET_ESP32S3)
#define RXDC_FEM_GATE 1
#include <soc/rtc_io_reg.h>
#include <driver/rtc_io.h>
#include <esp_sleep.h>
#include <ulp_common.h>
#include <esp32s3/ulp.h>
#endif

static RxdcEngine engine;
static bool booted;
static bool fem_gate = true;   // `set radio.fem.rxdc`; on by default, acts only where the LNA can be switched

#if defined(RXDC_FEM_GATE)
// During CPU light sleep only, the ULP-FSM copies BUSY (high while the radio sleeps) onto the KCT8103L's CTX (high = LNA
// bypass). Both pads are RTC-muxed just before the sleep and handed back on wake, so the ULP never owns CTX while the
// CPU can transmit. The Arduino core ships the ULP disabled with no reserved RTC memory, so the program sits at the top
// of RTC slow memory, above what the app places there (checked at run time).
#define FEM_ULP_PROG_W 0x780   // program at 0x50001E00
#define FEM_ULP_FLAG_W 0x7FC   // [armed, running] at 0x50001FF0
extern int _rtc_force_slow_end;
static bool fem_loaded = false, fem_active = false;
static uint32_t fem_sleeps = 0, fem_late = 0;
static inline volatile uint32_t* femWord(uint32_t w) { return (volatile uint32_t*)(0x50000000UL + w * 4); }

static bool femRtcFree() { return (uintptr_t)&_rtc_force_slow_end <= 0x50000000UL + FEM_ULP_PROG_W * 4; }

static void femLoad() {
  const uint32_t busy = rtc_io_number_get((gpio_num_t)P_LORA_BUSY);
  const uint32_t ctx = rtc_io_number_get((gpio_num_t)P_LORA_KCT8103L_PA_CTX);
  const ulp_insn_t prog[] = {
    I_MOVI(R2, FEM_ULP_FLAG_W),
    I_LD(R0, R2, 0),                                                                            // 1: armed?
    I_BL(7, 1),                                                                                 // no: to 9
    I_RD_REG(RTC_GPIO_IN_REG, RTC_GPIO_IN_NEXT_S + busy, RTC_GPIO_IN_NEXT_S + busy),            // BUSY
    I_BL(3, 1),                                                                                 // low, listening: to 7
    I_WR_REG(RTC_GPIO_OUT_W1TS_REG, RTC_GPIO_OUT_DATA_W1TS_S + ctx, RTC_GPIO_OUT_DATA_W1TS_S + ctx, 1),  // asleep: bypass
    I_BXI(FEM_ULP_PROG_W + 1),
    I_WR_REG(RTC_GPIO_OUT_W1TC_REG, RTC_GPIO_OUT_DATA_W1TC_S + ctx, RTC_GPIO_OUT_DATA_W1TC_S + ctx, 1),  // listening: LNA on
    I_BXI(FEM_ULP_PROG_W + 1),
    I_MOVI(R0, 0),                                                                              // 9: disarmed
    I_ST(R0, R2, 1),                                                                            // running = 0
    I_HALT(),
  };
  memcpy((void*)femWord(FEM_ULP_PROG_W), prog, sizeof(prog));
  fem_loaded = true;
}

// Why the gate is not acting, or nullptr when it would act on the next sleep.
static const char* femInactive() {
  if (!femRtcFree()) return "RTC memory in use";
  if (!modFemLnaAvailable()) return "no switchable LNA";
  if (!modFemLnaGet()) return "LNA off";
  if (engine.state() != RXDC_S_ARMED) return "rxdc not armed";
  return nullptr;
}

// Pins back to digital GPIO with the LNA on, the ULP halted: the state every awake moment needs.
static void femRelease() {
  *femWord(FEM_ULP_FLAG_W) = 0;
  uint32_t t0 = micros();
  while ((*femWord(FEM_ULP_FLAG_W + 1) & 0xFFFF) != 0 && micros() - t0 < 2000) {}
  if ((*femWord(FEM_ULP_FLAG_W + 1) & 0xFFFF) != 0) fem_late++;
  ulp_timer_stop();
  // Back to digital either way: an RTC_GPIO write after this cannot reach the pad.
  rtc_gpio_deinit((gpio_num_t)P_LORA_KCT8103L_PA_CTX);
  digitalWrite(P_LORA_KCT8103L_PA_CTX, LOW);
  rtc_gpio_deinit((gpio_num_t)P_LORA_BUSY);
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_AUTO);
  fem_active = false;
}

static void femSleep() {
  if (!fem_gate || fem_active || femInactive() != nullptr) return;
  if (!fem_loaded) femLoad();
  rtc_gpio_init((gpio_num_t)P_LORA_BUSY);
  rtc_gpio_set_direction((gpio_num_t)P_LORA_BUSY, RTC_GPIO_MODE_INPUT_ONLY);
  rtc_gpio_init((gpio_num_t)P_LORA_KCT8103L_PA_CTX);
  rtc_gpio_set_direction((gpio_num_t)P_LORA_KCT8103L_PA_CTX, RTC_GPIO_MODE_OUTPUT_ONLY);
  rtc_gpio_set_level((gpio_num_t)P_LORA_KCT8103L_PA_CTX, 0);
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
  *femWord(FEM_ULP_FLAG_W) = 1;
  *femWord(FEM_ULP_FLAG_W + 1) = 1;
  ulp_set_wakeup_period(0, 100);
  fem_active = true;
  if (ulp_run(FEM_ULP_PROG_W) != ESP_OK) {
    femRelease();
    return;
  }
  fem_sleeps++;
}

static void femWake() {
  if (fem_active) femRelease();
}

// A soft reset keeps the RTC domain: a ULP left running would still own CTX. Halted and released before anything sends.
static void femBoot() {
  ulp_timer_stop();
  *femWord(FEM_ULP_FLAG_W) = 0;
  rtc_gpio_deinit((gpio_num_t)P_LORA_KCT8103L_PA_CTX);
  rtc_gpio_deinit((gpio_num_t)P_LORA_BUSY);
}
#endif

// Older settings can carry only "auto" (1 auto, 0 off).
class RxdcSettingsFile : public ConfigSerializer {
  uint8_t& _mode;
  uint8_t& _legacy;
  uint8_t& _fem;
  uint8_t& _timeout;
public:
  RxdcSettingsFile(uint8_t& mode, uint8_t& legacy, uint8_t& fem, uint8_t& timeout)
    : _mode(mode), _legacy(legacy), _fem(fem), _timeout(timeout) { }
protected:
  void structure() override {
    def("mode", _mode);
    def("auto", _legacy);
    def("femrxdc", _fem);
    def("timeout", _timeout);
  }
};

static const char* modeText(RxdcMode m) {
  return m == RXDC_MODE_OFF ? "off" : "auto";
}

static void boot() {
  booted = true;
#if defined(RXDC_FEM_GATE)
  femBoot();
#endif
  uint8_t mode = 0xFF, legacy = 1, fem = 1, timeout = 0;
  if (ModFS.begin(true) && ModFS.exists(SETTINGS_PATH)) {
    File f = ModFS.open(SETTINGS_PATH, "r");
    if (f) {
      RxdcSettingsFile file(mode, legacy, fem, timeout);
      file.loadSerial(f);
      f.close();
    }
  }
  engine.setMode(rxdcStoredMode(mode, legacy));
  fem_gate = fem != 0;
#if defined(MOBMESH_HAS_SX1262)
  radio_io.timeout_enabled = timeout == 1;
#endif
}

static bool saveSettings() {
  uint8_t mode = engine.mode();
  uint8_t legacy = engine.mode() == RXDC_MODE_OFF ? 0 : 1;
  uint8_t fem = fem_gate ? 1 : 0;
  uint8_t timeout = 0;
#if defined(MOBMESH_HAS_SX1262)
  timeout = radio_io.timeout_enabled;
#endif
  File f = ModFS.open(SETTINGS_PATH, "w", true);
  if (!f) return false;
  RxdcSettingsFile file(mode, legacy, fem, timeout);
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
    case MOD_RX_STOP:
#if defined(RXDC_FEM_GATE)
      femWake();   // never expected awake; the radio only stops with the CPU running
#endif
      engine.stop(radio_io);
      return 0;
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
#if defined(RXDC_FEM_GATE)
    case MOD_RX_SLEEP:      femSleep(); return 0;
    case MOD_RX_WAKE:       femWake(); return 0;
#else
    case MOD_RX_SLEEP:
    case MOD_RX_WAKE:       return 0;
#endif
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

// "<verb> <key>[ <arg>]" for exactly this key; nothing else under "set radio" or "get radio" belongs to this mod.
static bool isKey(const char* command, const char* verb, const char* key, const char** arg) {
  size_t v = strlen(verb), k = strlen(key);
  if (strncmp(command, verb, v) != 0 || command[v] != ' ' || strncmp(command + v + 1, key, k) != 0) return false;
  char next = command[v + 1 + k];
  if (next != 0 && next != ' ') return false;
  *arg = next == ' ' ? command + v + 2 + k : "";
  return true;
}

// The first word of a value, so the full `get` line (as tryset snapshots it) can be given back.
static void firstWord(const char* arg, char* word, size_t len) {
  size_t n = 0;
  while (arg[n] && arg[n] != ' ' && arg[n] != ',' && n < len - 1) { word[n] = arg[n]; n++; }
  word[n] = 0;
}

static bool femCli(bool is_set, const char* arg, char* reply) {
#if defined(RXDC_FEM_GATE)
  if (is_set) {
    char word[8];
    firstWord(arg, word, sizeof(word));
    if (strcmp(word, "on") != 0 && strcmp(word, "off") != 0) {
      strcpy(reply, "ERR: usage: set radio.fem.rxdc on|off");
      return true;
    }
    fem_gate = word[1] == 'n';
    sprintf(reply, saveSettings() ? "OK - radio.fem.rxdc %s" : "ERR: radio.fem.rxdc %s, not saved", fem_gate ? "on" : "off");
    return true;
  }
  if (!fem_gate) {
    strcpy(reply, "> off");
    return true;
  }
  const char* why = femInactive();
  if (why) snprintf(reply, 160, "> on, inactive (%s), %lu gated sleeps", why, (unsigned long)fem_sleeps);
  else snprintf(reply, 160, "> on, %lu gated sleeps", (unsigned long)fem_sleeps);
#else
  (void)is_set;
  (void)arg;
  strcpy(reply, "ERR: radio.fem.rxdc not supported on this board");
#endif
  return true;
}

bool rxdcHandleCli(const ModCliContext& context, char* command, char* reply) {
  (void)context;
  const char* arg;
  bool is_set = isKey(command, "set", "radio.fem.rxdc", &arg);
  if (is_set || isKey(command, "get", "radio.fem.rxdc", &arg)) {
#if defined(MOBMESH_HAS_SX1262)
    if (!booted) boot();
#endif
    return femCli(is_set, arg, reply);
  }
  is_set = isKey(command, "set", "radio.rxdc.timeout", &arg);
  if (is_set || isKey(command, "get", "radio.rxdc.timeout", &arg)) {
#if defined(MOBMESH_HAS_SX1262)
    if (!booted) boot();
    if (is_set) {
      char word[8];
      firstWord(arg, word, sizeof(word));
      if (strcmp(word, "on") != 0 && strcmp(word, "off") != 0) {
        strcpy(reply, "ERR: usage: set radio.rxdc.timeout on|off");
        return true;
      }
      bool enabled = word[1] == 'n';
      if (radio_io.timeout_enabled != enabled) {
        radio_io.timeout_enabled = enabled;
        engine.requestRearm();
      }
      sprintf(reply, saveSettings() ? "OK - radio.rxdc.timeout %s" : "ERR: radio.rxdc.timeout %s, not saved", word);
    } else {
      sprintf(reply, "> %s", radio_io.timeout_enabled ? "on" : "off");
    }
#else
    strcpy(reply, "ERR: rx duty cycle needs an SX1262");
#endif
    return true;
  }
  is_set = isKey(command, "set", "radio.rxdc", &arg);
  if (!is_set && !isKey(command, "get", "radio.rxdc", &arg)) return false;
#if defined(MOBMESH_HAS_SX1262)
  if (!booted) boot();
  if (is_set) {
    char word[8];
    firstWord(arg, word, sizeof(word));
    RxdcMode m;
    if (strcmp(word, "auto") == 0) m = RXDC_MODE_AUTO;
    else if (strcmp(word, "off") == 0) m = RXDC_MODE_OFF;
    else {
      strcpy(reply, "ERR: usage: set radio.rxdc auto|off");
      return true;
    }
    engine.setMode(m);
    sprintf(reply, saveSettings() ? "OK - radio.rxdc %s" : "ERR: radio.rxdc %s, not saved", modeText(m));
    return true;
  }
  RxdcMode m = engine.mode();
  if (m == RXDC_MODE_OFF) {
    strcpy(reply, "> off");
    return true;
  }
  RxdcState s = engine.state();
  const RxdcGeometry& g = engine.geometry();
  uint32_t calib_s = engine.lastCalibMs() ? (millis() - engine.lastCalibMs()) / 1000 : 0;
  if (s == RXDC_S_ARMED || s == RXDC_S_CALIBRATING || s == RXDC_S_PAUSED) {
    snprintf(reply, 160, "> %s, %s, rx %luus sleep %luus, arms %lu, fails %lu, calib %lus ago", modeText(m), stateText(s),
             (unsigned long)g.rx_us, (unsigned long)g.sleep_us, (unsigned long)engine.arms(), (unsigned long)engine.armFailures(),
             (unsigned long)calib_s);
  } else if (s == RXDC_S_FALLBACK) {
    snprintf(reply, 160, "> %s, %s, err %d", modeText(m), stateText(s), engine.lastError());
  } else {
    snprintf(reply, 160, "> %s, %s", modeText(m), stateText(s));
  }
#else
  (void)arg;
  strcpy(reply, "ERR: rx duty cycle needs an SX1262");
#endif
  return true;
}
