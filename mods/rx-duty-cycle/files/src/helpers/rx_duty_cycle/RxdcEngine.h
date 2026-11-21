#pragma once

#include <stdint.h>
#include <helpers/rx_duty_cycle/RxdcGeometry.h>

// Receive duty cycle control: when to arm, stop, pause and calibrate. Pure; the integration supplies the radio.

#define RXDC_PASS            1         // the wrapper starts upstream's continuous receive
#define RXDC_RESTART         1         // loop flags, as the shim reads them
#define RXDC_FRESH_FLOOR     2
#define RXDC_MAX_ARM_FAILS   3         // in a row, then continuous receive until settings change
#define RXDC_CALIB_EVERY_MS  60000UL   // a noise-floor batch from continuous receive at least this often
#define RXDC_CALIB_SETTLE_MS 20UL      // readings straight after entering receive are garbage
#define RXDC_CALIB_LIMIT_MS  5000UL    // a batch that cannot finish (busy channel) gives up
#define RXDC_PAUSE_LIMIT_MS  300UL     // a window whose reader never says it is done closes anyway
#define RXDC_REARM_LIMIT_MS  500UL     // a restart the wrapper could not apply stops holding the CPU

class RxdcRadio {
public:
  virtual ~RxdcRadio() = default;
  virtual int16_t arm(uint32_t rx_us, uint32_t sleep_us) = 0;   // RadioLib status
  virtual int16_t continuous() = 0;
  virtual void stop() = 0;                                      // standby, sleep timer stopped and its event cleared
  virtual bool busy() = 0;                                      // BUSY pin: high through the sleep phase
  virtual uint8_t sf() = 0;
  virtual float bw() = 0;
  virtual uint32_t transitionUs() = 0;                          // TCXO delay + 1000, as RadioLib subtracts it
};

enum RxdcState : uint8_t {
  RXDC_S_OFF,                          // mode off, or power saving off
  RXDC_S_ARMED,
  RXDC_S_CALIBRATING,
  RXDC_S_PAUSED,
  RXDC_S_NO_GEOMETRY,                  // these radio settings have no safe pair
  RXDC_S_FALLBACK,                     // arming failed too often
};

// `set radio.rxdc`: auto follows power saving, on cycles whatever power saving says, off never cycles.
enum RxdcMode : uint8_t { RXDC_MODE_OFF = 0, RXDC_MODE_AUTO = 1, RXDC_MODE_ON = 2 };

class RxdcEngine {
public:
  void setMode(RxdcMode mode);         // `set radio.rxdc auto|on|off`
  RxdcMode mode() const { return _mode; }
  void setPowerSaving(bool on) { _power = on; }

  int16_t start(RxdcRadio& r, uint32_t now_ms);
  void stop(RxdcRadio& r);
  bool asleep(RxdcRadio& r);
  bool sampleOk(uint32_t now_ms) const;
  int16_t loop(RxdcRadio& r, uint32_t now_ms);
  void params(RxdcRadio& r);
  int16_t pauseState(uint32_t now_ms);         // any task: 0 read now, 1 wait (a window is requested or settling)
  void pauseNow(RxdcRadio& r, uint32_t now_ms); // the radio's own task: open the window at once
  void resume();                                // any task: the reading is done
  void resumeNow(RxdcRadio& r, uint32_t now_ms);// the radio's own task: re-arm at once
  void floorDone(uint32_t now_ms);
  void agcReset(RxdcRadio& r);
  bool busyHold() const { return _calib || _paused || _pause_req || _restart || _rearm; }

  RxdcState state() const;
  const RxdcGeometry& geometry() const { return _geo; }
  uint32_t arms() const { return _arms; }
  uint32_t armFailures() const { return _fail_total; }
  int16_t lastError() const { return _err; }
  uint32_t lastCalibMs() const { return _calib_last; }

private:
  bool wanted() const { return _mode == RXDC_MODE_ON || (_mode == RXDC_MODE_AUTO && _power); }
  bool refreshGeometry(RxdcRadio& r);
  void openPause(RxdcRadio& r, uint32_t now_ms);

  RxdcMode _mode = RXDC_MODE_AUTO;
  bool _power = false, _active = false;
  bool _armed = false, _geo_ok = false, _geo_dirty = true;
  bool _calib = false, _calib_due = true, _restart = false, _rearm = false;
  volatile bool _paused = false, _pause_req = false, _pause_done = false;   // shared with readers on other tasks
  uint8_t _fails = 0;
  uint32_t _arms = 0, _fail_total = 0;
  int16_t _err = 0;
  uint32_t _calib_start = 0, _calib_last = 0, _rearm_since = 0;
  volatile uint32_t _pause_start = 0;
  RxdcGeometry _geo = {0, 0, 0};
};
