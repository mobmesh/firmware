#include <helpers/diag_reports/DiagRelate.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

#define WEAK_SIDE_DB    3.0f
#define DRAIN_MIN_DAYS  3
#define DRAIN_WORSE_MV  15
#define COLD_DAY_C      5

static void label(const LinkTable& t, int index, char* out, size_t len) {
  linkLabel(t, index, out, len);
}

static void hourAt(uint32_t mask, char* out, size_t len) {
  int start = evRunStart(mask);
  // An hour into the run, when the source is surely on.
  diagHourText((uint8_t)((start < 0 ? 0 : start) + (evBits(mask) > 1 ? 1 : 0)), false, out, len);
}

void relateLinks(const DiagEvidence& e, const LinkTable& t, uint8_t sf, uint32_t now_ms, DiagNote& n) {
  noteReset(n);
  char id[8], hours[20];
  if (e.one_way >= 0) {
    label(t, e.one_way, id, sizeof(id));
    if (e.ping_ok && e.ping_id == t.nb[e.one_way].id) {
      // The ping measured both directions: the weaker one names the side to fix.
      if (e.ping_out_db + WEAK_SIDE_DB < e.ping_back_db) {
        noteAdd(n, "Ping: out %+.1f, back %+.1f.", e.ping_out_db, e.ping_back_db);
        noteAdd(n, "Their side hears us weakly:");
        noteAdd(n, "check %s's antenna.", id);
      } else if (e.ping_back_db + WEAK_SIDE_DB < e.ping_out_db) {
        noteAdd(n, "Ping: out %+.1f, back %+.1f.", e.ping_out_db, e.ping_back_db);
        noteAdd(n, "We hear it back weakly:");
        noteAdd(n, "check our antenna and cable.");
      } else {
        noteAdd(n, "Ping to %s works both ways:", id);
        noteAdd(n, "it does not relay our packets.");
      }
      return;
    }
    noteAdd(n, "%s heard well, but none of %lu", id, (unsigned long)t.echoes);
    noteAdd(n, "echoes came via it.");
    noteAdd(n, "See: diag probe");
    return;
  }
  if (e.lean_weak) {
    label(t, e.top, id, sizeof(id));
    noteAdd(n, "%d%% of paths go via weak %s:", e.top_share, id);
    noteAdd(n, "a second neighbour adds backup.");
  }
  if (e.strong >= 0 && e.err && !e.loud) {
    label(t, e.strong, id, sizeof(id));
    noteAdd(n, "%s at %+d dB margin can swamp", id, (int)lroundf(linkMargin(t, e.strong, sf)));
    noteAdd(n, "the receiver: decodes fail.");
  }
  // Slow change over two weeks: a link losing level before it drops.
  if (e.fading.found) {
    char who[8], since[12];
    if (e.fading.id_len >= 2) snprintf(who, sizeof(who), "%04X", e.fading.id);
    else snprintf(who, sizeof(who), "%02X", e.fading.id & 0xFF);
    diagDateText(e.fading.since_day, since, sizeof(since));
    noteAdd(n, "%s down %d dB since %s:", who, e.fading.drop_db, since);
    noteAdd(n, "antenna or cable degrading.");
    int days = trendDaysToLimit(e.fading_margin, e.fading.slope_db_day);
    if (days >= 0) noteAdd(n, "At this rate at its limit in ~%d days.", days);
  }
  // How settled the weakest link's verdict is: hours it was heard in the last day.
  float margin;
  (void)margin;
  if (linkWeakest(t, sf, now_ms, &margin, id, sizeof(id))) {
    for (int i = 0; i < t.count; i++) {
      char other[8];
      label(t, i, other, sizeof(other));
      if (strcmp(other, id) != 0) continue;
      int heard = evBits(t.nb[i].heard);
      if (heard) {
        evHoursText(evLocalMask(t.nb[i].weak, e.tz), hours, sizeof(hours));
        // Weak in some hours and fine in others is a pattern worth naming, even when the average margin looks solid.
        if (hours[0] && t.nb[i].weak != t.nb[i].heard) noteAdd(n, "%s weak %s (heard %d of 24h)", id, hours, heard);
        else noteAdd(n, "(%s heard %d of 24h)", id, heard);
      }
    }
  }
}

static bool sharesNoise(const DiagEvidence& e) {
  return e.shares.errors >= 10 && e.shares.noise >= 50 && e.shares.noise >= 2 * e.shares.noise_hours;
}

void relateSourceHint(const DiagEvidence& e, int8_t max_rise, char* out, size_t len) {
  out[0] = 0;
  if (!e.loud) return;
  const char* strength = max_rise >= 20 ? "Very strong" : max_rise >= 10 ? "Strong" : "Weak";
  const char* steady = e.loud_source == SOURCE_CARRIER ? ", steady" : e.loud_source == SOURCE_PACKETS ? ", bursts" : "";
  const uint32_t day = 0x07FF80, evening = 0xFE0000;   // 7am-7pm, 5pm-midnight
  const char* where;
  if (e.noise_follows_charging) where = "follows charging: own charger or panel";
  else if (e.schedule.found && e.schedule.weekdays_only) where = "weekdays: a workplace";
  else if (evInside(e.loud, day)) where = max_rise >= 10 ? "daytime: solar gear close by" : "daytime: solar or work hours";
  else if (evInside(e.loud, evening)) where = "evenings: lights or household";
  else where = max_rise >= 20 ? "close by" : max_rise >= 10 ? "nearby" : "distant or faint";
  snprintf(out, len, "%s%s, %s.", strength, steady, where);
}

void relateNoise(const DiagEvidence& e, const LinkTable& t, const NoiseView& v, DiagNote& n) {
  noteReset(n);
  char id[8], hours[20], at[8];
  // Against this site's own usual, before anything about today alone.
  if (e.usual.ready && e.usual.above && !v.loud_len) {
    diagHourText(e.usual.worst_hour, false, at, sizeof(at));
    noteAdd(n, "Floor %d dB above its usual for %s:", e.usual.worst_db, at);
    noteAdd(n, "something new is on air.");
  }
  if (e.foreign_lora && (v.loud_len || v.kind == NOISE_K_ERRORS)) {
    noteAdd(n, "LoRa that never decodes: another");
    noteAdd(n, "network shares our frequency.");
  }
  if (v.kind == NOISE_K_BUSY) {
    noteAdd(n, e.loud_source == SOURCE_PACKETS ? "Caught bursts: mesh traffic," : "Twice the packets in those hours:");
    noteAdd(n, "busy, not interference.");
    noteAdd(n, "See: diag radio.links");
    return;
  }
  if (v.loud_len) {
    if (e.schedule.found && evInside(e.loud, e.schedule.hours)) {
      evHoursText(e.schedule.hours, hours, sizeof(hours));
      noteAdd(n, "Loud %s %s, every week.", e.schedule.weekdays_only ? "weekdays" : "daily", hours);
    }
    if (e.loud_source == SOURCE_CARRIER) noteAdd(n, "Caught a steady carrier, not packets.");
    char hint[48];
    relateSourceHint(e, v.max_rise, hint, sizeof(hint));
    if (hint[0]) noteAdd(n, "%s", hint);
    if (sharesNoise(e)) noteAdd(n, "Noise explains %u%% of failed decodes.", e.shares.noise);
    if (e.drops >= 0) {
      label(t, e.drops, id, sizeof(id));
      evHoursText(e.loud, hours, sizeof(hours));
      noteAdd(n, "%s drops %s.", id, hours);
    } else if (v.have_margin) {
      noteAdd(n, "Links keep %d+ dB margin through it.", v.spare_db);
    }
    if (e.scan_explains_loud) {
      diagHourText(e.scan_hour, false, at, sizeof(at));
      noteAdd(n, "Scan at %s found it on channel.", at);
      noteAdd(n, "See: diag radio.rfscan");
    } else {
      hourAt(e.loud, at, sizeof(at));
      noteAdd(n, "Try: diag radio.rfscan at %s.", at);
    }
    return;
  }
  if (!e.freezes && e.strong_hit) {
    evHoursText(e.strong_hit, hours, sizeof(hours));
    noteAdd(n, "A strong signal on the channel at");
    noteAdd(n, "resets %s: a transmitter nearby.", hours);
  }
  if (e.freezes) {
    evHoursText(e.freeze, hours, sizeof(hours));
    noteAdd(n, "Receiver froze %ux %s: not noise.", e.freezes, hours);
    if (e.lna_freezes_only_on) noteAdd(n, "Only with the LNA on.");
    else if (e.scan_explains_freeze) noteAdd(n, "A blocker sits %+dk off channel.", e.scan.blocker_khz);
    noteAdd(n, "See: diag");
    return;
  }
  if (v.kind == NOISE_K_ERRORS) {
    diagHourText(v.err_start, false, at, sizeof(at));
    if (e.decode_source == SOURCE_CARRIER) {
      noteAdd(n, "A carrier was on air at the failures:");
      noteAdd(n, "interference, not collisions.");
      noteAdd(n, "Try: diag radio.rfscan at %s.", at);
    } else if (e.strong >= 0) {
      label(t, e.strong, id, sizeof(id));
      noteAdd(n, "Fails with no noise rise; %s is", id);
      noteAdd(n, "strong enough to swamp it.");
      noteAdd(n, "See: diag radio.links");
    } else {
      noteAdd(n, "Fails with no noise rise:");
      noteAdd(n, e.decode_source == SOURCE_PACKETS ? "bursts at the failures: collisions." : "collisions or a strong neighbour.");
      noteAdd(n, "See: diag radio.links");
    }
  }
}

void relateScan(const DiagEvidence& e, DiagNote& n) {
  noteReset(n);
  char ran[8], hours[20], at[8];
  diagHourText(e.scan_hour, false, ran, sizeof(ran));
  if (e.scan_explains_freeze) {
    evHoursText(e.freeze, hours, sizeof(hours));
    noteAdd(n, "Matches freezes %s:", hours);
    noteAdd(n, "likely the cause.");
    return;
  }
  if (e.scan_explains_loud) {
    evHoursText(e.loud, hours, sizeof(hours));
    noteAdd(n, "On channel in the loud hours");
    noteAdd(n, "(%s): likely the source.", hours);
    return;
  }
  if (e.freezes && !e.scan.blocker) {
    evHoursText(e.freeze, hours, sizeof(hours));
    hourAt(e.freeze, at, sizeof(at));
    if (e.scan_in_freeze) noteAdd(n, "Freezes %s; nothing on air now.", hours);
    else {
      noteAdd(n, "Ran %s; freezes are %s:", ran, hours);
      noteAdd(n, "scan at %s.", at);
    }
    return;
  }
  if (e.loud && !e.scan_in_loud) {
    evHoursText(e.loud, hours, sizeof(hours));
    hourAt(e.loud, at, sizeof(at));
    noteAdd(n, "Ran %s; loud hours are %s:", ran, hours);
    noteAdd(n, "scan at %s.", at);
  }
}

void relateLna(const DiagEvidence& e, DiagNote& n) {
  noteReset(n);
  if (!e.lna_available || !e.lna.known) return;
  char hours[20];
  if (e.lna_freezes_only_on) {
    noteAdd(n, "Freezes only with it on: a");
    noteAdd(n, "strong signal locks the receiver.");
  }
  int8_t shift = e.live_floor_shift ? e.live_floor_shift : e.lna.floor_shift;
  if (shift >= 20 && e.lna.err_tenths[1] >= 0 && e.lna.err_tenths[0] >= 0 && e.lna.err_tenths[1] >= 2 * e.lna.err_tenths[0] + 10) {
    noteAdd(n, "Floor +%d dB on; failed %d.%d%%", shift, e.lna.err_tenths[1] / 10, e.lna.err_tenths[1] % 10);
    noteAdd(n, "on vs %d.%d%% off: overload.", e.lna.err_tenths[0] / 10, e.lna.err_tenths[0] % 10);
  }
  if (e.lna.need_n) {
    char list[24] = "";
    for (int i = 0; i < e.lna.need_n && i < 2; i++) {
      char id[8];
      if (e.lna.need_len[i] >= 2) snprintf(id, sizeof(id), "%04X", e.lna.need_id[i]);
      else snprintf(id, sizeof(id), "%02X", e.lna.need_id[i] & 0xFF);
      size_t len = strlen(list);
      snprintf(list + len, sizeof(list) - len, "%s%s", len ? ", " : "", id);
    }
    noteAdd(n, "%s need%s it on.", list, e.lna.need_n == 1 ? "s" : "");
  }
  if (e.lna_off_better) {
    evHoursText(e.lna_off_better, hours, sizeof(hours));
    if (evInside(e.lna_off_better, e.loud)) {
      noteAdd(n, "Off better %s, the loud", hours);
      noteAdd(n, "hours: the source swamps it.");
    } else {
      noteAdd(n, "Off better %s: timed source?", hours);
    }
  }
  noteAdd(n, "On costs ~%d mA, ~%d mAh a day.", LNA_COST_UA / 1000, LNA_COST_UA * 24 / 1000);
}

void relateBatt(const DiagEvidence& e, const DiagStore& s, const BattView& v, DiagNote& n) {
  noteReset(n);
  bool falling = v.kind == BATT_K_DRAINING || v.kind == BATT_K_NET_LOSS || v.kind == BATT_K_NOT_RECOVERING;
  if (!falling) return;
  char hours[20];
  if ((e.has_temp && e.temp_now < 0) || (e.cold && !e.charging)) {
    evHoursText(e.cold, hours, sizeof(hours));
    if (e.has_temp) noteAdd(n, "CPU %d°C: charger pauses below 0.", e.temp_now);
    else noteAdd(n, "Below 0°C %s: no charge.", hours);
    noteAdd(n, "It resumes when it warms.");
    return;
  }
  int load = relateDayDrain(s, false);
  if (load) {
    noteAdd(n, "Busy days lose %d mV more:", load);
    noteAdd(n, "drain follows load, not panel.");
    return;
  }
  if (e.falling & (1UL << e.local_now) && v.hour_mv < 0) noteAdd(n, "Still falling: %d mV this hour.", v.hour_mv);
  // The LNA's share of the drain: with it on, the battery pays its cost every hour.
  if (e.lna_available && e.lna_on) noteAdd(n, "LNA on: ~%d mAh a day of it.", LNA_COST_UA * 24 / 1000);
}

int relateDayDrain(const DiagStore& s, bool by_cold) {
  uint32_t end = s.today.day != 0 ? s.today.day : s.last_day;
  if (end < STORE_DAYS) return 0;
  int32_t sum_a = 0, sum_b = 0, load_sum = 0;
  int n_a = 0, n_b = 0, n = 0;
  for (uint32_t day = end - (STORE_DAYS - 1); day < end; day++) {
    StoreDay d = storeDay(s, day);
    if (d.load_pct != STORE_NO_LOAD) {
      load_sum += d.load_pct;
      n++;
    }
  }
  int mean = n ? (int)(load_sum / n) : 0;
  for (uint32_t day = end - (STORE_DAYS - 1); day < end; day++) {
    StoreDay a = storeDay(s, day), b = storeDay(s, day + 1);
    if (a.morning == STORE_NO_MV || b.morning == STORE_NO_MV) continue;
    int delta = (int)storeUnpackMv(b.morning) - (int)storeUnpackMv(a.morning);
    bool hit, quiet;
    if (by_cold) {
      if (a.tmin == STORE_NO_TEMP) continue;
      hit = a.tmin < COLD_DAY_C;
      quiet = !hit;
    } else {
      if (a.load_pct == STORE_NO_LOAD) continue;
      hit = a.load_pct > mean + 2;
      quiet = a.load_pct <= mean;
    }
    if (hit) {
      sum_a += delta;
      n_a++;
    } else if (quiet) {
      sum_b += delta;
      n_b++;
    }
  }
  if (n_a < DRAIN_MIN_DAYS || n_b < DRAIN_MIN_DAYS) return 0;
  int worse = (int)(sum_b / n_b - sum_a / n_a);
  return worse >= DRAIN_WORSE_MV ? worse : 0;
}

void relateHist(const DiagStore& s, const HistView& v, DiagNote& n) {
  noteReset(n);
  if (v.kind == HIST_K_WATCHING) return;
  int cold = relateDayDrain(s, true), busy = relateDayDrain(s, false);
  if (cold && cold >= busy) noteAdd(n, "Worst on cold days: %d mV more.", cold);
  else if (busy) noteAdd(n, "Worst on busy days: %d mV more.", busy);
  if (v.kind == HIST_K_LOSING || v.kind == HIST_K_LOW) {
    if (v.days_to_low >= 0 && v.net_mv < -30) {
      char when[12];
      uint32_t today = s.today.day ? s.today.day : s.last_day;
      diagDateText(today + v.days_to_low, when, sizeof(when));
      noteAdd(n, "At this rate 3.35V around %s.", when);
    }
    if (cold && cold >= busy) noteAdd(n, "Cold stops the charge, not the panel.");
    else if (busy) noteAdd(n, "Cut the load or add panel.");
    else noteAdd(n, v.kind == HIST_K_LOW ? "Charge it now: it may stop soon." : "Check the panel: not keeping up.");
  }
}

void relateTemp(const DiagEvidence& e, const DiagStore& s, const TempView& v, DiagNote& n) {
  noteReset(n);
  if (e.heat_reboot >= 0) {
    const StoreReboot& r = s.reboot[e.heat_reboot];
    char when[12];
    diagDateText(storeLocalDay(r.at / 3600, e.tz), when, sizeof(when));
    noteAdd(n, "Restarted %s after %d°C:", when, r.temp_before);
    noteAdd(n, "heat. Shade or vent the box.");
  }
  // Cold days the battery did not recover from: the charger paused.
  int stuck = 0;
  uint32_t end = s.today.day != 0 ? s.today.day : s.last_day;
  for (uint32_t day = end >= STORE_DAYS ? end - (STORE_DAYS - 1) : 1; day < end; day++) {
    StoreDay a = storeDay(s, day), b = storeDay(s, day + 1);
    if (a.tmin == STORE_NO_TEMP || a.tmin >= 0 || a.morning == STORE_NO_MV || b.morning == STORE_NO_MV) continue;
    if (storeUnpackMv(b.morning) <= storeUnpackMv(a.morning)) stuck++;
  }
  if (stuck) {
    noteAdd(n, "%d cold day%s with no charge.", stuck, stuck == 1 ? "" : "s");
    if (v.kind == TEMP_K_COLD) noteAdd(n, "Charging resumes above 0°C.");
  }
}
