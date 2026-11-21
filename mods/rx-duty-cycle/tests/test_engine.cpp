#include <helpers/rx_duty_cycle/RxdcEngine.h>

#include "check.h"

class FakeRadio : public RxdcRadio {
public:
  int arms = 0, conts = 0, stops = 0;
  int16_t arm_result = 0;
  bool busy_pin = false;
  uint8_t sf_ = 7;
  int16_t arm(uint32_t, uint32_t) override { arms++; return arm_result; }
  int16_t continuous() override { conts++; return 0; }
  void stop() override { stops++; }
  bool busy() override { return busy_pin; }
  uint8_t sf() override { return sf_; }
  float bw() override { return 62.5f; }
  uint32_t transitionUs() override { return 6000; }
};

int main() {
  FakeRadio r;
  RxdcEngine e;
  uint32_t t = 1000;

  // Power saving off: upstream's continuous receive, the wrapper samples freely, no hold.
  CHECK(e.start(r, t) == RXDC_PASS && r.arms == 0 && e.state() == RXDC_S_OFF);
  CHECK(e.sampleOk(t) && !e.busyHold());

  // Power saving on: the loop asks for a restart, the start arms, and the armed radio is never sampled.
  e.setPowerSaving(true);
  CHECK(e.loop(r, t) & RXDC_RESTART);
  CHECK(e.start(r, t) == 0 && r.arms == 1 && e.state() == RXDC_S_ARMED);
  CHECK(!e.sampleOk(t));
  r.busy_pin = true;
  CHECK(e.asleep(r));
  r.busy_pin = false;
  CHECK(!e.asleep(r));

  // First armed loop opens a calibration: restart into continuous receive with a fresh batch, CPU held awake.
  int16_t f = e.loop(r, t + 10);
  CHECK((f & RXDC_RESTART) && (f & RXDC_FRESH_FLOOR) && e.busyHold());
  CHECK(e.start(r, t + 11) == RXDC_PASS && e.state() == RXDC_S_CALIBRATING);
  CHECK(!e.sampleOk(t + 20) && e.sampleOk(t + 11 + RXDC_CALIB_SETTLE_MS));
  // The published floor ends it at once and the next loop re-arms.
  e.floorDone(t + 60);
  CHECK(e.busyHold() && (e.loop(r, t + 61) & RXDC_RESTART) && e.busyHold());
  CHECK(e.start(r, t + 62) == 0 && r.arms == 2 && !e.busyHold());
  // No new window until a minute after the last one.
  CHECK(e.loop(r, t + 30000) == 0);
  CHECK(e.loop(r, t + 60 + RXDC_CALIB_EVERY_MS) & RXDC_FRESH_FLOOR);
  // A batch the channel never lets finish gives up after five seconds.
  uint32_t c0 = t + 60 + RXDC_CALIB_EVERY_MS;
  e.start(r, c0);
  CHECK(e.loop(r, c0 + RXDC_CALIB_LIMIT_MS - 1) == 0 && e.busyHold());
  CHECK((e.loop(r, c0 + RXDC_CALIB_LIMIT_MS) & RXDC_RESTART) && e.busyHold());
  t = c0 + RXDC_CALIB_LIMIT_MS + 1;
  CHECK(e.start(r, t) == 0 && !e.busyHold());

  // A reader on another task asks for a window; only the radio's own loop switches the radio, then holds the CPU until re-armed.
  int conts = r.conts;
  CHECK(e.pauseState(t) == 1 && e.busyHold() && r.conts == conts);
  CHECK(e.loop(r, t + 1) == 0 && r.conts == conts + 1 && e.state() == RXDC_S_PAUSED);
  CHECK(e.pauseState(t + 5) == 1 && e.pauseState(t + 1 + RXDC_CALIB_SETTLE_MS) == 0);
  CHECK(e.sampleOk(t + 1 + RXDC_CALIB_SETTLE_MS));
  e.resume();
  CHECK(e.busyHold());
  CHECK((e.loop(r, t + 30) & RXDC_RESTART) && e.busyHold());
  CHECK(e.start(r, t + 31) == 0 && !e.busyHold());
  // A reader that never says it is done: the window closes on its own.
  t += 100;
  e.pauseState(t);
  e.loop(r, t);
  CHECK(e.loop(r, t + RXDC_PAUSE_LIMIT_MS - 1) == 0);
  CHECK(e.loop(r, t + RXDC_PAUSE_LIMIT_MS) & RXDC_RESTART);
  t += RXDC_PAUSE_LIMIT_MS + 1;
  CHECK(e.start(r, t) == 0);
  // A request withdrawn before the window opened (the reader timed out) opens nothing.
  e.pauseState(t);
  e.resume();
  conts = r.conts;
  CHECK(e.loop(r, t + 1) == 0 && r.conts == conts && !e.busyHold());
  // Called on the radio's own task: the window opens at once and closes straight back into the duty cycle.
  int arms = r.arms;
  e.pauseNow(r, t + 2);
  CHECK(e.state() == RXDC_S_PAUSED && e.pauseState(t + 2) == 1 && e.pauseState(t + 2 + RXDC_CALIB_SETTLE_MS) == 0);
  e.resumeNow(r, t + 30);
  CHECK(r.arms == arms + 1 && e.state() == RXDC_S_ARMED && !e.busyHold());
  // Not duty-cycling: read straight away, no window.
  e.setMode(RXDC_MODE_OFF);
  e.loop(r, t + 40);
  e.start(r, t + 41);
  CHECK(e.pauseState(t + 42) == 0 && !e.busyHold());
  e.setMode(RXDC_MODE_AUTO);
  e.loop(r, t + 43);
  e.start(r, t + 44);
  t += 50;

  // A restart the wrapper could not apply stops holding the CPU after a while.
  e.params(r);
  CHECK((e.loop(r, t) & RXDC_RESTART) && e.busyHold());
  CHECK(e.loop(r, t + RXDC_REARM_LIMIT_MS) == 0 && !e.busyHold());
  e.start(r, t + RXDC_REARM_LIMIT_MS + 1);

  // An AGC reset stops the cycle and calibrates again, since the wrapper zeroed its floor.
  int stops = r.stops;
  e.agcReset(r);
  CHECK(r.stops == stops + 1);
  CHECK(e.start(r, t) == 0);
  CHECK(e.loop(r, t + 1) & RXDC_FRESH_FLOOR);
  e.floorDone(t + 2);
  e.loop(r, t + 3);
  e.start(r, t + 3);

  // Three failed arms in a row fall back to continuous receive (never deaf), and stay there until settings change.
  r.arm_result = -708;
  int before = r.conts;
  for (int i = 0; i < RXDC_MAX_ARM_FAILS; i++) e.start(r, t + 10 + i);
  CHECK(r.conts == before + RXDC_MAX_ARM_FAILS && e.armFailures() == RXDC_MAX_ARM_FAILS && e.lastError() == -708);
  CHECK(e.state() == RXDC_S_FALLBACK);
  arms = r.arms;
  CHECK(e.start(r, t + 20) == RXDC_PASS && r.arms == arms);
  r.arm_result = 0;
  e.params(r);
  CHECK(e.loop(r, t + 21) & RXDC_RESTART);
  CHECK(e.start(r, t + 22) == 0 && e.state() == RXDC_S_ARMED);

  // Settings with no safe pair (SF7 at 500 kHz) run continuous and say why.
  struct WideRadio : FakeRadio { float bw() override { return 500.0f; } } w;
  RxdcEngine e2;
  e2.setPowerSaving(true);
  CHECK(e2.start(w, t) == RXDC_PASS && w.arms == 0 && e2.state() == RXDC_S_NO_GEOMETRY);

  // `set radio.rxdc off` keeps continuous receive with power saving on.
  e.setMode(RXDC_MODE_OFF);
  CHECK(e.loop(r, t + 30) & RXDC_RESTART);
  CHECK(e.start(r, t + 31) == RXDC_PASS && e.state() == RXDC_S_OFF && e.sampleOk(t + 31));

  // `set radio.rxdc on` cycles with power saving off; auto then stops until power saving returns.
  e.setPowerSaving(false);
  e.setMode(RXDC_MODE_ON);
  CHECK(e.loop(r, t + 40) & RXDC_RESTART);
  CHECK(e.start(r, t + 41) == 0 && e.state() != RXDC_S_OFF);
  e.setMode(RXDC_MODE_AUTO);
  CHECK(e.loop(r, t + 50) & RXDC_RESTART);
  CHECK(e.start(r, t + 51) == RXDC_PASS && e.state() == RXDC_S_OFF);
  e.setPowerSaving(true);
  CHECK(e.loop(r, t + 60) & RXDC_RESTART);
  CHECK(e.start(r, t + 61) == 0);
  DONE();
}
