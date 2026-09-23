#pragma once

#include <stddef.h>
#include <stdint.h>

#include "SyncSelect.h"

#if SYNC_SETTINGS_WITH_RADIO && !defined(MOBMESH_BUILD_EPOCH)
#error "RADIO requires MOBMESH_BUILD_EPOCH"
#endif

namespace mobmesh {
namespace sync {

static const uint8_t RADIO_SCHEMA = 0x01;
static const size_t RADIO_PAYLOAD_LEN = 44;
static const uint16_t RADIO_CONFIRM_WINDOW_MAX = 35791;

struct RadioValues {
  uint32_t freq_hz;
  uint32_t bw_hz;
  uint8_t sf;
  uint8_t cr;
};

struct RadioSchedule {
  uint16_t campaign_interval;
  uint16_t test_interval;
  uint16_t test_window;
  uint16_t campaign_duration;
  uint16_t confirm_interval;
  uint16_t confirm_window;
};

struct RadioPayload {
  uint64_t migration_id;
  RadioValues target;
  uint32_t start;
  uint32_t cutover;
  uint32_t stamp;
  RadioSchedule schedule;
};

enum RadioClass : uint8_t {
  RADIO_IDENTICAL,
  RADIO_CR_ONLY,
  RADIO_RETUNE,
};

bool radioValuesValid(const RadioValues& value, bool allow_keep_cr);
bool radioFromFloats(float freq_mhz, float bw_khz, uint8_t sf, uint8_t cr,
                     bool allow_keep_cr, RadioValues& out);
bool radioToFloats(const RadioValues& value, float& freq_mhz, float& bw_khz);
bool radioScheduleValid(const RadioSchedule& value);
bool radioPayloadValid(const RadioPayload& value, uint32_t build_epoch);
uint8_t radioResolvedCr(const RadioValues& target, const RadioValues& current);
RadioClass radioClassify(const RadioValues& current, const RadioValues& target);
bool radioLocalDeadline(uint32_t stamp, uint32_t target, uint32_t now_ms,
                        uint32_t& deadline_ms);

}  // namespace sync
}  // namespace mobmesh
