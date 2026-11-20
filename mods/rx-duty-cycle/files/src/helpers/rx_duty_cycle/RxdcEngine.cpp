#include <helpers/rx_duty_cycle/RxdcEngine.h>

void RxdcEngine::setMode(bool auto_on) {
  _auto = auto_on;
  _fails = 0;
}

bool RxdcEngine::refreshGeometry(RxdcRadio& r) {
  if (_geo_dirty) {
    uint8_t sf = r.sf();
    _geo_ok = rxdcGeometry(sf, r.bw(), sf <= 8 ? 32 : 16, r.transitionUs(), &_geo);
    _geo_dirty = false;
  }
  return _geo_ok;
}

int16_t RxdcEngine::start(RxdcRadio& r, uint32_t now_ms) {
  stop(r);
  _rearm = false;
  if (!wanted() || _calib || _paused || _fails >= RXDC_MAX_ARM_FAILS || !refreshGeometry(r)) return RXDC_PASS;
  int16_t err = r.arm(_geo.rx_us, _geo.sleep_us);
  if (err == 0) {
    _armed = true;
    _arms++;
    _fails = 0;
    return 0;
  }
  // A failed arm never leaves the radio deaf: back to continuous receive this time.
  _err = err;
  _fails++;
  _fail_total++;
  r.stop();
  (void)now_ms;
  return r.continuous();
}

void RxdcEngine::stop(RxdcRadio& r) {
  if (!_armed) return;
  r.stop();
  _armed = false;
}

bool RxdcEngine::asleep(RxdcRadio& r) {
  return _armed && r.busy();
}

bool RxdcEngine::sampleOk(uint32_t now_ms) const {
  if (_armed) return false;
  if (_calib) return now_ms - _calib_start >= RXDC_CALIB_SETTLE_MS;
  if (_paused) return now_ms - _pause_start >= RXDC_CALIB_SETTLE_MS;
  return true;
}

// A false preamble needs no help here: the radio's post-preamble timer (the geometry's reload) returns it to cycling.
int16_t RxdcEngine::loop(RxdcRadio& r, uint32_t now_ms) {
  int16_t flags = _restart ? RXDC_RESTART : 0;
  _restart = false;
  // A reader on another task asked for a window: radio switching stays on this task.
  if (_pause_req && !_paused) {
    if (_armed) openPause(r, now_ms);
    else _pause_req = false;
  }
  // Power saving or the mode changed: the next receive start picks the new state.
  if (wanted() != _active) {
    _active = wanted();
    _calib = _paused = false;
    _calib_due = true;
    flags |= RXDC_RESTART;
  }
  if (_paused && (_pause_done || now_ms - _pause_start >= RXDC_PAUSE_LIMIT_MS)) {
    _paused = _pause_done = false;
    flags |= RXDC_RESTART;
  }
  if (_calib && now_ms - _calib_start >= RXDC_CALIB_LIMIT_MS) {
    _calib = false;
    _calib_last = now_ms;
    flags |= RXDC_RESTART;
  }
  if (_active && !_calib && !_paused && _armed && (_calib_due || now_ms - _calib_last >= RXDC_CALIB_EVERY_MS)) {
    _calib = true;
    _calib_due = false;
    _calib_start = now_ms;
    flags |= RXDC_RESTART | RXDC_FRESH_FLOOR;
  }
  // Held awake until the restart re-arms, or the CPU could sleep with the radio left in continuous receive.
  if (flags & RXDC_RESTART) {
    _rearm = true;
    _rearm_since = now_ms;
  } else if (_rearm && now_ms - _rearm_since >= RXDC_REARM_LIMIT_MS) {
    _rearm = false;
  }
  return flags;
}

void RxdcEngine::params(RxdcRadio& r) {
  stop(r);
  _geo_dirty = true;
  _fails = 0;
  _calib_due = true;
  _restart = true;
}

void RxdcEngine::openPause(RxdcRadio& r, uint32_t now_ms) {
  stop(r);
  r.continuous();
  _paused = true;
  _pause_start = now_ms;
  _pause_req = false;
}

int16_t RxdcEngine::pauseState(uint32_t now_ms) {
  if (_paused) return now_ms - _pause_start >= RXDC_CALIB_SETTLE_MS ? 0 : 1;
  if (!_armed) return 0;
  _pause_req = true;
  return 1;
}

void RxdcEngine::pauseNow(RxdcRadio& r, uint32_t now_ms) {
  if (_armed && !_paused) openPause(r, now_ms);
}

void RxdcEngine::resume() {
  if (_paused) _pause_done = true;
  else _pause_req = false;
}

void RxdcEngine::resumeNow(RxdcRadio& r, uint32_t now_ms) {
  _pause_req = false;
  if (!_paused) return;
  _paused = _pause_done = false;
  start(r, now_ms);
}

void RxdcEngine::floorDone(uint32_t now_ms) {
  if (!_calib) return;
  _calib = false;
  _calib_last = now_ms;
  _restart = true;
}

void RxdcEngine::agcReset(RxdcRadio& r) {
  stop(r);
  _calib_due = true;   // the wrapper zeroes its floor on every reset
}

RxdcState RxdcEngine::state() const {
  if (!wanted()) return RXDC_S_OFF;
  if (_calib) return RXDC_S_CALIBRATING;
  if (_paused) return RXDC_S_PAUSED;
  if (!_geo_dirty && !_geo_ok) return RXDC_S_NO_GEOMETRY;
  if (_fails >= RXDC_MAX_ARM_FAILS) return RXDC_S_FALLBACK;
  return RXDC_S_ARMED;
}
