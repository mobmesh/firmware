#pragma once

#include <stddef.h>
#include <stdint.h>

// Pure LNA-study arithmetic: no radio, clock or storage, so the host tests compile it unchanged.

#define LNA_MAX_NEIGHBOURS 16
#define LNA_MAX_DAYS       7
#define LNA_OFF            0
#define LNA_ON             1

struct LnaBlock {
  uint16_t heard[LNA_MAX_NEIGHBOURS];
  int32_t snr_q[LNA_MAX_NEIGHBOURS];     // quarter-dB sums, as Packet::_snr stores them
  uint16_t snr_n[LNA_MAX_NEIGHBOURS];
  uint16_t unique;                       // all unique packets, attributed or not
  int16_t floor_dbm;                     // median idle RSSI over the block
  bool floor_valid;
};

struct LnaNeighbour {
  uint16_t id;
  uint8_t id_len;
  uint32_t heard[2];
  int32_t snr_q[2];
  uint32_t snr_n[2];
};

struct LnaStudy {
  uint8_t active;
  uint8_t start_lna;                     // live state when the study began, restored at the end
  uint8_t result;                        // LnaVerdict of the finished study
  uint8_t clock_valid;
  uint32_t requested_s;
  uint32_t elapsed_s;                    // active study time; pauses do not count
  uint16_t wins[2];
  uint16_t ties;
  uint32_t unique[2];
  int32_t floor_sum[2];
  uint16_t floor_n[2];
  uint8_t neighbour_count;
  LnaNeighbour nb[LNA_MAX_NEIGHBOURS];
  uint8_t hour_wins[LNA_MAX_DAYS][24][2];
};

enum LnaVerdict : uint8_t {
  LNA_V_NONE,
  LNA_V_NOT_WORKING,
  LNA_V_NOT_ENOUGH,
  LNA_V_KEEP_ON_LOST,
  LNA_V_OVERLOADED,
  LNA_V_HELPS,
  LNA_V_NO_BENEFIT,
};

struct LnaRun {
  bool found;
  uint8_t start_hour;                    // first UTC hour of the run
  uint8_t end_hour;                      // hour the run ends at (exclusive)
  uint8_t state;                         // the state that wins inside the run
};

int lnaNeighbourIndex(LnaStudy& s, uint16_t id, uint8_t id_len);   // -1 when the table is full
// 1 = on wins, -1 = off wins, 0 = tie or no traffic; per-neighbour weighted so one chatty node cannot decide.
int lnaPairWinner(const LnaBlock& on, const LnaBlock& off, uint8_t neighbours);
void lnaAddPair(LnaStudy& s, const LnaBlock& on, const LnaBlock& off, int hour, int day);
bool lnaSignificant(uint16_t wins, uint16_t decided);
float lnaFloorShift(const LnaStudy& s);  // on minus off, dB; NAN-free: 0 when either side is empty
uint8_t lnaFloorPairs(const LnaStudy& s);
float lnaSnrLimit(uint8_t sf);
bool lnaClarity(const LnaStudy& s, uint8_t sf, float* delta_db);   // false when no strong neighbour
int lnaLostNeighbour(const LnaStudy& s);  // index of a neighbour heard only with the LNA on, or -1
uint8_t lnaHeardNeighbours(const LnaStudy& s);
int lnaPercentMore(const LnaStudy& s, uint8_t state);   // how much more the state hears than the other
LnaVerdict lnaVerdict(const LnaStudy& s, uint8_t sf, bool final_result);
bool lnaShouldStop(const LnaStudy& s, uint8_t sf);
LnaRun lnaOppositeRun(const LnaStudy& s, uint8_t overall_state);
// Report text for a finished study, packed later by the caller; returns lines written.
int lnaFormatResult(const LnaStudy& s, uint8_t live, uint8_t sf, int8_t tz_hours, char lines[][48], int max_lines);
int lnaFormatProgress(const LnaStudy& s, uint8_t sf, char lines[][48], int max_lines);
bool lnaParseTz(const char* text, int8_t* hours);
