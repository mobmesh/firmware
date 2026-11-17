#include <helpers/diag_reports/DiagLnaAuto.h>

#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define FADE_NUM        95             // daily fade 0.95: half-life about 14 days
#define FADE_DEN        100
#define FADING_DB       3.0f           // outbound SNRs further apart than this mean the path faded between pings
#define CLEARER_DB      1.0f
#define SIGMA           0.82f
#define VETO_DAYS       14

void autoInit(LnaAutoState& a, uint32_t day) {
  memset(&a, 0, sizeof(a));
  a.enabled = 1;
  a.start_day = day;
}

uint8_t autoBlockOf(uint8_t local_hour) {
  return (uint8_t)(local_hour % 24 / 2);
}

static bool significant(float wins, float decided) {
  return decided > 0 && wins >= decided / 2.0f + SIGMA * sqrtf(decided);
}

static float decided(const AutoBlock& b) {
  return (b.on_w + b.off_w) / 10.0f;
}

// The best state moves only on a significant score for the other one; otherwise the block keeps it.
static void settle(AutoBlock& b) {
  if (b.retired) {
    b.best = 0;
    return;
  }
  if (decided(b) < AUTO_MIN_DECIDED) return;
  float on = b.on_w / 10.0f, off = b.off_w / 10.0f, all = on + off;
  if (!b.best && significant(on, all)) b.best = 1;
  else if (b.best && significant(off, all) && !b.veto_off) b.best = 0;
}

// No proven best yet: nothing decided and no veto.
static bool undecided(const AutoBlock& b) {
  return !b.retired && !b.best && !b.veto_off && decided(b) < AUTO_MIN_DECIDED;
}

bool autoApplied(const LnaAutoState& a, uint8_t block, uint8_t local_hour) {
  if (!a.enabled || block >= AUTO_BLOCKS || a.park == AUTO_PARK_DEAD || a.park == AUTO_PARK_NO_BENEFIT) return false;
  const AutoBlock& b = a.b[block];
  // Alternating hours let neighbours heard only with the LNA on be found, and give the audit both records.
  bool run = undecided(b) ? (local_hour & 1) != 0 : b.best && !b.retired;
  // A banned state is never run.
  if (b.ban_days && run == (bool)b.ban_on) return !b.ban_on;
  if (b.refresh && run && !(b.ban_days && !b.ban_on)) return false;
  return run;
}

static void halve(AutoStats& st) {
  st.hours /= 2;
  st.floor_hours /= 2;
  st.freezes /= 2;
  st.recv /= 2;
  st.errors /= 2;
  st.heard /= 2;
  st.floor_sum /= 2;
}

void autoAddHour(LnaAutoState& a, uint8_t block, bool lna_on, uint32_t recv, uint32_t errors, uint8_t freezes, uint8_t heard, int8_t floor_dbm) {
  if (!a.enabled || block >= AUTO_BLOCKS) return;
  AutoStats& st = a.b[block].st[lna_on ? 1 : 0];
  if (st.hours >= 48 || st.recv > 60000 - recv || st.errors > 60000 - errors) halve(st);
  if (recv > 60000) recv = 60000;
  if (errors > 60000) errors = 60000;
  st.hours++;
  st.recv = (uint16_t)(st.recv + recv);
  st.errors = (uint16_t)(st.errors + errors);
  st.heard = (uint16_t)(st.heard + heard);
  if (st.freezes < 255 - freezes) st.freezes = (uint8_t)(st.freezes + freezes);
  if (floor_dbm != -128) {
    st.floor_hours++;
    st.floor_sum = (int16_t)(st.floor_sum + floor_dbm);
  }
}

static void ban(AutoBlock& b, bool ban_on, uint8_t reason) {
  b.ban_days = AUTO_BAN_DAYS;
  b.ban_on = ban_on ? 1 : 0;
  b.ban_reason = reason;
  b.best = ban_on ? 0 : 1;
  // The banned state starts its record again, and has to re-earn the block from the tests.
  memset(&b.st[ban_on ? 1 : 0], 0, sizeof(AutoStats));
  if (ban_on) b.on_w = 0;
  else b.off_w = 0;
}

uint8_t autoAudit(LnaAutoState& a, uint8_t block) {
  if (!a.enabled || a.park != AUTO_ACTIVE || block >= AUTO_BLOCKS) return AUTO_R_NONE;
  AutoBlock& b = a.b[block];
  // An undecided block runs both states: on, the riskier, is the one judged.
  bool run_on = undecided(b) ? !(b.ban_days && b.ban_on) : autoApplied(a, block);
  const AutoStats& r = b.st[run_on ? 1 : 0];
  const AutoStats& o = b.st[run_on ? 0 : 1];
  if (r.hours < AUTO_AUDIT_HOURS || o.hours < AUTO_AUDIT_HOURS) return AUTO_R_NONE;
  uint8_t why = AUTO_R_NONE;
  // Rates per hour in tenths, so the two records compare over different numbers of hours.
  uint32_t r_pkts = (uint32_t)r.recv * 10 / r.hours, o_pkts = (uint32_t)o.recv * 10 / o.hours;
  uint32_t r_all = (uint32_t)r.recv + r.errors, o_all = (uint32_t)o.recv + o.errors;
  if (r_all >= 50 && o_all >= 50) {
    uint32_t r_err = (uint32_t)r.errors * 1000 / r_all, o_err = (uint32_t)o.errors * 1000 / o_all;
    if (r_err > o_err + 20 && r_err * 2 > o_err * 3) why = AUTO_R_DECODES;
  }
  if (!why && o_pkts >= 50 && r_pkts * 10 < o_pkts * 7) why = AUTO_R_PACKETS;
  if (!why && (uint32_t)r.heard * 10 / r.hours + 10 < (uint32_t)o.heard * 10 / o.hours) why = AUTO_R_NEIGHBOURS;
  if (!why && r.freezes >= 2 && (uint32_t)r.freezes * o.hours > (uint32_t)o.freezes * r.hours * 2) why = AUTO_R_FREEZES;
  // The LNA lifts the floor by its usual shift; much more than that in the same hours is overload.
  if (!why && run_on && r.floor_hours >= AUTO_AUDIT_HOURS && o.floor_hours >= AUTO_AUDIT_HOURS && a.floor_n >= 5) {
    int usual = (int)(a.floor_shift_sum / a.floor_n);
    int shift = r.floor_sum / r.floor_hours - o.floor_sum / o.floor_hours;
    if (shift > usual + AUTO_JUMP_DB) why = AUTO_R_OVERLOAD;
  }
  if (why) ban(b, run_on, why);
  return why;
}

void autoBan(LnaAutoState& a, uint32_t local_hours, bool ban_on, uint8_t reason) {
  if (!a.enabled) return;
  for (int i = 0; i < AUTO_BLOCKS; i++) {
    if (local_hours & (3UL << (i * 2))) ban(a.b[i], ban_on, reason);
  }
}

bool autoChannelOk(LnaAutoState& a, uint8_t load_pct) {
  if (load_pct <= AUTO_BUSY_PCT) return true;
  if (a.busy < 0xFFFF) a.busy++;
  return false;
}

const char* autoReasonText(uint8_t reason) {
  switch (reason) {
    case AUTO_R_DECODES: return "decodes worse";
    case AUTO_R_PACKETS: return "fewer packets";
    case AUTO_R_NEIGHBOURS: return "neighbours lost";
    case AUTO_R_FREEZES: return "freezes";
    case AUTO_R_OVERLOAD: return "overload";
    default: return "";
  }
}

bool autoLearning(const LnaAutoState& a, uint32_t day, uint8_t block) {
  return day < a.start_day + AUTO_LEARN_DAYS || (block < AUTO_BLOCKS && a.b[block].restart);
}

bool autoTestDue(const LnaAutoState& a, uint32_t epoch, uint32_t day, uint8_t block) {
  if (!a.enabled || a.park != AUTO_ACTIVE || block >= AUTO_BLOCKS || epoch == 0) return false;
  if (a.b[block].retired) return false;
  if (a.test_day == day && a.tests_today >= AUTO_DAILY_CAP) return false;
  uint32_t every = autoLearning(a, day, block) ? AUTO_LEARN_EVERY_S : 7200;
  return a.last_test == 0 || epoch - a.last_test >= every;
}

bool autoAddTest(LnaAutoState& a, uint8_t block, const AutoTest& t, uint32_t epoch, uint32_t day) {
  if (block >= AUTO_BLOCKS) return false;
  if (a.test_day != day) {
    a.test_day = day;
    a.tests_today = 0;
  }
  a.tests_today++;
  if (a.tests_total < 0xFFFF) a.tests_total++;
  a.last_test = epoch;
  if (t.floor_dbm[0] != -128 && t.floor_dbm[1] != -128) {
    int shift = t.floor_dbm[1] - t.floor_dbm[0];
    // Noise that changed between the halves would score as the LNA's doing: the pair says nothing.
    if (a.floor_n >= 5 && abs(shift - (int)(a.floor_shift_sum / a.floor_n)) > AUTO_JUMP_DB) {
      if (a.noisy < 0xFFFF) a.noisy++;
      return false;
    }
    a.floor_shift_sum += shift;
    a.floor_n++;
  }
  if (!t.reply[0] && !t.reply[1]) {
    if (a.silent < 0xFFFF) a.silent++;
    return false;
  }
  // The neighbour's view of the ping does not depend on this node's LNA; if it moved, the path faded and the pair says nothing.
  if (t.reply[0] && t.reply[1] && fabsf(t.out_db[0] - t.out_db[1]) > FADING_DB) {
    if (a.faded < 0xFFFF) a.faded++;
    return false;
  }
  AutoBlock& b = a.b[block];
  int winner = 0;                      // 1 on, -1 off
  if (t.reply[1] && !t.reply[0]) {
    winner = 1;
    b.veto_off = VETO_DAYS;            // heard only with it on: off must not cost this neighbour
    b.best = 1;
  } else if (t.reply[0] && !t.reply[1]) {
    winner = -1;
  } else if (t.back_db[1] - t.back_db[0] >= CLEARER_DB) {
    winner = 1;
  } else if (t.back_db[0] - t.back_db[1] >= CLEARER_DB) {
    winner = -1;
  }
  if (winner == 0) {
    if (b.ties < 255) b.ties++;
    return true;
  }
  bool applied = autoApplied(a, block);
  if (winner > 0 && b.on_w < 60000) b.on_w += 10;
  if (winner < 0 && b.off_w < 60000) b.off_w += 10;
  // Against fixed off, running on either earned or cost a reply.
  if (applied) {
    a.gain_tests++;
    a.gain_replies += winner > 0 ? 1 : -1;
  }
  // Two losses in a row while on send the block to off and learning again; the old scores no longer hold.
  bool lost = (winner > 0) != applied;
  if (lost && applied && b.best && !b.veto_off) {
    if (++b.losses >= 2) {
      b.best = 0;
      b.on_w = b.off_w = 0;
      b.ties = 0;
      b.restart = 1;
      b.losses = 0;
    }
    return true;
  }
  if (!lost) b.losses = 0;
  settle(b);
  return true;
}

void autoAddFreeze(LnaAutoState& a, uint8_t block) {
  if (block < AUTO_BLOCKS && a.b[block].off_w < 60000) a.b[block].off_w += 10;
}

void autoDayTurn(LnaAutoState& a, uint32_t day, uint8_t pingable_neighbours) {
  if (!a.enabled) return;
  if (a.park != AUTO_ACTIVE) {
    // A parked auto rechecks once a season by learning again.
    if (day >= a.park_day + AUTO_RECHECK_DAYS) {
      autoInit(a, day);
    }
    return;
  }
  bool all_retired = true;
  for (int i = 0; i < AUTO_BLOCKS; i++) {
    AutoBlock& b = a.b[i];
    b.on_w = (uint16_t)(b.on_w * FADE_NUM / FADE_DEN);
    b.off_w = (uint16_t)(b.off_w * FADE_NUM / FADE_DEN);
    b.losses = 0;
    if (b.veto_off) b.veto_off--;
    if (b.ban_days && --b.ban_days == 0) b.ban_reason = AUTO_R_NONE;
    // A block running on runs off one day in AUTO_REFRESH_DAYS, so both records stay current.
    b.refresh = 0;
    if (b.best && !b.retired) {
      if (++b.refresh_days >= AUTO_REFRESH_DAYS) {
        b.refresh = 1;
        b.refresh_days = 0;
      }
    } else {
      b.refresh_days = 0;
    }
    float on = b.on_w / 10.0f, off = b.off_w / 10.0f, all = on + off + b.ties;
    if (b.restart && all >= AUTO_MIN_DECIDED && (significant(on, on + off) || significant(off, on + off))) b.restart = 0;
    if (day >= a.start_day + AUTO_LEARN_DAYS && all >= AUTO_RETIRE_TESTS && fabsf(on - off) * 100 < AUTO_RETIRE_PCT * all) b.retired = 1;
    settle(b);
    all_retired &= b.retired != 0;
  }
  a.few_nb_days = pingable_neighbours < 2 ? a.few_nb_days + 1 : 0;
  int shift = a.floor_n >= 5 ? (int)(a.floor_shift_sum / a.floor_n) : 99;
  uint8_t park = AUTO_ACTIVE;
  if (shift < AUTO_DEAD_DB) park = AUTO_PARK_DEAD;
  else if (a.few_nb_days >= AUTO_FEW_NB_DAYS) park = AUTO_PARK_FEW_NB;
  else if (all_retired) park = AUTO_PARK_NO_BENEFIT;
  if (park != AUTO_ACTIVE) {
    a.park = park;
    a.park_day = day;
  }
}

void autoBlockRow(const LnaAutoState& a, char out[AUTO_BLOCKS + 1]) {
  for (int i = 0; i < AUTO_BLOCKS; i++) {
    const AutoBlock& b = a.b[i];
    bool applied = autoApplied(a, (uint8_t)i);
    if (b.retired) out[i] = 'x';
    else if (applied) out[i] = '+';
    else out[i] = decided(b) >= AUTO_MIN_DECIDED ? '-' : '.';
  }
  out[AUTO_BLOCKS] = 0;
}

void autoStatusText(const LnaAutoState& a, int local_hour, int lna_now, char* out, size_t len) {
  switch (a.park) {
    case AUTO_PARK_DEAD: snprintf(out, len, "mode:auto parked (LNA not working)"); return;
    case AUTO_PARK_FEW_NB: snprintf(out, len, "mode:auto parked (too few neighbours)"); return;
    case AUTO_PARK_NO_BENEFIT: snprintf(out, len, "mode:auto now:off; parked (no benefit)"); return;
    default: break;
  }
  // The blocks running on, as local times; a run that crosses midnight is joined, so it starts after an off block.
  char spans[96] = "";
  int len_used = 0, on_blocks = 0, first_off = -1;
  for (int i = 0; i < AUTO_BLOCKS; i++) {
    if (autoApplied(a, (uint8_t)i)) on_blocks++;
    else if (first_off < 0) first_off = i;
  }
  for (int n = 0; first_off >= 0 && n < AUTO_BLOCKS;) {
    int i = (first_off + n) % AUTO_BLOCKS;
    if (!autoApplied(a, (uint8_t)i)) {
      n++;
      continue;
    }
    int run = 0;
    while (n + run < AUTO_BLOCKS && autoApplied(a, (uint8_t)((first_off + n + run) % AUTO_BLOCKS))) run++;
    // Blocks are already local hours: auto aligns them through the time zone when it picks the block.
    int from = i * 2, to = ((i + run) * 2) % 24;
    len_used += snprintf(spans + len_used, sizeof(spans) - len_used, "%s%02d:00-%02d:00", len_used ? ", " : "on ", from, to);
    n += run;
  }
  const char* plan = on_blocks == AUTO_BLOCKS ? "on all day" : spans[0] ? spans : "off all day";
  // The state now leads; without a clock auto holds the LNA off, and a learning block is marked.
  bool hour_known = local_hour >= 0 && local_hour < 24;
  bool learning = hour_known && undecided(a.b[autoBlockOf((uint8_t)local_hour)]);
  int state = lna_now >= 0 ? lna_now : hour_known ? (int)autoApplied(a, autoBlockOf((uint8_t)local_hour), (uint8_t)local_hour) : -1;
  if (state < 0) {
    snprintf(out, len, "mode:auto; %s", plan);
    return;
  }
  snprintf(out, len, "mode:auto now:%s%s; %s", state ? "on" : "off", !hour_known ? " (clock not set)" : learning ? " (learning)" : "", plan);
}

bool autoBanText(const LnaAutoState& a, char* out, size_t len) {
  out[0] = 0;
  if (!a.enabled) return false;
  for (int i = 0; i < AUTO_BLOCKS; i++) {
    const AutoBlock& b = a.b[i];
    if (!b.ban_days) continue;
    // Neighbouring blocks banned for the same state and reason read as one span.
    int j = i + 1;
    while (j < AUTO_BLOCKS && a.b[j].ban_days && a.b[j].ban_on == b.ban_on && a.b[j].ban_reason == b.ban_reason) j++;
    snprintf(out, len, "%s %02d-%02d banned %ud: %s", b.ban_on ? "on" : "off", i * 2, (j * 2) % 24, b.ban_days, autoReasonText(b.ban_reason));
    return true;
  }
  return false;
}
