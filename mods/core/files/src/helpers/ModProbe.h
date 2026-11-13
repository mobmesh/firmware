#pragma once

#include <stddef.h>
#include <stdint.h>

// ping and trace text and arithmetic: parsing, the out-and-back TRACE path, per-link SNRs, report lines and reply packing. No radio, so host tests compile it unchanged.

#define PROBE_MAX_HOPS   6
#define PROBE_HASH_MAX   3             // widest path hash a node is set to type (hash_mode 2)
#define PROBE_LINE       96
#define PROBE_LINES_MAX  10            // summary or note, reply line, 6 links, spare
#define PROBE_MSG_MAX    155           // one CLI reply frame of text, as ModCliRows sends
#define PROBE_MSGS_MAX   3
#define PROBE_AGE_NONE   0xFFFFFFFFu

enum ProbeParse : int8_t { PROBE_NOT_OURS = 0, PROBE_OK = 1, PROBE_ERROR = -1 };

struct ProbeRequest {
  bool trace;                          // trace output rather than ping
  bool last;                           // reprint the stored result; nothing is sent
  uint8_t width;                       // bytes per typed hash
  uint8_t hops;                        // via hops + 1; hash[hops - 1] is the destination
  uint8_t hash[PROBE_MAX_HOPS][PROBE_HASH_MAX];
};

struct ProbeResult {
  bool valid;                          // a probe has finished since boot
  bool received;
  uint8_t width;
  uint8_t hops;
  uint8_t hash[PROBE_MAX_HOPS][PROBE_HASH_MAX];
  uint32_t rtt_ms;
  uint32_t timeout_ms;
  int8_t out_q[PROBE_MAX_HOPS];        // quarter-dB SNR, link i outbound (node i heard by node i+1; node 0 is us)
  int8_t back_q[PROBE_MAX_HOPS];       // quarter-dB SNR, link i on the way back
};

// "ping ..." or "trace ..."; err holds the reply on PROBE_ERROR.
ProbeParse probeParse(const char* command, uint8_t width, ProbeRequest* out, char* err, size_t err_len);
// The TRACE path, via hops then destination then via hops reversed, each cut to trace_width bytes; returns bytes written.
size_t probeTracePath(const ProbeRequest& r, uint8_t trace_width, uint8_t* out);
// Forwarder SNRs (2N-1, in travel order) and our own last-leg SNR, split into per-link out/back.
bool probeFillLinks(ProbeResult& r, const int8_t* forwarder_q, uint8_t forwarder_count, int8_t own_q);
int8_t probeLowQ(const ProbeResult& r);
void probeHashText(const uint8_t* hash, uint8_t width, char* out);
// Report lines for the result; age_s PROBE_AGE_NONE for a fresh result.
int probeFormat(const ProbeResult& r, bool trace, uint32_t age_s, char lines[][PROBE_LINE], int max_lines);
// Lines joined by newlines into reply frames; a trace that needs a second frame gains a Summary line.
int probePack(const ProbeResult& r, bool trace, char lines[][PROBE_LINE], int n, char msgs[][PROBE_MSG_MAX + 1]);
