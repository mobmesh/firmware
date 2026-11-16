#include <helpers/diag_reports/DiagSched.h>

#include <stdlib.h>

uint8_t schedCap(uint8_t trigger) {
  return trigger == TRIG_FREEZES ? SCHED_SCAN_CAP : SCHED_BURST_CAP;
}

uint8_t schedUsed(const DiagStore& s, uint8_t trigger, uint32_t local_day) {
  return trigger < TRIG_COUNT && s.cap_day == local_day ? s.cap_used[trigger] : 0;
}

bool schedTestBatteryLow(uint16_t batt_mv) { return batt_mv != 0 && batt_mv < SCHED_LOW_MV; }

bool schedAllow(const DiagStore& s, uint8_t trigger, const SchedConditions& c, const char** why) {
  const char* reason = nullptr;
  if (trigger >= TRIG_COUNT) reason = "unknown trigger";
  else if (c.auto_off) reason = "auto off";
  else if (c.paused) reason = "auto paused";
  else if (c.epoch == 0) reason = "clock not set";
  else if (c.packet_arriving) reason = "packet arriving";
  else if (c.load_pct > SCHED_BUSY_PCT) reason = "channel busy";
  else if (schedUsed(s, trigger, c.local_day) >= schedCap(trigger)) reason = "daily cap";
  else if (s.cap_last[trigger] && c.epoch - s.cap_last[trigger] < SCHED_SPACING_S) reason = "too soon";
  else if (trigger == TRIG_FREEZES && c.scan_age_s < SCHED_SPACING_S) reason = "recent scan";
  if (why) *why = reason;
  return reason == nullptr;
}

void schedUse(DiagStore& s, uint8_t trigger, const SchedConditions& c) {
  if (trigger >= TRIG_COUNT) return;
  if (s.cap_day != c.local_day) {
    s.cap_day = c.local_day;
    for (int i = 0; i < TRIG_COUNT; i++) s.cap_used[i] = 0;
  }
  if (s.cap_used[trigger] < 255) s.cap_used[trigger]++;
  s.cap_last[trigger] = c.epoch;
}

bool schedDecodeSpike(uint32_t errors_then, uint32_t errors_now) {
  return errors_now >= errors_then && errors_now - errors_then >= 3;
}

int schedPauseMinutes(const char* text) {
  while (*text == ' ') text++;
  if (*text == 0) return 5;
  char* end;
  long v = strtol(text, &end, 10);
  while (*end == ' ') end++;
  if (*end != 0 || v < 1 || v > SCHED_PAUSE_MAX) return -1;
  return (int)v;
}
