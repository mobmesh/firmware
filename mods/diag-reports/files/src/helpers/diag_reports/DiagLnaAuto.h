#pragma once

#include <stddef.h>
#include <stdint.h>

// LNA auto: the LNA's state per 2-hour local block, learned from paired ping tests. Pure; the integration runs the tests.

#define AUTO_BLOCKS        12
#define AUTO_LEARN_DAYS    7
#define AUTO_LEARN_EVERY_S 600         // a test every 10 minutes while learning
#define AUTO_DAILY_CAP     150         // tests a day (two pings each)
#define AUTO_MIN_DECIDED   8           // decided tests before a block may switch state
#define AUTO_RETIRE_TESTS  30
#define AUTO_RETIRE_PCT    3
#define AUTO_RECHECK_DAYS  90
#define AUTO_FEW_NB_DAYS   14
#define AUTO_DEAD_DB       10
#define AUTO_BUSY_PCT      25          // no test when the last hour's load is over this
#define AUTO_JUMP_DB       6           // a floor shift this far off the usual means noise changed mid-test
#define AUTO_AUDIT_HOURS   6           // hours in each state before the audit compares them
#define AUTO_BAN_DAYS      7           // a state the audit or the overload verdict caught stays banned this long
#define AUTO_REFRESH_DAYS  14          // a block running on runs off one day in this many
#define AUTO_TARGETS       3           // tests rotate among this many weakest judged neighbours
#define AUTO_STRIKES       2           // confirmed on-failures in a row (off answered before and after) that ban on
#define AUTO_MUTE_MISSES   3           // tests in a row unanswered in both states before a neighbour sits out
#define AUTO_MUTE_MS       86400000UL  // how long it sits out of the rotation
#define AUTO_MUTE_MAX      8

enum AutoReason : uint8_t {
  AUTO_R_NONE,
  AUTO_R_DECODES,                      // more failed decodes in that state
  AUTO_R_PACKETS,                      // fewer packets decoded
  AUTO_R_NEIGHBOURS,                   // fewer neighbours heard
  AUTO_R_FREEZES,                      // more receiver freezes
  AUTO_R_OVERLOAD,                     // floor raised beyond the LNA's usual shift, or the engine's overload verdict
  AUTO_R_ON_LOST,                      // with the LNA on, a neighbour off reaches went unanswered (confirmed twice)
};

// One state's record in one block, from closed hours: halved when it grows, so it follows recent weeks.
struct AutoStats {
  uint8_t hours, floor_hours;
  uint8_t freezes, pad;
  uint16_t recv, errors;
  uint16_t heard;                      // neighbours heard, summed over the hours
  int16_t floor_sum;                   // dBm, summed over floor_hours
};

enum AutoPark : uint8_t {
  AUTO_ACTIVE,
  AUTO_PARK_NO_BENEFIT,
  AUTO_PARK_DEAD,
  AUTO_PARK_FEW_NB,
};

struct AutoBlock {
  uint16_t on_w, off_w;                // wins for each state, tenths, faded daily
  uint8_t ties;
  uint8_t veto_off;                    // days left on a lost-neighbour veto against off
  uint8_t retired;
  uint8_t losses;                      // tests in a row lost by the applied state this block
  uint8_t strikes;                     // confirmed on-failures in a row; AUTO_STRIKES bans on
  uint8_t restart;                     // back to the fast test rate after a contradiction
  uint8_t ban_days;                    // days left on a ban of one state
  uint8_t ban_on;                      // the banned state: 1 on, 0 off
  uint8_t ban_reason;                  // AutoReason
  uint8_t refresh_days;                // days since the block last ran off
  uint8_t refresh;                     // running off today to refresh its record
  uint8_t best;                        // the state the tests last proved best: 1 on, 0 off; starts off
  uint8_t pad[2];
  AutoStats st[2];                     // index 0 = LNA off, 1 = on
};

struct LnaAutoState {
  uint8_t enabled;
  uint8_t spare;
  uint8_t park;                        // AutoPark
  uint8_t pad;
  uint32_t start_day;                  // local day auto was turned on (or re-entered learning)
  uint32_t park_day;
  uint32_t last_test;                  // epoch of the last test
  uint32_t test_day;
  uint16_t tests_today;
  uint16_t few_nb_days;                // consecutive days with fewer than 2 pingable neighbours
  int32_t floor_shift_sum;             // on minus off idle floor, summed over tests
  uint16_t floor_n;
  uint16_t gain_tests;                 // paired tests while auto ran on
  int16_t gain_replies;                // replies won minus lost by auto against fixed off
  uint16_t tests_total, faded, silent; // all tests, discarded for fading, neither ping answered
  uint16_t noisy, busy;                // tests discarded for a floor jump, skipped for a busy channel
  AutoBlock b[AUTO_BLOCKS];
};

// One paired test: the neighbour pinged in each LNA state.
struct AutoTest {
  bool reply[2];                       // index 0 = LNA off, 1 = on
  float back_db[2];                    // SNR of the reply at this node: the LNA affects it
  float out_db[2];                     // SNR of the ping at the neighbour: the LNA does not, so a mismatch means fading
  int8_t floor_dbm[2];                 // idle floor just before each ping
  int8_t confirm;                      // off re-ping after a failed on-ping: 1 answered, 0 not, -1 not tried
};

// Neighbours that answer neither ping sit out of the test rotation for a while. RAM only; millis() based.
struct AutoMute {
  char id[8];                          // lower-case hash label; empty = free
  uint8_t misses;                      // tests in a row unanswered in both states
  uint32_t until_ms;                   // sits out until this millis(); 0 = in the rotation
};

struct AutoMutes {
  AutoMute m[AUTO_MUTE_MAX];
};

// One finished test against its target: any reply clears the misses; the third miss in a row sits it out.
void autoMuteRecord(AutoMutes& t, const char* id, bool answered, uint32_t now_ms);
bool autoMuted(const AutoMutes& t, const char* id, uint32_t now_ms);

void autoInit(LnaAutoState& a, uint32_t day);
uint8_t autoBlockOf(uint8_t local_hour);
// The state the block runs in: its proven best; undecided blocks run off; a ban, retirement, a dead LNA or no benefit run off.
bool autoApplied(const LnaAutoState& a, uint8_t block, uint8_t local_hour = 0);
bool autoLearning(const LnaAutoState& a, uint32_t day, uint8_t block);
// Whether a test is due now for the block; respects the daily cap and the rate for the block. A nonzero phase (from the
// node's identity) offsets the first test and lengthens the spacing, so neighbouring nodes do not switch together.
bool autoTestDue(const LnaAutoState& a, uint32_t epoch, uint32_t day, uint8_t block, uint32_t phase_s = 0);
// Scores a finished test; returns false when it was discarded (fading, or neither ping answered).
bool autoAddTest(LnaAutoState& a, uint8_t block, const AutoTest& t, uint32_t epoch, uint32_t day);
// A freeze in a block while the LNA was on counts against on there.
void autoAddFreeze(LnaAutoState& a, uint8_t block);
// A closed hour of the block in one LNA state; floor STORE_NO_FLOOR (-128) when unknown.
void autoAddHour(LnaAutoState& a, uint8_t block, bool lna_on, uint32_t recv, uint32_t errors, uint8_t freezes, uint8_t heard, int8_t floor_dbm);
// Compares the state the block runs against the other on every observation; bans the running state when it is clearly worse.
uint8_t autoAudit(LnaAutoState& a, uint8_t block);
// Bans one state in the blocks of a local-hour mask (bit per hour), for the overload verdict and the like.
void autoBan(LnaAutoState& a, uint32_t local_hours, bool ban_on, uint8_t reason);
// Whether a test may run now: not over a busy channel.
bool autoChannelOk(LnaAutoState& a, uint8_t load_pct);
const char* autoReasonText(uint8_t reason);
// The first ban in force: "on 06-08 banned 7d: decodes worse"; false when none.
bool autoBanText(const LnaAutoState& a, char* out, size_t len);
// Once a day: fade the scores, retire settled blocks, check the park triggers and the 90-day recheck.
void autoDayTurn(LnaAutoState& a, uint32_t day, uint8_t pingable_neighbours);
// "auto: off now; on 12-18", "auto: on now (learning); off all day", "auto: off now (clock not set); ..."; local_hour and lna_now -1 when unknown.
void autoStatusText(const LnaAutoState& a, int local_hour, int lna_now, char* out, size_t len);
// One character per block, midnight first: + on, - off, . undecided (off), x retired.
void autoBlockRow(const LnaAutoState& a, char out[AUTO_BLOCKS + 1]);
