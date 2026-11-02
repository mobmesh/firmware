#pragma once

#include <stddef.h>
#include <stdint.h>

// Pure link-report arithmetic and text: no radio, clock or storage, so the host tests compile it unchanged.

#define LINKS_MAX         16
#define LINKS_MIN_PKTS    5            // packets before a neighbour is judged
#define LINKS_STALE_MS    86400000UL   // neighbours silent this long drop out of the report
#define LINKS_LINE        48

struct LinkNeighbour {
  uint16_t id;
  uint8_t id_len;
  uint16_t packets;
  float snr_db;                        // running mean, then a slow average once it has 8 packets
  uint16_t echoes;                     // this node's relays it relayed straight after
  int32_t clock_off_s;                 // its advert time minus this clock, last zero-hop advert
  uint8_t clock_n;
  uint32_t last_ms;
};

struct LinkTable {
  LinkNeighbour nb[LINKS_MAX];
  uint8_t count;
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

float linkSnrLimit(uint8_t sf);
int linkIndex(LinkTable& t, uint16_t id, uint8_t id_len, uint32_t now_ms);   // finds, else takes the stalest slot
void linkAddPacket(LinkTable& t, int index, float snr_db, uint32_t now_ms);
void linkAddEcho(LinkTable& t, int index);
void linkAddClock(LinkTable& t, int index, int32_t offset_s);
LinkVerdict linkVerdict(const LinkTable& t, uint8_t sf, uint32_t now_ms);
// One-line forms for the diag summary; each returns 0 good, 1 caution, 2 problem, 3 not enough data.
int linkSummary(const LinkTable& t, uint8_t sf, uint32_t now_ms, char* out, size_t len);
int linkPathsSummary(const LinkTable& t, uint32_t now_ms, char* out, size_t len);
int linkClockSummary(const LinkTable& t, uint32_t now_ms, char* out, size_t len);
// Lowest-margin judged neighbour and its label; false when none is judged.
bool linkWeakest(const LinkTable& t, uint8_t sf, uint32_t now_ms, float* margin_db, char* id, size_t len);
int linkFormat(const LinkTable& t, uint8_t sf, uint32_t now_ms, char lines[][LINKS_LINE], int max_lines);
