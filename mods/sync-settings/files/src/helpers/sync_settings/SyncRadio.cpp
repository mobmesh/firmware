#include "SyncRadio.h"

#include <math.h>

namespace mobmesh {
namespace sync {

static bool crValid(uint8_t cr, bool allow_keep) {
  return (allow_keep && cr == 0) || (cr >= 5 && cr <= 8);
}

bool radioValuesValid(const RadioValues& value, bool allow_keep_cr) {
  return value.freq_hz >= 150000000u && value.freq_hz <= 2500000000u &&
         value.bw_hz >= 7000u && value.bw_hz <= 500000u &&
         value.sf >= 5 && value.sf <= 12 && crValid(value.cr, allow_keep_cr);
}

bool radioValuesEqual(const RadioValues& first, const RadioValues& second) {
  return first.freq_hz == second.freq_hz && first.bw_hz == second.bw_hz &&
         first.sf == second.sf && first.cr == second.cr;
}

static bool scaled(float value, double factor, uint32_t& out) {
  if (!isfinite(value) || value < 0) return false;
  double result = floor((double)value * factor + 0.5);
  if (result > 4294967295.0) return false;
  out = (uint32_t)result;
  return true;
}

bool radioFromFloats(float freq_mhz, float bw_khz, uint8_t sf, uint8_t cr,
                     bool allow_keep_cr, RadioValues& out) {
  RadioValues value = {};
  if (!scaled(freq_mhz, 1000000.0, value.freq_hz) ||
      !scaled(bw_khz, 1000.0, value.bw_hz)) return false;
  value.sf = sf;
  value.cr = cr;
  if (!radioValuesValid(value, allow_keep_cr)) return false;
  out = value;
  return true;
}

bool radioToFloats(const RadioValues& value, float& freq_mhz, float& bw_khz) {
  if (!radioValuesValid(value, true)) return false;
  float freq = (float)((double)value.freq_hz / 1000000.0);
  float bw = (float)((double)value.bw_hz / 1000.0);
  RadioValues round_trip;
  if (!radioFromFloats(freq, bw, value.sf, value.cr, true, round_trip) ||
      round_trip.freq_hz != value.freq_hz || round_trip.bw_hz != value.bw_hz) {
    return false;
  }
  freq_mhz = freq;
  bw_khz = bw;
  return true;
}

bool radioScheduleValid(const RadioSchedule& value) {
  if (value.campaign_interval <= 5 || value.campaign_duration == 0 ||
      value.campaign_duration >= 1441 ||
      value.campaign_interval >= value.campaign_duration ||
      value.confirm_interval == 0 ||
      value.confirm_interval >= value.confirm_window ||
      value.confirm_window <= 14 ||
      value.confirm_window > RADIO_CONFIRM_WINDOW_MAX) return false;
  bool tests_off = value.test_interval == 0 && value.test_window == 0;
  if (tests_off) return true;
  if (value.test_interval <= 30 || value.test_window == 0 ||
      value.test_window >= 25 || value.test_window >= value.test_interval) return false;
  return (uint32_t)value.test_interval + value.test_window + 1u <=
         value.campaign_duration;
}

bool radioPayloadValid(const RadioPayload& value, uint32_t build_epoch) {
  if (value.migration_id == 0 || !radioValuesValid(value.target, true) ||
      !radioScheduleValid(value.schedule) || value.start == 0 ||
      value.cutover <= value.start || value.stamp < value.start ||
      value.stamp >= value.cutover || value.stamp < build_epoch) return false;
  return (uint64_t)value.cutover - value.start < 1441u * 60u;
}

uint8_t radioResolvedCr(const RadioValues& target, const RadioValues& current) {
  return target.cr == 0 ? current.cr : target.cr;
}

RadioClass radioClassify(const RadioValues& current, const RadioValues& target) {
  uint8_t cr = radioResolvedCr(target, current);
  bool same_phy = current.freq_hz == target.freq_hz &&
                  current.bw_hz == target.bw_hz && current.sf == target.sf;
  if (same_phy && current.cr == cr) return RADIO_IDENTICAL;
  return same_phy ? RADIO_CR_ONLY : RADIO_RETUNE;
}

bool radioLocalDeadline(uint32_t stamp, uint32_t target, uint32_t now_ms,
                        uint32_t& deadline_ms) {
  if (target < stamp) return false;
  uint64_t delta = (uint64_t)(target - stamp) * 1000u;
  if (delta > 0x7fffffffu) return false;
  deadline_ms = now_ms + (uint32_t)delta;
  return true;
}

bool radioSamePlan(const RadioPayload& first, const RadioPayload& next) {
  return first.migration_id == next.migration_id &&
         first.target.freq_hz == next.target.freq_hz &&
         first.target.bw_hz == next.target.bw_hz &&
         first.target.sf == next.target.sf && first.target.cr == next.target.cr &&
         first.start == next.start && first.cutover == next.cutover &&
         first.schedule.campaign_interval == next.schedule.campaign_interval &&
         first.schedule.test_interval == next.schedule.test_interval &&
         first.schedule.test_window == next.schedule.test_window &&
         first.schedule.campaign_duration == next.schedule.campaign_duration &&
         first.schedule.confirm_interval == next.schedule.confirm_interval &&
         first.schedule.confirm_window == next.schedule.confirm_window;
}

bool radioNextTestWindow(const RadioPayload& value, uint32_t now_ms,
                         uint32_t& start_ms, uint32_t& end_ms) {
  if (!radioPayloadValid(value, 0) || value.schedule.test_interval == 0) return false;
  uint32_t interval = (uint32_t)value.schedule.test_interval * 60u;
  uint32_t window = (uint32_t)value.schedule.test_window * 60u;
  uint32_t first = value.start + interval;
  uint32_t test = first;
  if (value.stamp > first) {
    uint32_t elapsed = value.stamp - value.start;
    uint32_t count = (elapsed + interval - 1u) / interval;
    if (count == 0) count = 1;
    test = value.start + count * interval;
  }
  if ((uint64_t)test + window + 60u > value.cutover ||
      !radioLocalDeadline(value.stamp, test, now_ms, start_ms)) return false;
  end_ms = start_ms + window * 1000u;
  return true;
}

}  // namespace sync
}  // namespace mobmesh
