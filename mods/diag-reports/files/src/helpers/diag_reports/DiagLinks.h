#pragma once

#include <helpers/diag_reports/DiagOut.h>

// Neighbour links from what the node hears: margins, echoes, hourly presence, clocks. Pure.

#define LINKS_MAX         16
#define LINKS_MIN_PKTS    5            // packets before a neighbour is judged
#define LINKS_STALE_MS    86400000UL   // neighbours silent this long drop out
#define LINKS_SOLID_DB    6.0f
#define LINKS_CLOSE_DB    3.0f

struct LinkNeighbour {
  uint16_t id;
  uint8_t id_len;
  uint8_t clock_n;
  uint16_t packets;
  uint16_t echoes;                     // this node's relays it relayed straight after
  float snr_db;                        // running mean, then a slow average once it has 8 packets
  int32_t clock_off_s;                 // its advert time minus this clock, last zero-hop advert
  uint32_t last_ms;
  uint32_t heard;                      // bit per UTC hour of day with a packet, last 24 h
  uint32_t weak;                       // bit per UTC hour of day with a packet under the close margin
  uint16_t lna_pkts[2];                // packets heard with the LNA off and on
};

struct LinkTable {
  LinkNeighbour nb[LINKS_MAX];
  uint8_t count;
  uint8_t hour_of_day;                 // UTC hour the masks were last rolled to; 0xFF = clock unset
  uint32_t echoes;                     // all attributed echoes, the evidence a silent neighbour is measured against
};

enum LinkVerdict : uint8_t {
  LINK_V_NOT_ENOUGH,
  LINK_V_MOST_WEAK,
  LINK_V_ONE_WAY,
  LINK_V_CLOSE,
  LINK_V_CAN_DROP,
  LINK_V_CLOCK,
  LINK_V_SOLID,
};

// What the engine adds to the report: a header that names the cause, and the hours a cause is active.
struct LinkContext {
  uint32_t loud_utc;                   // bit per UTC hour of day the channel was loud; 0 = none
  uint32_t watched_utc;                // bit per UTC hour of day with a noise reading; 0 = all
  const char* loud_text;               // "8am-5pm"
  bool lna_available;
};

float linkSnrLimit(uint8_t sf);
void linkInit(LinkTable& t);
// Finds the neighbour, folding a 1-byte id into the one 2-byte neighbour it starts; else takes a free or the stalest slot.
int linkIndex(LinkTable& t, uint16_t id, uint8_t id_len, uint32_t now_ms);
// `lna` is 0 off, 1 on, anything else unknown; `sf` sets the margin for the weak-hour mask.
void linkAddPacket(LinkTable& t, int index, float snr_db, uint8_t sf, uint8_t lna, uint32_t now_ms);
void linkAddEcho(LinkTable& t, int index);
void linkAddClock(LinkTable& t, int index, int32_t offset_s);
// Called when the UTC hour of day changes: the new hour's bits start clear. 0xFF while the clock is unset.
void linkRollHour(LinkTable& t, uint8_t hour_of_day);
// The local clock jumped by `delta_s`: stored offsets move with it.
void linkClockShift(LinkTable& t, int32_t delta_s);
LinkVerdict linkVerdict(const LinkTable& t, uint8_t sf, uint32_t now_ms);
int linkOneWay(const LinkTable& t, uint8_t sf, uint32_t now_ms);          // index, or -1
// Index of a judged neighbour heard in the other watched hours but silent or weak in the masked ones, or -1; watched 0 = all day.
int linkDropsIn(const LinkTable& t, uint32_t mask_utc, uint32_t watched_utc, uint32_t now_ms);
// Index of a neighbour heard only with the LNA on, or -1.
int linkLnaOnly(const LinkTable& t, uint32_t now_ms);
// Share of fresh packets through the busiest neighbour, 0-100; its index in `top`; -1 with too few packets.
int linkTopShare(const LinkTable& t, uint32_t now_ms, int* top, int* neighbours);
float linkMargin(const LinkTable& t, int index, uint8_t sf);
void linkLabel(const LinkTable& t, int index, char* out, size_t len);
// One-line forms for the diag screen; each returns 0 good, 1 caution, 2 problem, 3 not enough data.
int linkSummary(const LinkTable& t, uint8_t sf, uint32_t now_ms, char* out, size_t len);
int linkPathsSummary(const LinkTable& t, uint32_t now_ms, char* out, size_t len);
int linkClockSummary(const LinkTable& t, uint32_t now_ms, char* out, size_t len);
// Lowest-margin judged neighbour and its label; false when none is judged.
bool linkWeakest(const LinkTable& t, uint8_t sf, uint32_t now_ms, float* margin_db, char* id, size_t len);
void linkFormat(const LinkTable& t, uint8_t sf, uint32_t now_ms, const LinkContext* context, const DiagNote* note, DiagOut& o);

struct LinkRetryHop {
  uint16_t id;
  uint8_t id_len;
  uint16_t retried, won;
};
#define LINK_RETRY_HOPS 8
#define LINK_RETRY_MIN  5              // resends to a hop before its record counts as evidence

// The judged neighbour whose resends mostly go unconfirmed (under half of LINK_RETRY_MIN or more), worst first, or -1.
int linkRetryOneWay(const LinkTable& t, const LinkRetryHop* hops, int n, uint32_t now_ms, int* hop_index);
