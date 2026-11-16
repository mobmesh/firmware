#pragma once

#include <stddef.h>
#include <stdint.h>
#include <helpers/diag_reports/DiagLnaAuto.h>

// Everything diag keeps over time, in one structure saved as one file. Pure: the caller supplies clock, readings and storage.

#define STORE_HOURS     25             // 24 finished hours plus the one in progress
#define STORE_DAYS      30
#define STORE_MAGIC     0x31524744UL   // "DGR1"
#define STORE_NO_FLOOR  (-128)
#define STORE_NO_TEMP   (-128)
#define STORE_NO_MV     0xFF
#define STORE_NO_LOAD   0xFF
#define STORE_RECUR_DAYS 7
#define STORE_LNA_OFF   0
#define STORE_LNA_ON    1
#define STORE_LNA_NA    2              // board without a switchable LNA
#define STORE_FULL_HOURS 20            // a whole charge and drain cycle
#define STORE_NOON      12             // the morning low is the lowest reading before local noon

#define HOUR_COUNTS     0x01           // recv and errors are valid
#define HOUR_BATT       0x02           // battery fields are valid

struct StoreHour {
  uint32_t hour;                       // UTC epoch hour the slot holds; 0 = empty
  int8_t floor_dbm;                    // median idle RSSI over the hour, or STORE_NO_FLOOR
  uint8_t lna;                         // LNA state while sampled
  uint8_t flags;
  uint8_t freezes;                     // receiver freezes found in the hour
  uint16_t recv, errors;               // packets decoded and failed decodes
  uint16_t batt_lo, batt_hi, batt_first, batt_last;   // mV
  uint8_t guard_off;                   // times the guard bit was found cleared
  uint8_t load_pct;                    // airtime share of the hour, 0-100
  int8_t temp_max;                     // CPU die, whole °C, or STORE_NO_TEMP
  uint8_t strong;                      // AGC resets that met a strong signal on the channel
  uint8_t lora;                        // captures in the hour: bit 0 saw another network's LoRa, bit 1 decoded ours
  uint8_t pad;
};

struct StoreDay {
  uint8_t morning;                     // (mV - 2500) / 8, or STORE_NO_MV
  int8_t tmin, tmax;                   // CPU die, whole °C, or STORE_NO_TEMP
  uint8_t freezes;
  uint8_t load_pct;                    // mean airtime share over the day's hours, or STORE_NO_LOAD
};

struct StoreToday {
  uint32_t day;                        // local day number; 0 = not started
  uint16_t morning_mv;                 // 0 = none yet
  int8_t tmin, tmax;
  uint8_t freezes;
  uint8_t load_n;                      // hours folded into load_sum
  uint16_t load_sum;
};

// One finished day's leading cause and the local hours it showed in, for "seen N of 7 days".
struct StoreRecur {
  uint8_t cause;                       // engine cause id; 0 = none
  uint8_t hours[3];                    // 24-bit local-hour mask, low byte first
};

struct StoreBattDays {
  uint32_t day;                        // local day of day_mv; 0 = none
  uint32_t day_hour;                   // epoch hour of day_mv
  uint16_t day_mv;                     // first reading of that day
  uint16_t yesterday_mv;               // first reading of the last whole local day
  uint16_t before_mv;                  // first reading of the day before it
  uint8_t full_days;                   // whole days recorded in a row, up to 2
};

struct StoreGuard {
  uint16_t freezes, off;               // since the last clear
  uint16_t strong;                     // resets that met a strong signal, not a lock
  uint32_t last_strong;
  uint32_t last_freeze, last_off;      // epoch seconds; 0 = never
  uint8_t off_before, off_after;       // register 0x08B5 around the reset that restored it
};

struct StoreCapture {
  uint32_t at;                         // epoch seconds; 0 = none
  int16_t offset_khz;                  // scan finding: distance from the channel
  int8_t a, b, c;                      // scan: dB over floor, dBm; burst: min, max, median dBm
  uint8_t kind;
};

// A restart seen at boot: when it happened and what the node looked like just before.
struct StoreReboot {
  uint32_t at;                         // epoch of the first reading after the restart; 0 = clock unset
  uint32_t last_seen;                  // epoch of the last hourly save before it
  int8_t temp_before;                  // hottest CPU reading in the hour before, or STORE_NO_TEMP
  uint8_t planned;                     // a reboot the operator asked for
  uint16_t batt_before;                // mV, 0 = unknown
};

// The last interference scan, kept so its findings can be matched to the hours they ran in.
struct StoreScan {
  uint32_t at;                         // epoch; 0 = none
  uint8_t kind;                        // ScanKind
  uint8_t level;
  int16_t offset_khz;
  int8_t db, floor_dbm;
  uint8_t blocker;
  int16_t blocker_khz;
  int8_t blocker_db;
  uint8_t traffic;                     // TrafficKind on the channel
};

// The last live LNA floor check (diag probe lna).
struct StoreLnaCheck {
  uint32_t at;
  int8_t floor_on, floor_off;
  uint8_t pad[2];
};

#define STORE_REBOOTS 4
#define STORE_BASE_DAYS 7               // how many days a baseline averages over
#define STORE_NB_DAYS   14
#define STORE_NB_MAX    12
#define STORE_NO_SNR    (-128)
#define STORE_MASK_DAYS 14

// This site's usual for one local hour of the day, learned over the last week.
struct StoreBase {
  int8_t floor_dbm;                    // usual idle floor; STORE_NO_FLOOR until learned
  uint8_t spread;                      // usual hour-to-hour wobble, dB
  uint16_t err_tenths;                 // usual failed-decode rate, 0.1 %
  uint8_t load_pct;
  uint8_t n;                           // days folded in, up to STORE_BASE_DAYS
};

// One neighbour's daily SNR over two weeks, for spotting a link that is slowly getting worse.
struct StoreNbHistory {
  uint16_t id;
  uint8_t id_len;
  uint8_t pad;
  int8_t snr[STORE_NB_DAYS];           // daily SNR, whole dB, indexed by local day % 14; STORE_NO_SNR = not heard
};

#define STORE_RETRY_DAYS 7

// direct-retry's daily record, sampled hourly from its counters: packets that needed a resend and those then confirmed.
struct StoreRetry {
  uint32_t day[STORE_RETRY_DAYS];      // local day each entry holds, indexed by day % 7; 0 = none
  uint16_t retried[STORE_RETRY_DAYS], won[STORE_RETRY_DAYS];
};

struct DiagStore {
  StoreHour h[STORE_HOURS];            // indexed by epoch hour % STORE_HOURS
  StoreDay d[STORE_DAYS];              // indexed by local day % STORE_DAYS
  uint32_t last_day;                   // newest local day filed; 0 = none
  StoreToday today;
  StoreBattDays batt;
  StoreGuard guard;
  StoreCapture scan_capture, noise_capture, decode_capture;
  StoreRecur recur[STORE_RECUR_DAYS];  // indexed by local day % STORE_RECUR_DAYS
  uint32_t recur_day;                  // newest local day recorded; 0 = none
  uint8_t recur_n;                     // days the ring covers, up to STORE_RECUR_DAYS
  uint32_t cap_day;                    // local day the capture counts belong to
  uint8_t cap_used[3];                 // T1 scans, T2 and T3 bursts today
  uint32_t cap_last[3];                // epoch of each trigger's last capture
  StoreReboot reboot[STORE_REBOOTS];   // newest first
  uint32_t last_seen;                  // epoch of the last save, for the next boot's reboot record
  int8_t last_temp;                    // hottest CPU reading in the hour before the last save
  uint8_t reboot_planned;              // set by a reboot command, cleared at boot
  uint16_t last_mv;
  StoreScan scan;
  StoreLnaCheck lna_check;
  StoreBase base[24];                  // by local hour of day
  StoreNbHistory nb[STORE_NB_MAX];
  uint32_t nb_day;                     // newest local day in the neighbour history
  uint8_t loud_days[STORE_MASK_DAYS][3];   // each day's loud local hours, indexed by local day % 14
  uint32_t loud_day;                   // newest local day in loud_days
  LnaAutoState lna_auto;
  uint8_t auto_off;                    // automatic captures switched off
  StoreRetry retry;
};

void storeInit(DiagStore& s);
// The slot for an epoch hour, cleared first when it held another hour.
StoreHour& storeHourSlot(DiagStore& s, uint32_t hour);
// The slot holding exactly this hour, or nullptr.
const StoreHour* storeHourAt(const DiagStore& s, uint32_t hour);
void storeNoiseHour(DiagStore& s, uint32_t hour, int8_t floor_dbm, uint8_t lna, bool counts_valid, uint32_t recv, uint32_t errors);
// One timed reading: mv 0 = no battery, NAN = no temperature. Files the finished day when the local day turns.
void storeSample(DiagStore& s, uint32_t hour, int8_t tz_hours, uint16_t mv, float temp_c);
void storeFreeze(DiagStore& s, uint32_t epoch, int8_t tz_hours);
// Records a restart at boot from what the last save left behind.
void storeRebootSeen(DiagStore& s, uint32_t now_epoch);
// Airtime share of a finished hour, into the hour slot and today's mean.
void storeLoadHour(DiagStore& s, uint32_t hour, int8_t tz_hours, uint8_t load_pct);
// Records a finished local day's leading cause, clearing days skipped since the last record.
void storeRecurSet(DiagStore& s, uint32_t day, uint8_t cause, uint32_t hours);
// Days in the last 7 (ending at `day`) whose cause matches and whose hours overlap the given ones; `recorded` counts days with any record.
int storeRecurCount(const DiagStore& s, uint32_t day, uint8_t cause, uint32_t hours, int* recorded);
void storeGuardOff(DiagStore& s, uint32_t epoch, uint8_t before, uint8_t after);
// A reset that found the receiver busy with a strong signal rather than locked.
void storeStrong(DiagStore& s, uint32_t epoch);
void storeLoraSeen(DiagStore& s, uint32_t epoch, bool foreign, bool ours);   // what one capture found on the channel
// Folds a finished hour into its local hour's baseline; freeze hours and hours without a floor are left out.
void storeBaseHour(DiagStore& s, uint8_t local_hour, int8_t floor_dbm, bool counts, uint32_t recv, uint32_t errors, uint8_t load_pct);
// Records a finished day's SNR for a neighbour (whole dB), opening a slot for a new one or reusing the stalest.
void storeNbDay(DiagStore& s, uint32_t day, uint16_t id, uint8_t id_len, int8_t snr_db);
// Starts a new day in the neighbour history: every neighbour's slot for that day is cleared until recorded.
void storeNbTurn(DiagStore& s, uint32_t day);
// Records a finished day's loud local hours.
void storeLoudDay(DiagStore& s, uint32_t day, uint32_t loud_mask);
uint32_t storeLoudMask(const DiagStore& s, uint32_t day);   // 0 when the day is not recorded
void storeRetryAdd(DiagStore& s, uint32_t day, uint32_t retried, uint32_t won);
// Sums the days from back_from to back_to days before today (0 = today), inclusive.
void storeRetrySum(const DiagStore& s, uint32_t today, int back_from, int back_to, uint32_t* retried, uint32_t* won);
// One day's record from the ring, or today's running values.
StoreDay storeDay(const DiagStore& s, uint32_t day);
uint8_t storePackMv(uint16_t mv);
uint16_t storeUnpackMv(uint8_t v);
uint32_t storeLocalDay(uint32_t hour, int8_t tz_hours);
uint8_t storeLocalHour(uint32_t hour, int8_t tz_hours);

// Running median of whole-dBm readings without keeping the samples.
struct DbmHistogram {
  uint8_t bin[128];                    // index = -dBm, saturating
  uint16_t count;
};
void histoReset(DbmHistogram& g);
void histoAdd(DbmHistogram& g, int8_t dbm);
int8_t histoMedian(const DbmHistogram& g);
