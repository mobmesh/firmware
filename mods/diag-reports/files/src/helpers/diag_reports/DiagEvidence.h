#pragma once

#include <helpers/diag_reports/DiagBatt.h>
#include <helpers/diag_reports/DiagLinks.h>
#include <helpers/diag_reports/DiagLna.h>
#include <helpers/diag_reports/DiagNoise.h>
#include <helpers/diag_reports/DiagScan.h>
#include <helpers/diag_reports/DiagStore.h>
#include <helpers/diag_reports/DiagTrend.h>

// Everything the node knows, laid out by local hour of day and as facts, so every report and the engine reason from the same picture. Pure.

#define EV_HOT_C        60             // a CPU hour this hot counts toward heat
#define EV_COLD_C       0
#define EV_STRONG_DB    20.0f          // a neighbour this far above the decode limit can desensitise the receiver
#define EV_DAY_HOURS    24

enum EvSource : uint8_t {
  SOURCE_UNKNOWN,
  SOURCE_CARRIER,                      // a capture saw a steady carrier
  SOURCE_PACKETS,                      // a capture saw bursts: mesh traffic
  SOURCE_FOREIGN,                      // a capture saw LoRa that never decoded: another network
};

struct DiagEvidence {
  uint32_t now_hour;                   // UTC epoch hour; 0 = clock unset
  int8_t tz;
  uint8_t local_now;                   // local hour of day now

  // The channel, by local hour (bit n = local hour n).
  uint32_t have, loud, err, freeze, busy, guard_off, strong_hit;
  uint8_t hours, freezes;
  int8_t floor_dbm, max_rise;
  uint8_t load[24];                    // airtime % per local hour
  EvSource loud_source;                // what a capture in the loud hours saw
  EvSource decode_source;              // what a capture at a failed-decode spike saw

  // Power and heat, by local hour.
  uint32_t hot, cold, charging, falling;
  int8_t temp_now;
  bool has_temp;

  // Links.
  int strong;                          // index of a neighbour strong enough to desensitise, or -1
  int drops;                           // index of a neighbour lost only in the loud hours, or -1
  int one_way;                         // index, or -1
  int top, top_share, neighbours;      // busiest neighbour and its share of packets
  bool lean_weak;

  // The last scan, and when it ran.
  bool have_scan;
  StoreScan scan;
  uint8_t scan_hour;                   // local hour of day it ran in
  bool scan_in_loud, scan_in_freeze;   // it ran during the loud or freezing hours
  bool scan_explains_freeze;           // a blocker beside the channel and freezes logged
  bool scan_explains_loud;             // energy on the channel itself, found in the loud hours
  bool scan_empty_at_trigger;          // a scan that freezes triggered found nothing
  bool foreign_lora;                   // a scan or capture in the last day found another LoRa network on the channel
  bool noise_follows_charging;         // the loud hours sit inside the hours the battery was charging: the node's own power gear

  // The LNA: the gate, the live state, and what its study found.
  bool lna_available, lna_on;
  LnaFacts lna;
  uint32_t lna_off_better;             // local hours the study found off better
  int8_t live_floor_shift;             // last diag probe lna check, on minus off; 0 = none
  bool lna_freezes_only_on;            // freezes logged only in hours with the LNA on

  // The last direct ping: SNR at the neighbour (out) and back at us.
  bool ping_ok;
  uint16_t ping_id;
  float ping_out_db, ping_back_db;

  // The history beyond today.
  TrendUsual usual;
  TrendNb fading;
  float fading_margin;                 // the fading neighbour's margin now
  TrendShares shares;
  TrendSchedule schedule;

  // Restarts.
  int heat_reboot;                     // index into the store's reboot log of an unplanned restart after heat, or -1
};

struct EvidenceSources {
  const DiagStore* store;
  const LinkTable* links;
  const NoiseView* noise;
  uint32_t now_hour, now_ms;
  int8_t tz;
  uint8_t sf;
  float temp_now;
  bool lna_available, lna_on;
  const LnaFacts* lna;                 // nullptr when no study has finished
  bool ping_ok;                        // a direct ping answered in the last day
  uint16_t ping_id;
  float ping_out_db, ping_back_db;
};

void evidenceBuild(DiagEvidence& e, const EvidenceSources& in);
// Local hours moved to and from UTC hours of day.
uint32_t evLocalMask(uint32_t utc_mask, int8_t tz);
uint32_t evUtcMask(uint32_t local_mask, int8_t tz);
int evBits(uint32_t mask);
// At least two thirds of `symptom`'s hours fall inside `cause`'s.
bool evInside(uint32_t symptom, uint32_t cause);
// "8am-5pm", "8am", "all day"; empty when the mask is empty.
void evHoursText(uint32_t mask, char* out, size_t len);
// The first hour of the longest run, or -1.
int evRunStart(uint32_t mask);
