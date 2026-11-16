#include <helpers/diag_reports/DiagEvidence.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

#define EV_BUSY_PCT        25
#define EV_FALL_MV         4           // a battery hour falling this much counts as draining
#define EV_RISE_MV         8
#define EV_CAPTURE_AGE_S   86400UL
#define EV_HEAT_REBOOT_S   (30UL * 86400UL)

static uint32_t rotate(uint32_t mask, int hours) {
  int shift = ((hours % 24) + 24) % 24;
  mask &= 0xFFFFFF;
  return ((mask << shift) | (mask >> (24 - shift))) & 0xFFFFFF;
}

uint32_t evLocalMask(uint32_t utc_mask, int8_t tz) {
  return rotate(utc_mask, tz);
}

uint32_t evUtcMask(uint32_t local_mask, int8_t tz) {
  return rotate(local_mask, -tz);
}

int evBits(uint32_t v) {
  int n = 0;
  for (v &= 0xFFFFFF; v; v &= v - 1) n++;
  return n;
}

bool evInside(uint32_t symptom, uint32_t cause) {
  int n = evBits(symptom);
  return n > 0 && evBits(symptom & cause) * 3 >= n * 2;
}

static bool longest(uint32_t mask, uint8_t* start, uint8_t* len) {
  bool flags[24];
  int runs = 0;
  for (int i = 0; i < 24; i++) flags[i] = mask >> i & 1;
  return diagLongestRun(flags, start, len, &runs);
}

void evHoursText(uint32_t mask, char* out, size_t len) {
  uint8_t start = 0, run = 0;
  if (!longest(mask, &start, &run)) {
    out[0] = 0;
    return;
  }
  if (run == 24) snprintf(out, len, "all day");
  else if (run == 1) diagHourText(start, false, out, len);
  else diagSpanText(start, run, out, len);
}

int evRunStart(uint32_t mask) {
  uint8_t start = 0, run = 0;
  return longest(mask, &start, &run) ? start : -1;
}

static EvSource sourceOf(const StoreCapture& c, uint32_t now_epoch) {
  if (c.at == 0 || now_epoch < c.at || now_epoch - c.at > EV_CAPTURE_AGE_S) return SOURCE_UNKNOWN;
  return c.kind == 3 ? SOURCE_FOREIGN : c.kind ? SOURCE_CARRIER : SOURCE_PACKETS;
}

void evidenceBuild(DiagEvidence& e, const EvidenceSources& in) {
  memset(&e, 0, sizeof(e));
  const DiagStore& s = *in.store;
  const NoiseView& n = *in.noise;
  e.now_hour = in.now_hour;
  e.tz = in.tz;
  e.local_now = in.now_hour ? storeLocalHour(in.now_hour, in.tz) : 0;
  uint32_t now_epoch = in.now_hour * 3600;

  e.have = n.have_mask;
  e.loud = n.loud_mask;
  e.err = n.err_mask;
  e.freeze = n.freeze_mask;
  e.hours = n.hours;
  e.floor_dbm = n.floor_dbm;
  e.max_rise = n.max_rise;

  // Hour by hour from the store: load, freezes in progress, guard, heat, cold, charging.
  for (int i = 0; i < STORE_HOURS; i++) {
    const StoreHour& h = s.h[i];
    if (h.hour == 0 || h.hour > in.now_hour || in.now_hour - h.hour > EV_DAY_HOURS) continue;
    int local = storeLocalHour(h.hour, in.tz);
    uint32_t bit = 1UL << local;
    e.load[local] = h.load_pct;
    if (h.load_pct >= EV_BUSY_PCT) e.busy |= bit;
    if (h.freezes) {
      e.freeze |= bit;
      e.freezes = (uint8_t)(e.freezes + h.freezes > 255 ? 255 : e.freezes + h.freezes);
    }
    if (h.guard_off) e.guard_off |= bit;
    if (h.strong) e.strong_hit |= bit;
    if (h.temp_max != STORE_NO_TEMP && h.temp_max >= EV_HOT_C) e.hot |= bit;
    if (h.temp_max != STORE_NO_TEMP && h.temp_max < EV_COLD_C) e.cold |= bit;
    if (h.flags & HOUR_BATT) {
      if (h.batt_last >= h.batt_first + EV_RISE_MV) e.charging |= bit;
      if (h.batt_first >= h.batt_last + EV_FALL_MV) e.falling |= bit;
    }
  }
  e.has_temp = !isnan(in.temp_now);
  e.temp_now = e.has_temp ? (int8_t)lroundf(in.temp_now) : 0;

  // Captures say what the loud hours and the failed-decode spikes actually were.
  e.loud_source = sourceOf(s.noise_capture, now_epoch);
  e.decode_source = sourceOf(s.decode_capture, now_epoch);

  // Links, with the loud hours in hand.
  const LinkTable& t = *in.links;
  e.strong = e.drops = e.one_way = -1;
  LinkVerdict verdict = linkVerdict(t, in.sf, in.now_ms);
  if (verdict == LINK_V_ONE_WAY) e.one_way = linkOneWay(t, in.sf, in.now_ms);
  if (e.loud && verdict != LINK_V_NOT_ENOUGH) e.drops = linkDropsIn(t, evUtcMask(e.loud, in.tz), evUtcMask(e.have, in.tz), in.now_ms);
  for (int i = 0; i < t.count; i++) {
    if (t.nb[i].packets < LINKS_MIN_PKTS || in.now_ms - t.nb[i].last_ms >= LINKS_STALE_MS) continue;
    if (linkMargin(t, i, in.sf) >= EV_STRONG_DB && (e.strong < 0 || linkMargin(t, i, in.sf) > linkMargin(t, e.strong, in.sf))) e.strong = i;
  }
  e.top_share = linkTopShare(t, in.now_ms, &e.top, &e.neighbours);
  e.lean_weak = e.top_share >= 70 && e.top >= 0 && linkMargin(t, e.top, in.sf) < LINKS_SOLID_DB;

  // The last scan against the hours it ran in.
  if (s.scan.at) {
    e.have_scan = true;
    e.scan = s.scan;
    e.scan_hour = storeLocalHour(s.scan.at / 3600, in.tz);
    uint32_t bit = 1UL << e.scan_hour;
    e.scan_in_loud = e.loud & bit;
    e.scan_in_freeze = e.freeze & bit;
    e.scan_explains_freeze = e.freezes && s.scan.blocker;
    e.scan_explains_loud = s.scan.kind == SCAN_K_MESH_STEADY && e.scan_in_loud;
  }
  e.scan_empty_at_trigger = s.scan_capture.at && s.scan_capture.kind == 0 && e.freezes;
  // Weak packets of this mesh make orphan preambles too: another network needs two hours of them with none of this mesh's decoded.
  int foreign_hours = 0;
  for (int i = 0; i < STORE_HOURS; i++) {
    const StoreHour& h = s.h[i];
    if (h.hour && h.hour <= in.now_hour && in.now_hour - h.hour < 24 && (h.lora & 1) && !(h.lora & 2)) foreign_hours++;
  }
  e.foreign_lora = foreign_hours >= 2;
  if (!e.foreign_lora && e.loud_source == SOURCE_FOREIGN) e.loud_source = SOURCE_UNKNOWN;
  if (!e.foreign_lora && e.decode_source == SOURCE_FOREIGN) e.decode_source = SOURCE_UNKNOWN;
  e.noise_follows_charging = e.loud && evBits(e.charging) >= 2 && evInside(e.loud, e.charging);

  // The LNA, gated: nothing here when the board cannot switch it.
  e.lna_available = in.lna_available;
  if (in.lna_available) {
    e.lna_on = in.lna_on;
    if (in.lna) {
      e.lna = *in.lna;
      e.lna_off_better = evLocalMask(in.lna->off_better_utc, in.tz);
    }
    if (s.lna_check.at && now_epoch - s.lna_check.at < EV_CAPTURE_AGE_S) e.live_floor_shift = (int8_t)(s.lna_check.floor_on - s.lna_check.floor_off);
    // Only meaningful when the LNA spent real time off: hours with a floor in each state.
    uint8_t on = 0, off = 0, off_hours = 0;
    for (int i = 0; i < STORE_HOURS; i++) {
      const StoreHour& h = s.h[i];
      if (h.hour == 0 || h.hour > in.now_hour || in.now_hour - h.hour > EV_DAY_HOURS) continue;
      if (h.lna == STORE_LNA_OFF && h.floor_dbm != STORE_NO_FLOOR) off_hours++;
      if (!h.freezes) continue;
      if (h.lna == STORE_LNA_ON) on += h.freezes;
      if (h.lna == STORE_LNA_OFF) off += h.freezes;
    }
    e.lna_freezes_only_on = (on >= 2 && off == 0 && off_hours >= 4) || (e.lna.freezes[1] >= 2 && e.lna.freezes[0] == 0);
  }

  // The history beyond today: usual levels, slow change, measured shares, schedules.
  if (in.now_hour) {
    uint32_t today = storeLocalDay(in.now_hour, in.tz);
    e.usual = trendUsual(s, in.now_hour, in.tz);
    e.fading = trendWorstNb(s, today);
    if (e.fading.found) {
      for (int i = 0; i < t.count; i++) {
        if (t.nb[i].id == e.fading.id && t.nb[i].id_len == e.fading.id_len) e.fading_margin = linkMargin(t, i, in.sf);
      }
    }
    e.shares = trendShares(s, in.now_hour, in.tz, e.loud, e.freeze, e.busy);
    e.schedule = trendSchedule(s, today);
  }
  e.ping_ok = in.ping_ok;
  e.ping_id = in.ping_id;
  e.ping_out_db = in.ping_out_db;
  e.ping_back_db = in.ping_back_db;

  // An unplanned restart in the last 30 days after an hour of heat.
  e.heat_reboot = -1;
  for (int i = 0; i < STORE_REBOOTS; i++) {
    const StoreReboot& r = s.reboot[i];
    if (r.at == 0 || r.planned || r.temp_before == STORE_NO_TEMP || r.temp_before < EV_HOT_C) continue;
    if (now_epoch && now_epoch - r.at > EV_HEAT_REBOOT_S) continue;
    e.heat_reboot = i;
    break;
  }
}
