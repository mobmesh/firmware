#include <Arduino.h>
#include <stdlib.h>
#include <string.h>
#include <Mesh.h>
#include <helpers/ClientACL.h>
#include <helpers/CommonCLI.h>
#include <helpers/TxtDataHelpers.h>
#include <helpers/CoreIntegration.h>
#include <helpers/ModProbe.h>

// One TRACE in flight: sent from the CLI, matched in the receive observer, timed out in the loop. Results go to serial at once, or to the admin who asked as a late CLI reply.

#define PROBE_TIMEOUT_BASE_MS  2000UL
#define PROBE_TIMEOUT_LEG_MS   250UL   // queue and turnaround margin per transmission
#define PROBE_ROW_SPACING_MS   1500UL
#define PROBE_REPLY_DELAY_MS   600UL   // upstream's CLI_REPLY_DELAY_MILLIS

struct ProbePending {
  bool active;
  bool trace;
  bool remote;
  uint8_t trace_width;
  uint32_t tag;
  uint32_t sent_ms;
  uint32_t timeout_ms;
  uint32_t sender_timestamp;
  uint8_t admin_key[PUB_KEY_SIZE];
  char note[PROBE_LINE];               // remote only: sent with the result, so no reply floods while the trace is in flight
  ProbeRequest req;
};

static ProbePending pending;
static ProbeResult last_result;
static uint32_t last_done_ms;

static uint8_t typedWidth() {
  const ModBindings* b = modBindings();
  uint8_t mode = b && b->prefs ? b->prefs->path_hash_mode : 0;
  return mode >= PROBE_HASH_MAX ? PROBE_HASH_MAX : mode + 1;
}

// TRACE matches on a public-key prefix of 1, 2, 4 or 8 bytes; a 3-byte setting traces on the first two.
static uint8_t traceWidth(uint8_t typed) {
  return typed == 1 ? 1 : 2;
}

// The admin whose command is running: upstream records its timestamp on the client before dispatch.
static bool captureAdmin(uint32_t sender_timestamp, uint8_t key[PUB_KEY_SIZE]) {
  const ModBindings* b = modBindings();
  if (b == nullptr || b->acl == nullptr) return false;
  for (int i = 0; i < b->acl->getNumClients(); i++) {
    ClientInfo* c = b->acl->getClientByIdx(i);
    if (c->isAdmin() && c->last_timestamp == sender_timestamp) {
      memcpy(key, c->id.pub_key, PUB_KEY_SIZE);
      return true;
    }
  }
  return false;
}

static void sendLateReply(const uint8_t key[PUB_KEY_SIZE], uint32_t sender_timestamp, char msgs[][PROBE_MSG_MAX + 1], int count) {
  const ModBindings* b = modBindings();
  if (b == nullptr || b->acl == nullptr || b->mesh == nullptr) return;
  ClientInfo* c = b->acl->getClient(key, PUB_KEY_SIZE);
  if (c == nullptr) return;
  uint8_t hash_size = b->prefs ? b->prefs->path_hash_mode + 1 : 1;
  bool flood = c->out_path_len == OUT_PATH_UNKNOWN;
  uint32_t delay = PROBE_REPLY_DELAY_MS;
  for (int i = 0; i < count; i++) {
    uint8_t frame[5 + PROBE_MSG_MAX];
    size_t len = strlen(msgs[i]);
    uint32_t stamp = modCliStamp(sender_timestamp);
    memcpy(frame, &stamp, 4);
    frame[4] = TXT_TYPE_CLI_DATA << 2;
    memcpy(&frame[5], msgs[i], len);
    mesh::Packet* pkt = b->mesh->createDatagram(PAYLOAD_TYPE_TXT_MSG, c->id, c->shared_secret, frame, 5 + len);
    if (pkt == nullptr) return;
    if (flood) b->mesh->sendFlood(pkt, delay, hash_size);
    else b->mesh->sendDirect(pkt, c->out_path, c->out_path_len, delay);
    // A flooded frame is echoed by the first relay after up to 5 x airtime x f_txdelay; the next frame waits that echo out.
    uint32_t airtime = modAirtimeFor(5 + len + 16);
    float factor = b->prefs ? b->prefs->tx_delay_factor : 1.0f;
    delay += flood ? (uint32_t)(airtime * (2.0f + 5.0f * factor)) + 300 : PROBE_ROW_SPACING_MS;
  }
}

static void printSerial(char lines[][PROBE_LINE], int n) {
  for (int i = 0; i < n; i++) {
    Serial.print(i == 0 ? "  -> " : "");
    Serial.println(lines[i]);
  }
}

static void deliver(const ProbeResult& r, bool trace, bool remote, const uint8_t* key, uint32_t sender_timestamp, uint32_t age_s, const char* note) {
  // Borrowed only while a result is printed; the loop stack is too tight for 1.3 KB.
  char (*lines)[PROBE_LINE] = (char (*)[PROBE_LINE])malloc(PROBE_LINES_MAX * PROBE_LINE);
  char (*msgs)[PROBE_MSG_MAX + 1] = (char (*)[PROBE_MSG_MAX + 1])malloc(PROBE_MSGS_MAX * (PROBE_MSG_MAX + 1));
  if (lines == nullptr || msgs == nullptr) {
    free(lines);
    free(msgs);
    return;
  }
  int first = 0;
  if (note && note[0]) {
    strncpy(lines[0], note, PROBE_LINE - 1);
    lines[0][PROBE_LINE - 1] = 0;
    first = 1;
  }
  int n = first + probeFormat(r, trace, age_s, lines + first, PROBE_LINES_MAX - first);
  int m = probePack(r, trace, lines, n, msgs);
  if (!remote) {
    if (m > 1) n++;   // the summary line probePack added
    printSerial(lines, n);
  } else if (m > 0) {
    sendLateReply(key, sender_timestamp, msgs, m);
  }
  free(lines);
  free(msgs);
}

static void finish(bool received, const int8_t* forwarder_q, uint8_t count, int8_t own_q) {
  ProbeResult& r = last_result;
  memset(&r, 0, sizeof(r));
  r.valid = true;
  r.width = pending.req.width;
  r.hops = pending.req.hops;
  memcpy(r.hash, pending.req.hash, sizeof(r.hash));
  r.timeout_ms = pending.timeout_ms;
  r.rtt_ms = millis() - pending.sent_ms;
  r.received = received && probeFillLinks(r, forwarder_q, count, own_q);
  last_done_ms = millis();
  pending.active = false;
  deliver(r, pending.trace, pending.remote, pending.admin_key, pending.sender_timestamp, PROBE_AGE_NONE, pending.note);
}

// A note when the destination (or first via hop) is not in the neighbour table; room servers keep no table and stay silent.
static void neighbourNote(const ProbeRequest& r, char* reply) {
  char table[200];
  table[0] = 0;
  modCliDispatch(0, (char*)"neighbors", table);
  bool listed = strncmp(table, "-none-", 6) == 0;
  bool hex_first = (table[0] >= '0' && table[0] <= '9') || (table[0] >= 'A' && table[0] <= 'F');
  if (!listed && !hex_first) return;
  char want[2 * PROBE_HASH_MAX + 1];
  probeHashText(r.hash[0], r.width, want);
  for (char* p = want; *p; p++) if (*p >= 'a' && *p <= 'f') *p -= 32;
  size_t len = strlen(want);
  for (const char* p = table; (p = strstr(p, want)) != nullptr; p++) {
    if (p == table || p[-1] == '\n' || p[-1] == ' ' || p[-1] == '|') return;
  }
  for (char* p = want; *p; p++) if (*p >= 'A' && *p <= 'F') *p += 32;
  (void)len;
  if (r.hops == 1) sprintf(reply, "%s not in neighbours, trying direct", want);
  else sprintf(reply, "%s not in neighbours, trying anyway", want);
}

static void startProbe(const ModCliContext& context, const ProbeRequest& r, char* reply) {
  const ModBindings* b = modBindings();
  const char* name = r.trace ? "trace" : "ping";
  if (pending.active) {
    sprintf(reply, "ERR: %s already running", name);
    return;
  }
  if (b == nullptr || b->mesh == nullptr) {
    strcpy(reply, "ERR: mesh not ready");
    return;
  }
  uint8_t tw = traceWidth(r.width);
  uint32_t tag;
  b->mesh->getRNG()->random((uint8_t*)&tag, sizeof(tag));
  mesh::Packet* pkt = b->mesh->createTrace(tag, 0, tw == 1 ? 0 : 1);
  if (pkt == nullptr) {
    strcpy(reply, "ERR: no free packet, try again");
    return;
  }
  uint8_t path[2 * PROBE_MAX_HOPS * 2];
  size_t len = probeTracePath(r, tw, path);
  memset(&pending, 0, sizeof(pending));
  pending.trace = r.trace;
  pending.req = r;
  pending.trace_width = tw;
  pending.tag = tag;
  // Each of the 2N transmissions: airtime plus upstream's largest random direct retransmit delay (5 x airtime x factor).
  int legs = 2 * r.hops;
  int longest = 2 + 9 + (legs - 1) * tw + (legs - 1);
  float factor = b->prefs ? b->prefs->direct_tx_delay_factor : 1.0f;
  uint32_t per_leg = (uint32_t)(modAirtimeFor(longest) * (1.0f + 5.0f * factor)) + PROBE_TIMEOUT_LEG_MS;
  pending.timeout_ms = PROBE_TIMEOUT_BASE_MS + legs * per_leg;
  pending.sender_timestamp = context.sender_timestamp;
  pending.remote = context.sender_timestamp != 0 && captureAdmin(context.sender_timestamp, pending.admin_key);
  reply[0] = 0;
  neighbourNote(r, reply);
  if (pending.remote) {
    strncpy(pending.note, reply, PROBE_LINE - 1);
    reply[0] = 0;
  }
  pending.sent_ms = millis();
  pending.active = true;
  b->mesh->sendDirect(pkt, path, (uint8_t)len, 0);
}

bool coreProbeCli(const ModCliContext& context, char* command, char* reply) {
  ProbeRequest r;
  ProbeParse parsed = probeParse(command, typedWidth(), &r, reply, 160);
  if (parsed == PROBE_NOT_OURS) return false;
  if (parsed == PROBE_ERROR) return true;
  if (!r.last) {
    startProbe(context, r, reply);
    return true;
  }
  if (!last_result.valid) {
    sprintf(reply, "ERR: no %s result yet", r.trace ? "trace" : "ping");
    return true;
  }
  uint32_t age_s = (millis() - last_done_ms) / 1000;
  reply[0] = 0;
  uint8_t key[PUB_KEY_SIZE];
  bool remote = context.sender_timestamp != 0 && captureAdmin(context.sender_timestamp, key);
  if (context.sender_timestamp != 0 && !remote) return true;
  deliver(last_result, r.trace, remote, key, context.sender_timestamp, age_s, nullptr);
  return true;
}

void coreLoop() {
  if (!pending.active) return;
  if (millis() - pending.sent_ms >= pending.timeout_ms) finish(false, nullptr, 0, 0);
}

void coreRecv(const mesh::Packet* pkt, bool, const uint8_t*) {
  if (!pending.active || !pkt->isRouteDirect() || pkt->getPayloadType() != PAYLOAD_TYPE_TRACE) return;
  if (pkt->payload_len < 9) return;
  uint32_t tag;
  memcpy(&tag, pkt->payload, 4);
  if (tag != pending.tag) return;
  uint8_t path_sz = pkt->payload[8] & 0x03;
  uint16_t hashes_len = pkt->payload_len - 9;
  // Upstream's own end-of-path test: the path has run out, so this copy has come back to the sender.
  if (((uint16_t)pkt->path_len << path_sz) < hashes_len) return;
  finish(true, (const int8_t*)pkt->path, pkt->path_len, pkt->_snr);
}
