#include <helpers/diag_reports/DiagEngine.h>
#include <helpers/diag_reports/DiagLna.h>
#include <helpers/diag_reports/DiagTrend.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

#define ERRORS_MIN       50
#define ERRORS_CAUTION   20            // tenths of a percent
#define ERRORS_PROBLEM   100
#define LOAD_MIN_S       600
#define LOAD_CAUTION     25
#define LOAD_PROBLEM     50
#define OVERLOAD_SHIFT   20            // dB the floor rises with the LNA on when it is overloaded
#define DRAIN_MIN_DAYS   3
#define DRAIN_WORSE_MV   30
#define SURE_DAYS        3

static const char* const ICON[AREA_COUNT] = {"\U0001F517", "\U0001F4F6", "\U0001F4E1", "\U0001F50A", "\U0001F6E1", "\U0001F4C9", "🔋", "\U0001F4CA", "\U0001F9ED", "\U0001F550"};
static const char* const NAME[AREA_COUNT] = {"Links", "LNA", "Nearby", "Noise", "Guard", "Decodes", "Battery", "Load", "Paths", "Clock"};

// Causes before symptoms: among equally severe causes the lower rank gives the fix.
static const uint8_t RANK[CAUSE_COUNT] = {99, 0, 1, 1, 1, 2, 3, 3, 3, 3, 3, 4, 4, 5, 6, 7, 8, 8, 9, 10, 6, 6};

const char* engineCauseName(uint8_t cause) {
  static const char* const names[CAUSE_COUNT] = {"none", "battery low", "guard off", "noise", "blocker", "antenna", "LNA fault", "LNA overload",
                                                 "cold", "load drain", "panel", "freezes", "far side", "weak link", "congestion", "collisions",
                                                 "LNA setting", "heat", "single path", "clock", "strong neighbour", "foreign LoRa"};
  return cause < CAUSE_COUNT ? names[cause] : "?";
}

const char* engineProbeFor(uint8_t cause) {
  switch (cause) {
    case CAUSE_NOISE: return "scan";
    case CAUSE_COLLISIONS: case CAUSE_CONGESTION: case CAUSE_STRONG_NEIGHBOUR: case CAUSE_FOREIGN_LORA: return "noise";
    case CAUSE_BLOCKER: case CAUSE_FREEZES: case CAUSE_GUARD_OFF: return "guard";
    case CAUSE_LNA_OVERLOAD: case CAUSE_LNA_FAULT: case CAUSE_LNA_SETTING: return "lna";
    case CAUSE_BATT_LOW: case CAUSE_PANEL: case CAUSE_COLD: case CAUSE_LOAD_DRAIN: return "batt";
    case CAUSE_FAR_SIDE: case CAUSE_WEAK_LINK: case CAUSE_ANTENNA: return "ping";
    default: return "";
  }
}

uint32_t engineRotate(uint32_t mask, int hours) {
  int shift = ((hours % 24) + 24) % 24;
  mask &= 0xFFFFFF;
  return ((mask << shift) | (mask >> (24 - shift))) & 0xFFFFFF;
}

static int bits(uint32_t v) {
  int n = 0;
  for (; v; v &= v - 1) n++;
  return n;
}

// True when at least two thirds of the symptom's hours fall inside the cause's hours.
static bool inside(uint32_t symptom, uint32_t cause) {
  int n = bits(symptom);
  return n > 0 && bits(symptom & cause) * 3 >= n * 2;
}

bool engineLoadDrain(const DiagStore& s) {
  // Each day's load against the change to the next morning; busy and quiet days split at the mean load.
  int32_t load_sum = 0;
  int n = 0;
  int16_t delta[STORE_DAYS];
  uint8_t load[STORE_DAYS];
  uint32_t end = s.today.day != 0 ? s.today.day : s.last_day;
  if (end < STORE_DAYS) return false;
  for (uint32_t day = end - (STORE_DAYS - 1); day < end; day++) {
    StoreDay a = storeDay(s, day), b = storeDay(s, day + 1);
    if (a.morning == STORE_NO_MV || b.morning == STORE_NO_MV || a.load_pct == STORE_NO_LOAD) continue;
    delta[n] = (int16_t)((int)storeUnpackMv(b.morning) - (int)storeUnpackMv(a.morning));
    load[n] = a.load_pct;
    load_sum += a.load_pct;
    n++;
  }
  if (n < DRAIN_MIN_DAYS * 2) return false;
  int mean = (int)(load_sum / n), busy_n = 0, quiet_n = 0;
  int32_t busy = 0, quiet = 0;
  for (int i = 0; i < n; i++) {
    if (load[i] > mean + 2) {
      busy += delta[i];
      busy_n++;
    } else if (load[i] <= mean) {
      quiet += delta[i];
      quiet_n++;
    }
  }
  if (busy_n < DRAIN_MIN_DAYS || quiet_n < DRAIN_MIN_DAYS) return false;
  return busy / busy_n <= quiet / quiet_n - DRAIN_WORSE_MV;
}

struct Evidence {
  uint32_t loud, err, freeze, busy;    // local-hour masks
  uint8_t freezes;                     // in the last 24 h
  uint8_t freezes_lna[2];              // of those, hours tagged LNA off and on
  uint8_t off_count;                   // guard found cleared, last 24 h
  int load_pct;                        // -1 while too young
  int err_tenths;                      // -1 with too few packets
  LinkVerdict link;
  int one_way, drops, top, top_share, neighbours;
  bool one_way_retry;                  // named by unconfirmed resends rather than missing echoes
  bool lean_weak;                      // most paths through a neighbour under the solid margin
  bool load_drain;
  bool cold;
};

static void gather(const EngineInput& in, Evidence& e) {
  memset(&e, 0, sizeof(e));
  e.loud = in.noise->loud_mask;
  e.err = in.noise->err_mask;
  e.freeze = in.noise->freeze_mask;
  if (in.ev) e.freeze |= in.ev->freeze;
  for (int i = 0; i < STORE_HOURS; i++) {
    const StoreHour& h = in.store->h[i];
    if (h.hour == 0 || h.hour > in.now_hour || in.now_hour - h.hour > 24) continue;
    int local = storeLocalHour(h.hour, in.tz);
    if (h.load_pct >= LOAD_CAUTION) e.busy |= 1UL << local;
    // The hour in progress counts for freezes: a frozen receiver matters now, not after the hour closes.
    if (h.freezes) {
      e.freezes = (uint8_t)(e.freezes + h.freezes > 255 ? 255 : e.freezes + h.freezes);
      e.freeze |= 1UL << local;
      if (h.lna < 2) e.freezes_lna[h.lna] = (uint8_t)(e.freezes_lna[h.lna] + h.freezes);
      else e.freezes_lna[in.lna_on ? 1 : 0] = (uint8_t)(e.freezes_lna[in.lna_on ? 1 : 0] + h.freezes);
    }
    e.off_count = (uint8_t)(e.off_count + h.guard_off);
  }
  e.load_pct = in.uptime_s < LOAD_MIN_S ? -1 : (int)((uint64_t)in.air_s * 100 / in.uptime_s);
  uint32_t total = in.recv + in.errors;
  e.err_tenths = total < ERRORS_MIN ? -1 : (int)((uint64_t)in.errors * 1000 / total);
  e.link = linkVerdict(*in.links, in.sf, in.now_ms);
  e.one_way = e.link == LINK_V_ONE_WAY ? linkOneWay(*in.links, in.sf, in.now_ms) : -1;
  if (e.one_way >= 0 && in.cleared_one_way && in.links->nb[e.one_way].id == in.cleared_one_way) e.one_way = -1;
  // Resends that the next hop never confirms say the same: it does not hear this node.
  e.one_way_retry = false;
  int retry = in.retry_one_way - 1;
  if (e.one_way < 0 && retry >= 0 && retry < in.links->count && !(in.cleared_one_way && in.links->nb[retry].id == in.cleared_one_way)) {
    e.one_way = retry;
    e.one_way_retry = true;
  }
  e.drops = e.loud && e.link != LINK_V_NOT_ENOUGH ? linkDropsIn(*in.links, engineRotate(e.loud, -in.tz), engineRotate(in.noise->have_mask, -in.tz), in.now_ms) : -1;
  e.top_share = linkTopShare(*in.links, in.now_ms, &e.top, &e.neighbours);
  e.lean_weak = e.top_share >= 70 && e.top >= 0 && linkMargin(*in.links, e.top, in.sf) < LINKS_SOLID_DB;
  e.load_drain = engineLoadDrain(*in.store);
  int8_t tmin = in.store->today.tmin;
  e.cold = (in.has_temp && in.temp_now < TEMP_COLD_C) || (tmin != STORE_NO_TEMP && tmin < TEMP_COLD_C);
}

static bool battFalling(const BattView& b) {
  return b.kind == BATT_K_DRAINING || b.kind == BATT_K_NET_LOSS || b.kind == BATT_K_NOT_RECOVERING;
}

// How strongly the evidence supports each cause, 0-100; 0 = not in play.
static void score(const EngineInput& in, const Evidence& e, uint8_t s[CAUSE_COUNT]) {
  memset(s, 0, CAUSE_COUNT);
  const NoiseView& n = *in.noise;
  const BattView& b = *in.batt;
  bool agc_off = in.agc_interval == 0;
  const DiagEvidence* ev = in.ev;
  int8_t shift = ev && ev->live_floor_shift ? ev->live_floor_shift : in.study_floor_shift;
  bool overload_floor = in.lna_available && in.lna_on && shift >= OVERLOAD_SHIFT && e.err_tenths >= ERRORS_CAUTION;
  // What captures actually saw beats the packet-count guess.
  bool loud_is_packets = (ev && ev->loud_source == SOURCE_PACKETS) || (n.mesh_known && n.mesh_busy && !(ev && ev->loud_source == SOURCE_CARRIER));
  bool decode_carrier = ev && ev->decode_source == SOURCE_CARRIER;

  if (in.batt_present && b.kind == BATT_K_LOW) s[CAUSE_BATT_LOW] = 100;
  if (in.batt_present && in.clock_set && b.hour_mv <= -15 && b.kind != BATT_K_LOW && !battFalling(b)) s[CAUSE_PANEL] = 30;

  if (in.radio_sx126x && agc_off && (e.freezes > 0 || in.frozen_now || (in.guard_known && !in.guard_on))) s[CAUSE_GUARD_OFF] = 90;
  // Protection still off with the AGC reset running: the reset failed to restore it.
  if (in.radio_sx126x && !agc_off && in.guard_known && !in.guard_on) s[CAUSE_GUARD_OFF] = 90;

  if (in.lna_available && in.study_result == LNA_V_NOT_WORKING && !in.study_active) s[CAUSE_LNA_FAULT] = 85;

  if (in.lna_available && in.lna_on && !in.study_active) {
    int v = 0;
    if (in.study_result == LNA_V_OVERLOADED) v = 70;
    if (overload_floor) v = v ? v + 15 : 60;
    // Freezes only ever with the LNA on, after real time spent with it off, point at the LNA.
    if (((ev && ev->lna_freezes_only_on) || (e.freezes_lna[1] >= 2 && e.freezes_lna[0] == 0)) && v) v += 10;
    else if (ev && ev->lna_freezes_only_on && e.freezes) v = 55;
    s[CAUSE_LNA_OVERLOAD] = (uint8_t)(v > 100 ? 100 : v);
  }

  if (in.radio_sx126x && (e.freezes > 0 || in.frozen_now)) {
    bool caught = in.store->scan_capture.at != 0 && in.store->scan_capture.kind != 0;
    // A carrier on the channel itself locks the receiver as surely as one beside it.
    bool seen = in.have_scan && (in.scan.blocker || in.scan.kind == SCAN_K_MESH_STEADY);
    if (caught || seen) s[CAUSE_BLOCKER] = (uint8_t)(caught && seen ? 95 : caught ? 85 : 75);
    else if (!s[CAUSE_GUARD_OFF] && !s[CAUSE_LNA_OVERLOAD]) s[CAUSE_FREEZES] = 45;
  } else if (in.have_scan && in.scan.kind == SCAN_K_EDGE_STEADY) {
    s[CAUSE_BLOCKER] = 35;
  }

  if (in.clock_set && n.hours >= NOISE_MIN_HOURS && n.loud_mask && !overload_floor) {
    int v = 45 + (n.max_rise > 10 ? 20 : n.max_rise * 2);
    if (n.kind == NOISE_K_LOUD_LOSS || n.kind == NOISE_K_LOUD_DROP) v += 10;
    if (in.have_scan && in.scan.kind == SCAN_K_MESH_STEADY) v += 10;
    if (in.store->noise_capture.at && in.store->noise_capture.kind) v += 10;
    if (ev && ev->loud_source == SOURCE_CARRIER) v += 10;
    if (ev && ev->noise_follows_charging) v += 15;
    // Loud hours that turned out to be another network's LoRa are that network, not noise.
    if (ev && ev->loud_source == SOURCE_FOREIGN) {
      s[CAUSE_FOREIGN_LORA] = 65;
      v -= 35;
    }
    if (loud_is_packets) v -= 35;
    s[CAUSE_NOISE] = (uint8_t)(v > 100 ? 100 : v < 0 ? 0 : v);
  } else if (in.have_scan && in.scan.kind == SCAN_K_MESH_STEADY) {
    s[CAUSE_NOISE] = 55;
  }

  if (e.link == LINK_V_MOST_WEAK) s[CAUSE_ANTENNA] = 80;
  // This site's usual says the floor has risen even when today's own hours all look alike.
  if (ev && ev->usual.ready && ev->usual.above && !n.loud_mask && !s[CAUSE_NOISE]) s[CAUSE_NOISE] = 45;
  if (ev && ev->fading.found && !s[CAUSE_WEAK_LINK]) s[CAUSE_WEAK_LINK] = 45;
  if (e.one_way >= 0) s[CAUSE_FAR_SIDE] = 55;
  if (e.drops < 0 && (e.link == LINK_V_CLOSE || e.link == LINK_V_CAN_DROP)) s[CAUSE_WEAK_LINK] = e.link == LINK_V_CLOSE ? 45 : 35;
  if (e.lean_weak && s[CAUSE_WEAK_LINK]) s[CAUSE_WEAK_LINK] += 15;

  if (e.load_pct >= LOAD_CAUTION) s[CAUSE_CONGESTION] = (uint8_t)(e.load_pct >= LOAD_PROBLEM ? 75 : 50);
  else if (n.loud_mask && loud_is_packets) s[CAUSE_CONGESTION] = 45;

  // Failed decodes that noise, freezes, the LNA and load all fail to explain are collisions or a strong neighbour.
  if (e.err_tenths >= ERRORS_CAUTION || n.kind == NOISE_K_ERRORS) {
    bool explained = (e.err && (inside(e.err, e.loud) || inside(e.err, e.freeze) || inside(e.err, e.busy)))
                     || (!e.err && (n.kind == NOISE_K_LOUD_LOSS || e.freezes > 0 || e.load_pct >= LOAD_CAUTION)) || s[CAUSE_LNA_OVERLOAD] >= 60;
    if (decode_carrier && !explained) {
      // A carrier caught at the failures is interference, whatever the hourly floor said.
      if (!s[CAUSE_NOISE]) s[CAUSE_NOISE] = 55;
    } else if (!explained && ev && ev->foreign_lora) {
      s[CAUSE_FOREIGN_LORA] = 60;
    } else if (!explained && ev && ev->strong >= 0) {
      s[CAUSE_STRONG_NEIGHBOUR] = 55;
    } else if (!explained) {
      s[CAUSE_COLLISIONS] = ev && ev->decode_source == SOURCE_PACKETS ? 60 : 50;
    }
  }

  if (in.batt_present && in.clock_set && battFalling(b)) {
    int panel = b.kind == BATT_K_NET_LOSS ? 30 : 60;
    if (in.hist_kind == HIST_K_LOSING) panel += 15;
    if (e.cold && b.charge_hours == 0) s[CAUSE_COLD] = 70;
    else if (e.cold) s[CAUSE_COLD] = 50;
    if (e.load_drain) s[CAUSE_LOAD_DRAIN] = 65;
    if (s[CAUSE_COLD] >= 70 || s[CAUSE_LOAD_DRAIN]) panel -= 25;
    s[CAUSE_PANEL] = (uint8_t)(panel < 5 ? 5 : panel);
  } else if (in.batt_present && in.hist_kind == HIST_K_LOSING) {
    s[CAUSE_PANEL] = 40;
  }

  if (in.temp_kind == TEMP_K_HOT) s[CAUSE_HEAT] = 40;
  if (ev && ev->heat_reboot >= 0) s[CAUSE_HEAT] = 75;

  if (in.lna_available && !in.study_active && !in.lna_auto && !s[CAUSE_LNA_OVERLOAD]) {
    int advice = lnaAdvice(in.study_result);
    if (advice == LNA_ON && !in.lna_on) s[CAUSE_LNA_SETTING] = 50;
    if (advice == LNA_OFF && in.lna_on && in.study_result == LNA_V_NO_BENEFIT) s[CAUSE_LNA_SETTING] = 20;
  }

  if (e.top_share >= 70 || (e.top_share >= 0 && e.neighbours == 1)) s[CAUSE_SINGLE_PATH] = 20;
  char text[40];
  if (linkClockSummary(*in.links, in.now_ms, text, sizeof(text)) == DIAG_CAUTION) s[CAUSE_CLOCK] = 15;
}

static EngineArea& show(EngineResult& r, DiagAreaId id) {
  EngineArea& a = r.area[id];
  a.shown = true;
  a.level = DIAG_UNKNOWN;
  a.cause = CAUSE_NONE;
  a.learning = false;
  a.text[0] = 0;
  return a;
}

static void append(EngineArea& a, const char* note) {
  size_t len = strlen(a.text);
  snprintf(a.text + len, sizeof(a.text) - len, "%s", note);
}

// The freeze cause in force, most certain first.
static uint8_t freezeCause(const uint8_t s[CAUSE_COUNT]) {
  if (s[CAUSE_GUARD_OFF]) return CAUSE_GUARD_OFF;
  if (s[CAUSE_BLOCKER] >= 75) return CAUSE_BLOCKER;
  if (s[CAUSE_LNA_OVERLOAD]) return CAUSE_LNA_OVERLOAD;
  return CAUSE_FREEZES;
}

static void areas(const EngineInput& in, const Evidence& e, EngineResult& r) {
  const uint8_t* s = r.score;
  const NoiseView& n = *in.noise;
  char id[8], text[16];

  EngineArea* a = &show(r, AREA_LINKS);
  a->level = (uint8_t)linkSummary(*in.links, in.sf, in.now_ms, a->text, sizeof(a->text));
  a->learning = true;
  if (e.drops >= 0 && e.link != LINK_V_MOST_WEAK) {
    linkLabel(*in.links, e.drops, id, sizeof(id));
    snprintf(a->text, sizeof(a->text), "%s drops in noise", id);
    a->level = DIAG_CAUTION;
    a->cause = CAUSE_NOISE;
  } else if (e.link == LINK_V_MOST_WEAK) a->cause = CAUSE_ANTENNA;
  else if (e.one_way_retry) {
    linkLabel(*in.links, e.one_way, id, sizeof(id));
    snprintf(a->text, sizeof(a->text), "%s resends fail", id);
    a->level = DIAG_CAUTION;
    a->cause = CAUSE_FAR_SIDE;
  } else if (e.link == LINK_V_ONE_WAY) {
    if (e.one_way >= 0) a->cause = CAUSE_FAR_SIDE;
    else {
      // A probe ping just proved the link two-way.
      snprintf(a->text, sizeof(a->text), "two-way, confirmed");
      a->level = DIAG_GOOD;
    }
  } else if (a->level == DIAG_CAUTION) a->cause = CAUSE_WEAK_LINK;
  if (in.ev && in.ev->fading.found && a->level != DIAG_PROBLEM && e.drops < 0 && e.link != LINK_V_ONE_WAY) {
    char who[8];
    if (in.ev->fading.id_len >= 2) snprintf(who, sizeof(who), "%04X", in.ev->fading.id);
    else snprintf(who, sizeof(who), "%02X", in.ev->fading.id & 0xFF);
    snprintf(a->text, sizeof(a->text), "%s fading, -%d dB", who, in.ev->fading.drop_db);
    a->level = DIAG_CAUTION;
    a->cause = CAUSE_WEAK_LINK;
  }

  if (in.lna_available) {
    a = &show(r, AREA_LNA);
    a->level = (uint8_t)lnaSummary(in.study_result, in.study_percent_on, in.study_active, in.study_h, in.study_of_h, in.lna_on, a->text, sizeof(a->text));
    a->learning = in.study_active;
    if (in.lna_auto) {
      // Auto owns the LNA: its state and blocks are the line; only a dead LNA is a problem.
      snprintf(a->text, sizeof(a->text), "%s", in.lna_auto);
      a->level = in.lna_auto_dead ? DIAG_PROBLEM : DIAG_GOOD;
      a->learning = false;
      if (in.lna_auto_dead) a->cause = CAUSE_LNA_FAULT;
    } else if (s[CAUSE_LNA_OVERLOAD] && in.study_result != LNA_V_OVERLOADED) {
      snprintf(a->text, sizeof(a->text), "on hurts here");
      a->level = DIAG_CAUTION;
    }
    if (!in.lna_auto && (a->level == DIAG_CAUTION || a->level == DIAG_PROBLEM)) {
      a->cause = s[CAUSE_LNA_FAULT] ? CAUSE_LNA_FAULT : s[CAUSE_LNA_OVERLOAD] ? CAUSE_LNA_OVERLOAD : CAUSE_LNA_SETTING;
    }
  }

  if (in.radio_sx126x) {
    a = &show(r, AREA_NEARBY);
    if (!in.have_scan) {
      snprintf(a->text, sizeof(a->text), "no scan yet");
    } else {
      a->level = (uint8_t)scanSummary(in.scan, a->text, sizeof(a->text));
      if (a->level == DIAG_PROBLEM) a->cause = CAUSE_NOISE;
      if (a->level == DIAG_CAUTION) a->cause = CAUSE_BLOCKER;
    }
  }

  a = &show(r, AREA_NOISE);
  if (!in.clock_set) {
    snprintf(a->text, sizeof(a->text), "clock not set");
  } else {
    a->level = (uint8_t)noiseSummary(n, a->text, sizeof(a->text));
    a->learning = true;
    int8_t shift = in.ev && in.ev->live_floor_shift ? in.ev->live_floor_shift : in.study_floor_shift;
    bool packets = n.kind == NOISE_K_BUSY || (in.ev && in.ev->loud_source == SOURCE_PACKETS) || (n.mesh_known && n.mesh_busy);
    if (s[CAUSE_LNA_OVERLOAD] >= 60 && shift >= OVERLOAD_SHIFT) {
      // The LNA lifts the whole floor, so the "noise" is the node's own front end.
      snprintf(a->text, sizeof(a->text), "floor +%d dB, from LNA", shift);
      a->level = DIAG_CAUTION;
      a->cause = CAUSE_LNA_OVERLOAD;
    } else if (!n.loud_mask && in.ev && in.ev->usual.ready && in.ev->usual.above && n.kind != NOISE_K_FREEZES) {
      // Every hour today looks alike, but this site's usual says the whole floor has risen.
      snprintf(a->text, sizeof(a->text), "+%d dB over usual", in.ev->usual.worst_db);
      a->level = DIAG_CAUTION;
      a->cause = CAUSE_NOISE;
    } else if (n.kind == NOISE_K_FREEZES) a->cause = freezeCause(s);
    else if (n.kind == NOISE_K_ERRORS) a->cause = s[CAUSE_STRONG_NEIGHBOUR] ? CAUSE_STRONG_NEIGHBOUR : s[CAUSE_NOISE] ? CAUSE_NOISE : CAUSE_COLLISIONS;
    else if (a->level == DIAG_CAUTION || a->level == DIAG_PROBLEM) {
      if (in.ev && in.ev->loud_source == SOURCE_FOREIGN) {
        append(*a, ", other LoRa");
        a->cause = CAUSE_FOREIGN_LORA;
      } else if (packets) {
        if (n.kind != NOISE_K_BUSY) append(*a, ", busy mesh");
        a->cause = CAUSE_CONGESTION;
      } else {
        a->cause = CAUSE_NOISE;
      }
    }
  }

  a = &show(r, AREA_GUARD);
  if (!in.radio_sx126x) {
    snprintf(a->text, sizeof(a->text), "not on this radio");
  } else {
    bool agc_off = in.agc_interval == 0;
    a->level = DIAG_GOOD;
    if (in.frozen_now) {
      snprintf(a->text, sizeof(a->text), agc_off ? "Frozen Now, AGC Off" : "Frozen Now");
      a->level = DIAG_PROBLEM;
    } else if (e.freezes && agc_off) {
      snprintf(a->text, sizeof(a->text), "%u Freeze%s, AGC Off", e.freezes, e.freezes == 1 ? "" : "s");
      a->level = DIAG_PROBLEM;
    } else if (in.guard_known && !in.guard_on && agc_off) {
      snprintf(a->text, sizeof(a->text), "Off, AGC Off");
      a->level = DIAG_PROBLEM;
    } else if (e.freezes) {
      bool blocker = s[CAUSE_BLOCKER] >= 75;
      snprintf(a->text, sizeof(a->text), "%u Freeze%s, %s", e.freezes, e.freezes == 1 ? "" : "s", blocker ? "blocker" : "Cleared");
      a->level = blocker ? DIAG_PROBLEM : DIAG_CAUTION;
    } else if (in.guard_known && !in.guard_on) {
      snprintf(a->text, sizeof(a->text), "Off, not restored");
      a->level = DIAG_PROBLEM;
    } else if (e.off_count) {
      snprintf(a->text, sizeof(a->text), "Off %ux, restored", e.off_count);
      a->level = DIAG_CAUTION;
    } else {
      snprintf(a->text, sizeof(a->text), "No Freezes");
    }
    if (a->level != DIAG_GOOD) a->cause = e.freezes || in.frozen_now ? freezeCause(s) : agc_off || (in.guard_known && !in.guard_on) ? CAUSE_GUARD_OFF : CAUSE_NONE;
  }

  a = &show(r, AREA_DECODES);
  a->learning = true;
  if (e.err_tenths < 0) {
    snprintf(a->text, sizeof(a->text), "%lu of %d pkts", (unsigned long)(in.recv + in.errors), ERRORS_MIN);
  } else {
    a->level = e.err_tenths >= ERRORS_PROBLEM ? DIAG_PROBLEM : e.err_tenths >= ERRORS_CAUTION ? DIAG_CAUTION : DIAG_GOOD;
    snprintf(text, sizeof(text), "%d.%d%%", e.err_tenths / 10, e.err_tenths % 10);
    const char* note = " failed";
    static char share_note[24];
    const TrendShares* sh = in.ev && in.ev->shares.errors >= 10 ? &in.ev->shares : nullptr;
    if (a->level != DIAG_GOOD && sh && sh->noise >= 50 && sh->noise >= 2 * sh->noise_hours && s[CAUSE_NOISE]) {
      // Measured: most of the failures fell in the loud hours.
      a->cause = CAUSE_NOISE;
      snprintf(share_note, sizeof(share_note), ", %u%% in noise", sh->noise);
      note = share_note;
    } else if (a->level != DIAG_GOOD && sh && sh->freeze >= 50 && e.freezes) {
      a->cause = freezeCause(s);
      snprintf(share_note, sizeof(share_note), ", %u%% in freezes", sh->freeze);
      note = share_note;
    } else if (a->level != DIAG_GOOD && sh && sh->busy >= 50 && s[CAUSE_CONGESTION]) {
      a->cause = CAUSE_CONGESTION;
      snprintf(share_note, sizeof(share_note), ", %u%% when busy", sh->busy);
      note = share_note;
    } else if (a->level != DIAG_GOOD) {
      if (s[CAUSE_LNA_OVERLOAD] >= 60) {
        a->cause = CAUSE_LNA_OVERLOAD;
        note = ", from LNA";
      } else if (e.freezes && (!e.err || inside(e.err, e.freeze)) && !inside(e.err, e.loud)) {
        a->cause = freezeCause(s);
        note = ", in freezes";
      } else if (s[CAUSE_NOISE] && (inside(e.err, e.loud) || (!e.err && n.kind == NOISE_K_LOUD_LOSS))) {
        a->cause = CAUSE_NOISE;
        note = ", from noise";
      } else if (s[CAUSE_CONGESTION] && (inside(e.err, e.busy) || !e.err)) {
        a->cause = CAUSE_CONGESTION;
        note = ", from load";
      } else if (s[CAUSE_FOREIGN_LORA]) {
        a->cause = CAUSE_FOREIGN_LORA;
        note = ", other LoRa";
      } else if (s[CAUSE_STRONG_NEIGHBOUR]) {
        a->cause = CAUSE_STRONG_NEIGHBOUR;
        note = ", strong nbr";
      } else if (s[CAUSE_NOISE] && in.ev && in.ev->decode_source == SOURCE_CARRIER) {
        a->cause = CAUSE_NOISE;
        note = ", carrier";
      } else {
        a->cause = CAUSE_COLLISIONS;
      }
    }
    snprintf(a->text, sizeof(a->text), "%s%s", text, note);
  }

  a = &show(r, AREA_BATTERY);
  if (!in.batt_present) {
    snprintf(a->text, sizeof(a->text), "not available");
  } else if (!in.clock_set) {
    snprintf(a->text, sizeof(a->text), "clock not set");
  } else {
    a->level = (uint8_t)battSummary(*in.batt, a->text, sizeof(a->text));
    a->learning = true;
    // The last hour's slope, so a battery that has just turned is seen before the day's trend shows it.
    if (in.batt->hour_mv <= -15 && a->level != DIAG_PROBLEM) {
      snprintf(a->text, sizeof(a->text), "falling now, %d mV/h", in.batt->hour_mv);
      if (a->level == DIAG_GOOD || a->level == DIAG_UNKNOWN) a->level = DIAG_CAUTION;
    }
    if (a->level == DIAG_PROBLEM) a->cause = CAUSE_BATT_LOW;
    else if (a->level == DIAG_CAUTION) {
      a->cause = s[CAUSE_COLD] >= s[CAUSE_PANEL] && s[CAUSE_COLD] ? CAUSE_COLD : s[CAUSE_LOAD_DRAIN] > s[CAUSE_PANEL] ? CAUSE_LOAD_DRAIN : CAUSE_PANEL;
    }
  }

  a = &show(r, AREA_LOAD);
  a->learning = true;
  if (e.load_pct < 0) {
    snprintf(a->text, sizeof(a->text), "%lu of %d min up", (unsigned long)(in.uptime_s / 60), LOAD_MIN_S / 60);
  } else {
    snprintf(a->text, sizeof(a->text), "channel %d%% busy", e.load_pct);
    a->level = e.load_pct >= LOAD_PROBLEM ? DIAG_PROBLEM : e.load_pct >= LOAD_CAUTION ? DIAG_CAUTION : DIAG_GOOD;
    if (a->level != DIAG_GOOD) a->cause = CAUSE_CONGESTION;
  }

  a = &show(r, AREA_PATHS);
  a->level = (uint8_t)linkPathsSummary(*in.links, in.now_ms, a->text, sizeof(a->text));
  a->learning = true;
  if (a->level == DIAG_CAUTION) {
    if (e.lean_weak && s[CAUSE_WEAK_LINK]) {
      linkLabel(*in.links, e.top, id, sizeof(id));
      snprintf(a->text, sizeof(a->text), "leans on weak %s", id);
      a->cause = CAUSE_WEAK_LINK;
    } else {
      a->cause = CAUSE_SINGLE_PATH;
    }
  }

  a = &show(r, AREA_CLOCK);
  a->level = (uint8_t)linkClockSummary(*in.links, in.now_ms, a->text, sizeof(a->text));
  if (a->level == DIAG_CAUTION) a->cause = CAUSE_CLOCK;
}

// The cause that leads: worst area first, then causes before symptoms, then the one that explains most, then score.
static void lead(EngineResult& r) {
  uint8_t worst[CAUSE_COUNT] = {}, count[CAUSE_COUNT] = {};
  for (int i = 0; i < AREA_COUNT; i++) {
    const EngineArea& a = r.area[i];
    if (!a.shown || a.cause == CAUSE_NONE || (a.level != DIAG_CAUTION && a.level != DIAG_PROBLEM)) continue;
    if (a.level > worst[a.cause]) worst[a.cause] = a.level;
    count[a.cause]++;
  }
  // A cause with no area of its own still leads when nothing else explains the node's state.
  if (r.score[CAUSE_HEAT] && !worst[CAUSE_HEAT]) worst[CAUSE_HEAT] = DIAG_CAUTION;
  r.cause = CAUSE_NONE;
  r.level = DIAG_GOOD;
  for (uint8_t c = 1; c < CAUSE_COUNT; c++) {
    if (!worst[c]) continue;
    bool better = r.cause == CAUSE_NONE || worst[c] > r.level
                  || (worst[c] == r.level && (RANK[c] < RANK[r.cause]
                      || (RANK[c] == RANK[r.cause] && (count[c] > count[r.cause] || (count[c] == count[r.cause] && r.score[c] > r.score[r.cause])))));
    if (better) {
      r.cause = c;
      r.level = worst[c];
    }
  }
  r.explained = count[r.cause];
}

static void clockText(uint32_t epoch, int8_t tz, char* out, size_t len) {
  uint32_t local = epoch + (int32_t)tz * 3600;
  snprintf(out, len, "%lu:%02lu", (unsigned long)(local / 3600 % 24), (unsigned long)(local / 60 % 60));
}

static void hoursText(uint32_t mask, char* out, size_t len) {
  bool flags[24];
  uint8_t start = 0, run = 0;
  int runs = 0;
  for (int i = 0; i < 24; i++) flags[i] = mask >> i & 1;
  if (!diagLongestRun(flags, &start, &run, &runs)) {
    out[0] = 0;
    return;
  }
  if (run == 24) snprintf(out, len, "all day");
  else if (run == 1) diagHourText(start, false, out, len);
  else diagSpanText(start, run, out, len);
}

static void advise(const EngineInput& in, const Evidence& e, EngineResult& r) {
  char id[8] = "", at[8], span[20];
  const NoiseView& n = *in.noise;
  const char* lna_off = in.lna_available && in.lna_on ? "tryset 86400 radio.fem.rxgain off" : nullptr;
  r.fix[0][0] = r.fix[1][0] = r.see[0] = r.confidence[0] = 0;
  r.cause_hours = 0;
#define HEAD(...) snprintf(r.headline, sizeof(r.headline), __VA_ARGS__)
#define FIX0(...) snprintf(r.fix[0], sizeof(r.fix[0]), __VA_ARGS__)
#define FIX1(...) snprintf(r.fix[1], sizeof(r.fix[1]), __VA_ARGS__)
#define SEE(...) snprintf(r.see, sizeof(r.see), __VA_ARGS__)
  switch (r.cause) {
    case CAUSE_BATT_LOW:
      HEAD("battery low");
      FIX0("charge it now, or it");
      FIX1("may stop soon.");
      SEE("diag batt");
      break;
    case CAUSE_GUARD_OFF:
      if (in.agc_interval) {
        HEAD("guard off, not restored");
        FIX0("the AGC reset left lock-up");
        FIX1("protection off: reboot the node.");
        SEE("reboot");
        break;
      }
      HEAD(e.freezes || in.frozen_now ? "freezes, AGC off" : "unprotected, AGC off");
      FIX0("nothing clears a frozen");
      FIX1("receiver: turn the AGC reset on.");
      SEE("set agc.reset.interval 16");
      break;
    case CAUSE_NOISE:
      r.cause_hours = n.loud_mask;
      if (!n.loud_mask && in.ev && in.ev->usual.above) {
        diagHourText(in.ev->usual.worst_hour, false, at, sizeof(at));
        HEAD("floor above usual at %s", at);
        FIX0("%d dB over this site's usual:", in.ev->usual.worst_db);
        FIX1("something new; scan at %s.", at);
        r.cause_hours = in.ev->usual.above;
        SEE("diag radio.rfscan");
        break;
      }
      if (n.loud_mask) {
        noiseLoudText(n, span, sizeof(span));
        HEAD("noise %s", span);
        diagHourText((uint8_t)(n.loud_start + (n.loud_len > 1 ? 1 : 0)), false, at, sizeof(at));
        if (in.ev && in.ev->noise_follows_charging) {
          FIX0("noise follows charging: the");
          FIX1("charger or panel leads; add ferrites.");
        } else if (in.store->noise_capture.at && in.store->noise_capture.kind) {
          clockText(in.store->noise_capture.at, in.tz, span, sizeof(span));
          FIX0("a steady carrier,");
          FIX1("caught %s; scan at %s to find it.", span, at);
        } else if (in.ev && in.ev->schedule.found && evInside(n.loud_mask, in.ev->schedule.hours)) {
          FIX0("loud %s every week:", in.ev->schedule.weekdays_only ? "weekdays" : "daily");
          FIX1("scan at %s%s.", at, in.ev->schedule.weekdays_only ? " on a weekday" : "");
        } else {
          FIX0("scan at %s to find it.", at);
        }
      } else {
        HEAD("signal on the channel");
        if (in.scan.peak_khz == 0) FIX0("confirmed on %.3f MHz,", in.scan.peak_mhz);
        else FIX0("peak %.3f MHz, %+dk from ours,", in.scan.peak_mhz, in.scan.peak_khz);
        FIX1("%d dB over the floor.", in.scan.db);
      }
      SEE("diag radio.rfscan");
      break;
    case CAUSE_BLOCKER: {
      r.cause_hours = e.freeze;
      const StoreCapture& c = in.store->scan_capture;
      hoursText(e.freeze, span, sizeof(span));
      if (e.freezes == 0 && !in.frozen_now) {
        HEAD("strong signal nearby");
        FIX0("it can freeze the receiver: move");
        FIX1("the antenna away; keep AGC reset on.");
        SEE("diag radio.rfscan");
        break;
      }
      HEAD("radio freezing");
      if (in.have_scan && in.scan.kind == SCAN_K_MESH_STEADY && !in.scan.blocker) {
        if (in.scan.peak_khz == 0) FIX0("a signal on %.3f MHz,", in.scan.peak_mhz);
        else FIX0("a signal %+dk from ours,", in.scan.peak_khz);
        FIX1("%d dB up, locks the receiver.", in.scan.db);
        SEE("diag radio.rfscan");
        break;
      }
      if (c.at && c.kind) {
        clockText(c.at, in.tz, at, sizeof(at));
        FIX0("caught %s, %d dBm", at, c.b);
        FIX1("%+dk off channel; freezes %s.", c.offset_khz, span);
      } else {
        FIX0("a transmitter beside the");
        FIX1("channel freezes the receiver.");
      }
      if (lna_off) SEE("%s", lna_off);
      else {
        FIX1("move the antenna away from it.");
        SEE("diag radio.rfscan");
      }
      break;
    }
    case CAUSE_FREEZES:
      r.cause_hours = e.freeze;
      hoursText(e.freeze, span, sizeof(span));
      HEAD("radio froze %ux", e.freezes);
      FIX0("cause not on air at");
      FIX1("scan; freezes cluster %s.", span);
      {
        bool flags[24];
        uint8_t start = 0, run = 0;
        int runs = 0;
        for (int i = 0; i < 24; i++) flags[i] = e.freeze >> i & 1;
        diagLongestRun(flags, &start, &run, &runs);
        diagHourText(start, false, at, sizeof(at));
        SEE("diag radio.rfscan at %s", at);
      }
      break;
    case CAUSE_ANTENNA:
      HEAD("all links weak");
      FIX0("weak on all sides: check");
      FIX1("our antenna, cable and connector.");
      SEE("diag radio.links");
      break;
    case CAUSE_LNA_FAULT:
      HEAD("LNA not working");
      FIX0("switching it barely");
      FIX1("moves the floor: check the FEM.");
      SEE("diag radio.lna");
      break;
    case CAUSE_LNA_OVERLOAD:
      HEAD("LNA overload");
      FIX0("turn the LNA off.");
      SEE("tryset 86400 radio.fem.rxgain off");
      break;
    case CAUSE_COLD:
      HEAD("cold, not charging");
      if (in.has_temp) FIX0("CPU %d°C: chargers", in.temp_now);
      else FIX0("below 0°C: chargers");
      FIX1("pause; it resumes when it warms.");
      SEE("diag temp");
      break;
    case CAUSE_LOAD_DRAIN:
      HEAD("drain follows load");
      FIX0("busy days cost the");
      FIX1("battery, not the panel: cut load.");
      SEE("diag batt");
      break;
    case CAUSE_PANEL:
      if (in.batt->kind == BATT_K_NET_LOSS) {
        HEAD("battery net loss");
        FIX0("watch it: may just be");
        FIX1("a low sun day.");
      } else {
        HEAD("battery not refilling");
        FIX0("check the panel or");
        FIX1("charger: it isn't holding.");
      }
      SEE("diag batt");
      break;
    case CAUSE_FAR_SIDE:
      linkLabel(*in.links, e.one_way, id, sizeof(id));
      HEAD("%s one-way", id);
      if (e.one_way_retry) {
        FIX0("%u of %u resends unconfirmed:", (unsigned)(in.retry_tried - in.retry_won), (unsigned)in.retry_tried);
        FIX1("it may not hear us; check its antenna.");
      } else {
        FIX0("it may not hear us:");
        FIX1("check TX power, or its antenna.");
      }
      SEE("diag probe");
      break;
    case CAUSE_WEAK_LINK: {
      float margin = 0;
      linkWeakest(*in.links, in.sf, in.now_ms, &margin, id, sizeof(id));
      if (in.ev && in.ev->fading.found && !e.lean_weak) {
        if (in.ev->fading.id_len >= 2) snprintf(id, sizeof(id), "%04X", in.ev->fading.id);
        else snprintf(id, sizeof(id), "%02X", in.ev->fading.id & 0xFF);
        HEAD("%s fading %d dB", id, in.ev->fading.drop_db);
        FIX0("an antenna, cable or connector");
        FIX1("at one end is degrading.");
        SEE("diag radio.links");
        break;
      }
      if (e.lean_weak) {
        linkLabel(*in.links, e.top, id, sizeof(id));
        HEAD("paths lean on weak %s", id);
      } else {
        HEAD("%s weak", id);
      }
      FIX0("a higher antenna or a");
      FIX1("relay helps most.");
      SEE("diag radio.links");
      break;
    }
    case CAUSE_CONGESTION:
      r.cause_hours = e.busy;
      HEAD("channel busy");
      FIX0("a faster preset can help.");
      break;
    case CAUSE_COLLISIONS:
      r.cause_hours = e.err;
      HEAD("packets colliding");
      FIX0("repeats overlap: spread them,");
      FIX1("raise txdelay.");
      SEE("set txdelay 1");
      break;
    case CAUSE_LNA_SETTING:
      if (lnaAdvice(in.study_result) == LNA_ON) {
        HEAD("LNA off, study says on");
        FIX0("turn the LNA on.");
        SEE("tryset 86400 radio.fem.rxgain on");
      } else {
        HEAD("LNA brings no benefit");
        FIX0("off saves power.");
        SEE("tryset 86400 radio.fem.rxgain off");
      }
      break;
    case CAUSE_HEAT:
      if (in.ev && in.ev->heat_reboot >= 0) {
        HEAD("heat reboots");
        FIX0("restarted after %d°C:", in.store->reboot[in.ev->heat_reboot].temp_before);
        FIX1("shade or vent the enclosure.");
      } else {
        HEAD("running hot");
        FIX0("shade or vent the");
        FIX1("enclosure.");
      }
      SEE("diag temp");
      break;
    case CAUSE_FOREIGN_LORA:
      r.cause_hours = e.loud ? e.loud : e.err;
      HEAD("another LoRa network here");
      FIX0("LoRa that never decodes shares");
      FIX1("our frequency: its packets collide.");
      SEE("diag radio.noise");
      break;
    case CAUSE_STRONG_NEIGHBOUR: {
      char who[8];
      linkLabel(*in.links, in.ev->strong, who, sizeof(who));
      r.cause_hours = e.err;
      HEAD("%s swamps the receiver", who);
      FIX0("decodes fail with no noise");
      FIX1("rise; space the antennas apart.");
      SEE("diag radio.links");
      break;
    }
    case CAUSE_SINGLE_PATH:
      HEAD("one path out");
      FIX0("a second neighbour in");
      FIX1("range adds backup.");
      SEE("diag radio.links");
      break;
    case CAUSE_CLOCK:
      HEAD("clock off");
      FIX0("sync the clock from");
      FIX1("the app.");
      break;
    default:
      HEAD("all good");
      break;
  }
#undef HEAD
#undef FIX0
#undef FIX1
#undef SEE
}

// How well the verdict is established: recurrence across days, else how much has been watched.
static void confidence(const EngineInput& in, EngineResult& r) {
  r.confidence[0] = 0;
  if (r.cause == CAUSE_NONE || !in.clock_set) return;
  bool hourly = r.cause == CAUSE_NOISE || r.cause == CAUSE_BLOCKER || r.cause == CAUSE_FREEZES || r.cause == CAUSE_CONGESTION
                || r.cause == CAUSE_COLLISIONS || r.cause == CAUSE_PANEL || r.cause == CAUSE_COLD || r.cause == CAUSE_LOAD_DRAIN
                || r.cause == CAUSE_GUARD_OFF || r.cause == CAUSE_LNA_OVERLOAD;
  if (!hourly) return;
  uint32_t today = storeLocalDay(in.now_hour, in.tz);
  int recorded = 0;
  // Finished days come from the ring; today counts as one more sighting.
  int seen = 1 + storeRecurCount(*in.store, today - 1, r.cause, r.cause_hours, &recorded);
  int days = recorded + 1;
  if (recorded == 0) {
    if (in.noise->hours > 0 && in.noise->hours < 24) snprintf(r.confidence, sizeof(r.confidence), "(early: %uh seen)", in.noise->hours);
    return;
  }
  snprintf(r.confidence, sizeof(r.confidence), "(%sseen %d of %d days)", seen >= SURE_DAYS ? "sure: " : "", seen, days);
}

void engineRun(const EngineInput& in, EngineResult& r) {
  memset(&r, 0, sizeof(r));
  Evidence e;
  gather(in, e);
  score(in, e, r.score);
  areas(in, e, r);
  lead(r);
  advise(in, e, r);
  confidence(in, r);
}

void engineFormat(const EngineInput& in, const EngineResult& r, DiagOut& o) {
  // An empty first line, so the area rows start on their own line after the serial `-> ` prefix.
  outLine(o, "%s", "");
  int unknown = 0;
  for (int i = 0; i < AREA_COUNT; i++) {
    const EngineArea& a = r.area[i];
    if (!a.shown) continue;
    if (a.level == DIAG_UNKNOWN && a.learning) unknown++;
    outLine(o, "%s %-8s %s %s", ICON[i], NAME[i], diagOrb(a.level), a.text);
  }
  outSep(o);
  if (r.cause == CAUSE_NONE) {
    outLine(o, "🩺 %-8s %s %s", "Diag", diagOrb(unknown ? DIAG_UNKNOWN : DIAG_GOOD), unknown ? "still learning" : "all good");
    outLine(o, unknown ? "Check again in a day." : "Nothing to fix.");
    char health[64];
    if (!unknown && in.clock_set) {
      trendHealth(*in.store, in.now_hour, in.tz, health, sizeof(health));
      if (health[0]) outLine(o, "Trend: %s", health);
    }
  } else {
    outLine(o, "🩺 %-8s %s %s", "Diag", diagOrb(r.level), r.headline);
    outLine(o, "Best fix: %s", r.fix[0]);
    if (r.fix[1][0]) outLine(o, "%s", r.fix[1]);
    if (r.see[0]) outLine(o, "See: %s", r.see);
    if (r.confidence[0]) outLine(o, "%s", r.confidence);
  }
  // A quiet report with captures stood down must not read as a clean one.
  if (in.auto_state == ENGINE_AUTO_OFF) outLine(o, "Auto captures off.");
  if (in.auto_state == ENGINE_AUTO_PAUSED) outLine(o, "Auto captures paused %um.", in.pause_min);
}
