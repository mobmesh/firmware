#include <helpers/ModProbe.h>

#include <stdio.h>
#include <string.h>

static int hexValue(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

// One hash of exactly `width` bytes from text[0..len).
static bool parseHash(const char* text, size_t len, uint8_t width, uint8_t* out) {
  if (len != (size_t)width * 2) return false;
  for (uint8_t i = 0; i < width; i++) {
    int hi = hexValue(text[i * 2]), lo = hexValue(text[i * 2 + 1]);
    if (hi < 0 || lo < 0) return false;
    out[i] = (uint8_t)(hi << 4 | lo);
  }
  return true;
}

static size_t tokenLen(const char* s) {
  size_t n = 0;
  while (s[n] && s[n] != ' ' && s[n] != ',') n++;
  return n;
}

static void badHash(const char* text, size_t len, uint8_t width, char* err, size_t err_len) {
  snprintf(err, err_len, "ERR: %.*s is not a hash, use %u hex digits", (int)(len > 16 ? 16 : len), text, width * 2u);
}

ProbeParse probeParse(const char* command, uint8_t width, ProbeRequest* out, char* err, size_t err_len) {
  const char* name;
  if (strncmp(command, "ping", 4) == 0 && (command[4] == 0 || command[4] == ' ')) name = "ping";
  else if (strncmp(command, "trace", 5) == 0 && (command[5] == 0 || command[5] == ' ')) name = "trace";
  else return PROBE_NOT_OURS;
  memset(out, 0, sizeof(*out));
  out->trace = name[0] == 't';
  out->width = width;
  const char* p = command + strlen(name);
  while (*p == ' ') p++;
  if (*p == 0) {
    snprintf(err, err_len, "ERR: %s <hash> [via <hash>,...] or %s last", name, name);
    return PROBE_ERROR;
  }
  if (strncmp(p, "last", 4) == 0 && (p[4] == 0 || p[4] == ' ')) {
    out->last = true;
    return PROBE_OK;
  }
  uint8_t dest[PROBE_HASH_MAX];
  size_t len = tokenLen(p);
  if (!parseHash(p, len, width, dest)) {
    badHash(p, len, width, err, err_len);
    return PROBE_ERROR;
  }
  p += len;
  while (*p == ' ') p++;
  uint8_t via = 0;
  if (strncmp(p, "via", 3) == 0 && (p[3] == ' ' || p[3] == 0)) {
    p += 3;
    while (*p == ' ') p++;
    while (*p) {
      len = tokenLen(p);
      if (via >= PROBE_MAX_HOPS - 1) {
        snprintf(err, err_len, "ERR: path too long (max %d hops)", PROBE_MAX_HOPS);
        return PROBE_ERROR;
      }
      if (!parseHash(p, len, width, out->hash[via])) {
        badHash(p, len, width, err, err_len);
        return PROBE_ERROR;
      }
      if (memcmp(out->hash[via], dest, width) == 0) {
        char text[2 * PROBE_HASH_MAX + 1];
        probeHashText(dest, width, text);
        snprintf(err, err_len, "ERR: %s is the destination, leave it out of via", text);
        return PROBE_ERROR;
      }
      via++;
      p += len;
      while (*p == ' ' || *p == ',') p++;
    }
    if (via == 0) {
      snprintf(err, err_len, "ERR: %s <hash> via <hash>,<hash>...", name);
      return PROBE_ERROR;
    }
  } else if (*p) {
    snprintf(err, err_len, "ERR: %s <hash> [via <hash>,...] or %s last", name, name);
    return PROBE_ERROR;
  }
  memcpy(out->hash[via], dest, width);
  out->hops = via + 1;
  return PROBE_OK;
}

size_t probeTracePath(const ProbeRequest& r, uint8_t trace_width, uint8_t* out) {
  size_t n = 0;
  for (int i = 0; i < r.hops; i++, n += trace_width) memcpy(out + n, r.hash[i], trace_width);
  for (int i = r.hops - 2; i >= 0; i--, n += trace_width) memcpy(out + n, r.hash[i], trace_width);
  return n;
}

bool probeFillLinks(ProbeResult& r, const int8_t* forwarder_q, uint8_t forwarder_count, int8_t own_q) {
  int n = r.hops;
  if (forwarder_count != 2 * n - 1) return false;
  for (int i = 0; i < n; i++) r.out_q[i] = forwarder_q[i];
  // Return legs run from the destination back: forwarder entries n..2n-2, then the sender's own reading on link 0.
  for (int i = 0; i < n; i++) r.back_q[i] = i == 0 ? own_q : forwarder_q[2 * n - 1 - i];
  return true;
}

int8_t probeLowQ(const ProbeResult& r) {
  int8_t low = 127;
  for (int i = 0; i < r.hops; i++) {
    if (r.out_q[i] < low) low = r.out_q[i];
    if (r.back_q[i] < low) low = r.back_q[i];
  }
  return low;
}

void probeHashText(const uint8_t* hash, uint8_t width, char* out) {
  static const char digits[] = "0123456789abcdef";
  for (uint8_t i = 0; i < width; i++) {
    out[i * 2] = digits[hash[i] >> 4];
    out[i * 2 + 1] = digits[hash[i] & 15];
  }
  out[width * 2] = 0;
}

static void snrText(int8_t q, char* out, size_t len) {
  int v = q < 0 ? -q : q;
  snprintf(out, len, "%c%d.%02d", q < 0 ? '-' : '+', v / 4, (v % 4) * 25);
}

static void ageText(uint32_t age_s, char* out, size_t len) {
  if (age_s == PROBE_AGE_NONE) out[0] = 0;
  else if (age_s < 60) snprintf(out, len, " (%lus ago)", (unsigned long)age_s);
  else if (age_s < 3600) snprintf(out, len, " (%lum ago)", (unsigned long)(age_s / 60));
  else snprintf(out, len, " (%luh ago)", (unsigned long)(age_s / 3600));
}

#define ADD(...) do { if (n < max_lines) snprintf(lines[n++], PROBE_LINE, __VA_ARGS__); } while (0)

// Hop number (1-based) of the weakest link reading, first one on a tie.
static int lowHop(const ProbeResult& r) {
  int8_t low = probeLowQ(r);
  for (int i = 0; i < r.hops; i++) {
    if (r.out_q[i] == low || r.back_q[i] == low) return i + 1;
  }
  return r.hops;
}

// The weakest reading, named by the hop it sits on as trace lists it.
static void weakestText(const ProbeResult& r, char* out, size_t len) {
  char low[8], hop[2 * PROBE_HASH_MAX + 1];
  snrText(probeLowQ(r), low, sizeof(low));
  probeHashText(r.hash[lowHop(r) - 1], r.width, hop);
  snprintf(out, len, "weakest(%s)=%s", hop, low);
}

int probeFormat(const ProbeResult& r, bool trace, uint32_t age_s, char lines[][PROBE_LINE], int max_lines) {
  int n = 0;
  char dest[2 * PROBE_HASH_MAX + 1], age[16], weakest[32];
  probeHashText(r.hash[r.hops - 1], r.width, dest);
  ageText(age_s, age, sizeof(age));
  if (!trace) {
    if (!r.received) {
      ADD("Request timed out.%s", age);
      return n;
    }
    weakestText(r, weakest, sizeof(weakest));
    ADD("Reply %s: time=%lums hops=%u %s%s", dest, (unsigned long)r.rtt_ms, r.hops, weakest, age);
    return n;
  }
  const char* plural = r.hops == 1 ? "" : "s";
  if (!r.received) {
    ADD("Trace to %s over %u hop%s: no reply in %lus%s", dest, r.hops, plural,
        (unsigned long)((r.timeout_ms + 999) / 1000), age);
    return n;
  }
  ADD("Trace to %s over %u hop%s, %lums%s:", dest, r.hops, plural, (unsigned long)r.rtt_ms, age);
  // Numbered hops as tracert lists them; fixed-width fields keep the columns straight.
  for (int i = 0; i < r.hops; i++) {
    char hop[2 * PROBE_HASH_MAX + 1], out[8], back[8];
    probeHashText(r.hash[i], r.width, hop);
    snrText(r.out_q[i], out, sizeof(out));
    snrText(r.back_q[i], back, sizeof(back));
    ADD("  %d  %s  out %6s  back %6s", i + 1, hop, out, back);
  }
  return n;
}

static int packInto(char lines[][PROBE_LINE], int n, char msgs[][PROBE_MSG_MAX + 1]) {
  int m = 0;
  msgs[0][0] = 0;
  for (int i = 0; i < n; i++) {
    size_t have = strlen(msgs[m]), add = strlen(lines[i]);
    if (have > 0 && have + 1 + add > PROBE_MSG_MAX) {
      if (m + 1 >= PROBE_MSGS_MAX) return -1;
      msgs[++m][0] = 0;
      have = 0;
    }
    if (have > 0) strcat(msgs[m], "\n");
    strncat(msgs[m], lines[i], PROBE_MSG_MAX - strlen(msgs[m]));
  }
  return m + 1;
}

int probePack(const ProbeResult& r, bool trace, char lines[][PROBE_LINE], int n, char msgs[][PROBE_MSG_MAX + 1]) {
  int m = packInto(lines, n, msgs);
  if (m <= 1 || !trace || !r.received || n >= PROBE_LINES_MAX) return m;
  char weakest[32];
  weakestText(r, weakest, sizeof(weakest));
  snprintf(lines[n], PROBE_LINE, "Summary: time=%lums hops=%u %s", (unsigned long)r.rtt_ms, r.hops, weakest);
  return packInto(lines, n + 1, msgs);
}
