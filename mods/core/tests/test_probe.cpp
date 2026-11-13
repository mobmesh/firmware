#include <helpers/ModProbe.h>

#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

static bool any(char lines[][PROBE_LINE], int n, const char* text) {
  for (int i = 0; i < n; i++) if (strstr(lines[i], text)) return true;
  return false;
}

static ProbeResult made(const ProbeRequest& r, bool received) {
  ProbeResult x;
  memset(&x, 0, sizeof(x));
  x.valid = true;
  x.received = received;
  x.width = r.width;
  x.hops = r.hops;
  memcpy(x.hash, r.hash, sizeof(x.hash));
  return x;
}

int main() {
  ProbeRequest r;
  char err[96];
  static char lines[PROBE_LINES_MAX][PROBE_LINE];
  static char msgs[PROBE_MSGS_MAX][PROBE_MSG_MAX + 1];

  // Not ours, usage, last.
  CHECK(probeParse("pingx c5fb", 2, &r, err, sizeof(err)) == PROBE_NOT_OURS);
  CHECK(probeParse("tracert c5fb", 2, &r, err, sizeof(err)) == PROBE_NOT_OURS);
  CHECK(probeParse("ping", 2, &r, err, sizeof(err)) == PROBE_ERROR && strstr(err, "ping <hash>"));
  CHECK(probeParse("trace last", 2, &r, err, sizeof(err)) == PROBE_OK && r.last && r.trace);

  // Hash width is the node's: 4 hex digits at 2 bytes.
  CHECK(probeParse("ping c5", 2, &r, err, sizeof(err)) == PROBE_ERROR && strcmp(err, "ERR: c5 is not a hash, use 4 hex digits") == 0);
  CHECK(probeParse("ping c5fg", 2, &r, err, sizeof(err)) == PROBE_ERROR);
  CHECK(probeParse("ping C5FB", 2, &r, err, sizeof(err)) == PROBE_OK && r.hops == 1 && r.hash[0][0] == 0xC5 && r.hash[0][1] == 0xFB);
  CHECK(probeParse("ping c5", 1, &r, err, sizeof(err)) == PROBE_OK);

  // via: hops in order, destination last; out-and-back path.
  CHECK(probeParse("trace c5fb via a1b2,a2c4", 2, &r, err, sizeof(err)) == PROBE_OK);
  CHECK(r.trace && r.hops == 3 && r.hash[0][0] == 0xA1 && r.hash[1][0] == 0xA2 && r.hash[2][0] == 0xC5);
  uint8_t path[64];
  size_t len = probeTracePath(r, 2, path);
  const uint8_t want[] = {0xA1, 0xB2, 0xA2, 0xC4, 0xC5, 0xFB, 0xA2, 0xC4, 0xA1, 0xB2};
  CHECK(len == sizeof(want) && memcmp(path, want, len) == 0);
  CHECK(probeTracePath(r, 1, path) == 5 && path[4] == 0xA1);
  CHECK(probeParse("ping c5fb via a1b2, c5fb", 2, &r, err, sizeof(err)) == PROBE_ERROR && strstr(err, "c5fb is the destination"));
  CHECK(probeParse("ping c5fb via", 2, &r, err, sizeof(err)) == PROBE_ERROR);
  CHECK(probeParse("ping c5fb a1b2", 2, &r, err, sizeof(err)) == PROBE_ERROR);
  CHECK(probeParse("ping c5fb via 0001,0002,0003,0004,0005", 2, &r, err, sizeof(err)) == PROBE_OK && r.hops == 6);
  CHECK(probeParse("ping c5fb via 0001,0002,0003,0004,0005,0006", 2, &r, err, sizeof(err)) == PROBE_ERROR && strstr(err, "max 6 hops"));

  // Neighbour ping: one forwarder SNR (the destination hearing us), ours for the way back.
  probeParse("ping c5fb", 2, &r, err, sizeof(err));
  ProbeResult x = made(r, true);
  const int8_t one[] = {29};
  CHECK(probeFillLinks(x, one, 1, 26));
  x.rtt_ms = 412;
  int n = probeFormat(x, false, PROBE_AGE_NONE, lines, PROBE_LINES_MAX);
  CHECK(n == 1 && strcmp(lines[0], "Reply from c5fb: time=412ms hops=1 weakest=+6.50 at hop 1") == 0);
  n = probeFormat(x, true, PROBE_AGE_NONE, lines, PROBE_LINES_MAX);
  CHECK(n == 2 && strcmp(lines[0], "Trace to c5fb over 1 hop, 412ms:") == 0);
  CHECK(strcmp(lines[1], "  1  c5fb  out  +7.25  back  +6.50") == 0);
  CHECK(!probeFillLinks(x, one, 2, 26));

  // Three hops: out legs then return legs, ours last; snr(lo) is the lowest either way.
  probeParse("trace c5fb via a1b2,a2c4", 2, &r, err, sizeof(err));
  x = made(r, true);
  const int8_t five[] = {37, 20, 14, 16, 22};
  CHECK(probeFillLinks(x, five, 5, 35));
  CHECK(x.out_q[0] == 37 && x.out_q[2] == 14 && x.back_q[2] == 16 && x.back_q[1] == 22 && x.back_q[0] == 35);
  CHECK(probeLowQ(x) == 14);
  x.rtt_ms = 2105;
  n = probeFormat(x, true, PROBE_AGE_NONE, lines, PROBE_LINES_MAX);
  CHECK(n == 4 && strcmp(lines[0], "Trace to c5fb over 3 hops, 2105ms:") == 0);
  CHECK(strcmp(lines[1], "  1  a1b2  out  +9.25  back  +8.75") == 0);
  CHECK(strcmp(lines[3], "  3  c5fb  out  +3.50  back  +4.00") == 0);
  CHECK(probePack(x, true, lines, n, msgs) == 1);
  n = probeFormat(x, false, 125, lines, PROBE_LINES_MAX);
  CHECK(strcmp(lines[0], "Reply from c5fb: time=2105ms hops=3 weakest=+3.50 at hop 3 (2m ago)") == 0);
  CHECK(x.out_q[0] == 37);

  // At 1-byte hashes "me" already fills the width.
  CHECK(probeParse("trace c5", 1, &r, err, sizeof(err)) == PROBE_OK);
  x = made(r, true);
  probeFillLinks(x, one, 1, 26);
  n = probeFormat(x, true, PROBE_AGE_NONE, lines, PROBE_LINES_MAX);
  CHECK(strcmp(lines[1], "  1  c5  out  +7.25  back  +6.50") == 0);

  // The weakest hop is where the lowest reading sits, out or back; the first wins a tie.
  probeParse("ping c5fb via a1b2,a2c4", 2, &r, err, sizeof(err));
  x = made(r, true);
  const int8_t back_low[] = {37, 30, 30, 30, 9};   // entry 4 is hop 2 on the way back
  probeFillLinks(x, back_low, 5, 35);
  CHECK(probeLowQ(x) == 9);
  n = probeFormat(x, false, PROBE_AGE_NONE, lines, PROBE_LINES_MAX);
  CHECK(any(lines, n, "weakest=+2.25 at hop 2"));

  // Negative SNR prints with its sign; quarter steps are exact.
  probeParse("ping c5fb", 2, &r, err, sizeof(err));
  x = made(r, true);
  const int8_t weak[] = {-30};
  probeFillLinks(x, weak, 1, -1);
  n = probeFormat(x, false, PROBE_AGE_NONE, lines, PROBE_LINES_MAX);
  CHECK(any(lines, n, "weakest=-7.50 at hop 1"));
  n = probeFormat(x, true, PROBE_AGE_NONE, lines, PROBE_LINES_MAX);
  CHECK(any(lines, n, "out  -7.50  back  -0.25"));

  // Six hops: two frames, the second ends with the summary; nothing exceeds a frame.
  probeParse("trace c5fb via a1b2,a2c4,b7e1,d4a9,e2f0", 2, &r, err, sizeof(err));
  x = made(r, true);
  const int8_t eleven[] = {37, 20, 25, 19, 28, 14, 16, 27, 21, 22, 22};
  CHECK(probeFillLinks(x, eleven, 11, 35));
  x.rtt_ms = 4210;
  n = probeFormat(x, true, PROBE_AGE_NONE, lines, PROBE_LINES_MAX);
  CHECK(n == 7);
  int m = probePack(x, true, lines, n, msgs);
  CHECK(m == 2);
  CHECK(strstr(msgs[1], "Summary: time=4210ms hops=6 weakest=+3.50 at hop 6") != NULL);
  CHECK(strstr(msgs[0], "Summary") == NULL);
  for (int i = 0; i < m; i++) CHECK(strlen(msgs[i]) <= PROBE_MSG_MAX);

  // Lost probes, fresh and reprinted.
  x.received = false;
  x.timeout_ms = 10000;
  n = probeFormat(x, false, PROBE_AGE_NONE, lines, PROBE_LINES_MAX);
  CHECK(n == 1 && strcmp(lines[0], "Request timed out.") == 0);
  n = probeFormat(x, true, 75, lines, PROBE_LINES_MAX);
  CHECK(n == 1 && strcmp(lines[0], "Trace to c5fb over 6 hops: no reply in 10s (1m ago)") == 0);
  CHECK(probePack(x, true, lines, n, msgs) == 1);

  if (failures == 0) printf("probe: all checks passed\n");
  return failures == 0 ? 0 : 1;
}
