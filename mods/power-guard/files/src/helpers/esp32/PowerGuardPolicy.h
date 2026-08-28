#pragma once

#include <Arduino.h>
#include <helpers/ModHooks.h>     // the board, without naming a board class
#include <Preferences.h>          // stored overrides

// Engages MeshCore's power-saving sleep when the battery is low, releasing it on recovery.
// Runtime-only: NodePrefs is operator intent and hits flash on every change.

// 0 disables the rung. No default worth having: a threshold is a fact about a board's
// divider, and inheriting another board's measurement power saves at the wrong voltage.
#ifndef POWER_GUARD_AUTO_ON_MV
  #define POWER_GUARD_AUTO_ON_MV 0
#endif
// Wide gap is required: sleeping removes load, which raises the reading.
#ifndef POWER_GUARD_AUTO_OFF_MV
  #define POWER_GUARD_AUTO_OFF_MV 0
#endif
#ifndef POWER_GUARD_SAMPLE_INTERVAL_MS
  #define POWER_GUARD_SAMPLE_INTERVAL_MS 60000
#endif
// Rejects the transient sag under a TX burst.
#ifndef POWER_GUARD_CONSECUTIVE_SAMPLES
  #define POWER_GUARD_CONSECUTIVE_SAMPLES 3
#endif
// Bypass the FEM LNA while saving. Off by default: measured at only ~0.3mA of
// a ~5.5mA sleeping floor, which does not justify the lost RX sensitivity.
#ifndef POWER_GUARD_AUTO_DROP_FEM_LNA
  #define POWER_GUARD_AUTO_DROP_FEM_LNA 0
#endif
// Automatic power saving is opt-in: light sleep drops serial and thins RX, so it
// stays off until an operator asks for it. The safe rung below is independent.
#ifndef POWER_GUARD_AUTO_DEFAULT
  #define POWER_GUARD_AUTO_DEFAULT 0
#endif
// Leave service entirely below this, keeping the reserve a solar pack needs to
// climb back. 0 disables the rung.
#ifndef POWER_GUARD_SAFE_MV
  #define POWER_GUARD_SAFE_MV 0
#endif
// First wait only; boot-pwrcheck owns the backoff from the next boot.
#ifndef POWER_GUARD_SAFE_SECS
  #define POWER_GUARD_SAFE_SECS 60
#endif
// Lower rail for a stored threshold: below it a brownout fires before the rung does.
// 0 enforces no floor -- the real one is a board's measured boot floor.
#ifndef POWER_GUARD_SAFE_FLOOR_MV
  #define POWER_GUARD_SAFE_FLOOR_MV 0
#endif
// Return-to-service mark, and the upper rail for a stored threshold: at or above it a node
// hibernates and immediately requalifies. Lives here because both TUs need it; 0 disables.
#ifndef POWER_GUARD_RESUME_MV
  #define POWER_GUARD_RESUME_MV 0
#endif

// One below the return-to-service mark, the invariant above. Derived so a board cannot
// get it wrong by omission.
#ifndef POWER_GUARD_SAFE_MAX_MV
  #define POWER_GUARD_SAFE_MAX_MV \
    (POWER_GUARD_RESUME_MV > 0 ? POWER_GUARD_RESUME_MV - 1 : 0)
#endif

class PowerGuardPolicy {
  unsigned long _next_sample_at;
  uint8_t _agree;
  uint8_t _sleep_agree;
  bool _auto;
  bool _active;
  bool _lna_was_enabled;   // hardware state captured at engage
  uint16_t _last_mv;
  uint32_t _transitions;
  uint16_t _sleep_mv;
  uint8_t _safe_on;     // 0/1, or UNSET before anything is stored
  bool _loaded;

  static const uint8_t SAFE_UNSET = 0xFF;

  // NVS, not NodePrefs: no struct to drift, and an absent key stays distinguishable from a
  // stored 0 -- which lets `off` persist while a board default remains in place.
  void ensureLoaded() {
    if (_loaded) return;
    _loaded = true;
    Preferences p;
    if (p.begin("mobmesh", true)) {
      _sleep_mv = p.getUShort("safe_mv", POWER_GUARD_SAFE_MV);
      _safe_on = p.getUChar("safe_on", SAFE_UNSET);
      p.end();
    }
    if (_safe_on == SAFE_UNSET) _safe_on = (_sleep_mv > 0) ? 1 : 0;
  }

public:
  PowerGuardPolicy()
    : _next_sample_at(0), _agree(0), _sleep_agree(0),
      _auto(POWER_GUARD_AUTO_DEFAULT), _active(false),
      _lna_was_enabled(false), _last_mv(0), _transitions(0),
      _sleep_mv(POWER_GUARD_SAFE_MV), _safe_on(SAFE_UNSET), _loaded(false) { }

  // No sleep inhibit here -- upstream's own sleep already refuses while
  // inhibit_sleep is set, which covers OTA on every path.
  bool isActive() const { return _active; }
  bool isAuto() const { return _auto; }
  void setAuto(bool on) { _auto = on; if (!on && _active) setActive(false); }

  uint16_t sleepMilliVolts() { ensureLoaded(); return _sleep_mv; }
  bool safeEnabled() { ensureLoaded(); return _safe_on == 1; }

  void setSafeEnabled(bool on) {
    ensureLoaded();
    _safe_on = on ? 1 : 0;
    store("safe_on", (uint8_t)_safe_on);
  }

  // 0 disables the rung outright. Anything else must clear the boot floor and stay
  // under the duty-cycle rung, or a healthy node sleeps on a 60s cycle.
  bool setSleepMilliVolts(uint16_t mv) {
    if (mv != 0 && (mv < POWER_GUARD_SAFE_FLOOR_MV || mv > POWER_GUARD_SAFE_MAX_MV)) return false;
    ensureLoaded();
    _sleep_mv = mv;
    Preferences p;
    if (p.begin("mobmesh", false)) { p.putUShort("safe_mv", mv); p.end(); }
    return true;
  }
  uint16_t lastMilliVolts() const { return _last_mv; }
  uint32_t transitionCount() const { return _transitions; }

  void loop() {
    unsigned long now = millis();
    if ((long)(now - _next_sample_at) < 0) return;   // rollover-safe
    _next_sample_at = now + POWER_GUARD_SAMPLE_INTERVAL_MS;

    uint16_t mv = modBoardBattMilliVolts();
    _last_mv = mv;

    // No reading at all. Both rungs act on low voltage, so a 0 would otherwise
    // read as flat. A high reading needs no guard -- it can only release.
    if (mv == 0) {
      _agree = 0;
      _sleep_agree = 0;
      if (_active) setActive(false);
      return;
    }

    // Checked first: leaving service outranks any duty-cycle decision, and the
    // consecutive count is what keeps a transmit's sag from triggering it.
    ensureLoaded();
    if (_safe_on == 1 && _sleep_mv > 0 && mv < _sleep_mv) {
      if (++_sleep_agree >= POWER_GUARD_CONSECUTIVE_SAMPLES) {
        Serial.printf("batt %umV below %umV for %us -- leaving service\n",
                      mv, (unsigned)_sleep_mv,
                      (unsigned)(POWER_GUARD_SAMPLE_INTERVAL_MS / 1000
                                 * POWER_GUARD_CONSECUTIVE_SAMPLES));
        modBoardDeepSleep(POWER_GUARD_SAFE_SECS);   // does not return
      }
    } else {
      _sleep_agree = 0;
    }

    if (!_auto || POWER_GUARD_AUTO_ON_MV == 0) {
      _agree = 0;
      if (_active) setActive(false);
      return;
    }

    bool wants = _active ? (mv < POWER_GUARD_AUTO_OFF_MV) : (mv <= POWER_GUARD_AUTO_ON_MV);
    if (wants == _active) { _agree = 0; return; }

    if (++_agree < POWER_GUARD_CONSECUTIVE_SAMPLES) return;
    _agree = 0;
    setActive(wants);
  }

private:
  void store(const char* key, uint8_t v) {
    Preferences p;
    if (p.begin("mobmesh", false)) { p.putUChar(key, v); p.end(); }
  }

  void setActive(bool on) {
    _active = on;
    _transitions++;
#if POWER_GUARD_AUTO_DROP_FEM_LNA
    if (!modFemLnaAvailable()) return;
    if (on) {
      // Capture rather than assume, so release restores the operator's setting.
      _lna_was_enabled = modFemLnaGet();
      if (_lna_was_enabled) modFemLnaSet(false);
    } else if (_lna_was_enabled) {
      modFemLnaSet(true);
    }
#endif
  }
};
