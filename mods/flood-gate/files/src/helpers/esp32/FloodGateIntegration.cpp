#include <helpers/esp32/FloodGateIntegration.h>

#include <Arduino.h>
#include <Packet.h>
#include <SPIFFS.h>
#include <Utils.h>

#define SINKHOLE_MAX         16
#define SINKHOLE_DEF_DEPTH   3        // path positions checked, counted from the origin
#define SINKHOLE_MAX_DEPTH   63       // the most hashes a path can carry
#define SINKHOLE_DEF_HOURS   24
#define SINKHOLE_MAX_HOURS   168
#define SINKHOLE_PATH        "/sinkhole"
#define SINKHOLE_MAGIC       0x534B4834UL   // "SKH4"
#define SINKHOLE_SAVE_MS     600000UL       // remaining time on flash is at most this stale
#define SINKHOLE_REPLY_MAX   160            // the caller's reply buffer, serial and mesh alike

enum : uint8_t { SCOPE_CHAN = 1, SCOPE_CHAN_ONE = 2 };

struct SinkholeEntry {
  uint8_t  active;
  uint8_t  hash[3];
  uint8_t  hash_len;    // 0 for *, any repeater
  uint8_t  scope;
  uint8_t  channel;     // channel hash, SCOPE_CHAN_ONE only
  uint8_t  depth;       // hops checked from the origin, 0 for any
  uint8_t  minbytes;    // floods at least this wide pass, 0 for off
  uint8_t  watch;       // count only, never drop
  char     label[14];   // the channel as typed, for list
  uint32_t left_ms;
};

// Counters live in RAM only: they reset on reboot and when a slot is re-added.
struct SinkholeCounts {
  uint32_t match;
  uint32_t hop[4];      // first match at hop 1, 2, 3, then 4 or later
  uint32_t width[3];    // path hash width of the matched flood, 1 to 3 bytes
  uint32_t last_ms;
};

static SinkholeEntry entries[SINKHOLE_MAX];
static SinkholeCounts counts[SINKHOLE_MAX];
static bool booted = false;
static bool mounted = false;
static bool dirty = false;
static uint32_t last_tick_ms = 0;
static uint32_t last_save_ms = 0;
static uint32_t stat_drops = 0;
static uint32_t stat_minbytes = 0;
static uint32_t stat_watched = 0;
static uint8_t active_count = 0;   // lets the forward hook return at once when no entry is live

// Mounted here rather than assumed: this mod does not require hotspot-ota, which otherwise calls SPIFFS.begin() first.
static bool ensureMounted() {
  if (!mounted) mounted = SPIFFS.begin(true);
  return mounted;
}

static void recount() {
  active_count = 0;
  for (auto& e : entries) active_count += e.active != 0;
}

static void load() {
  memset(entries, 0, sizeof(entries));
  memset(counts, 0, sizeof(counts));
  if (!ensureMounted() || !SPIFFS.exists(SINKHOLE_PATH)) return;
  File f = SPIFFS.open(SINKHOLE_PATH, "r");
  if (!f) return;
  uint32_t magic = 0;
  bool ok = f.read((uint8_t*)&magic, sizeof(magic)) == sizeof(magic) && magic == SINKHOLE_MAGIC
            && f.read((uint8_t*)entries, sizeof(entries)) == sizeof(entries);
  f.close();
  if (!ok) memset(entries, 0, sizeof(entries));
  for (auto& e : entries) {
    e.label[sizeof(e.label) - 1] = 0;
    if (e.hash_len > sizeof(e.hash) || e.left_ms == 0 || (e.scope != SCOPE_CHAN && e.scope != SCOPE_CHAN_ONE)) e.active = 0;
  }
  recount();
}

// Every change to the table passes through here, so the count stays current.
static void save() {
  recount();
  dirty = false;
  last_save_ms = millis();
  if (!ensureMounted()) return;
  bool any = false;
  for (auto& e : entries) any |= e.active != 0;
  if (!any) {
    if (SPIFFS.exists(SINKHOLE_PATH)) SPIFFS.remove(SINKHOLE_PATH);
    return;
  }
  File f = SPIFFS.open(SINKHOLE_PATH, "w", true);
  if (!f) return;
  uint32_t magic = SINKHOLE_MAGIC;
  f.write((const uint8_t*)&magic, sizeof(magic));
  f.write((const uint8_t*)entries, sizeof(entries));
  f.close();
}

// Expiry runs on uptime: many repeaters boot with no valid clock, and time spent powered off does not count.
static void tick() {
  uint32_t now = millis();
  uint32_t elapsed = now - last_tick_ms;
  last_tick_ms = now;
  bool any = false;
  for (auto& e : entries) {
    if (!e.active) continue;
    if (e.left_ms <= elapsed) {
      e.active = 0;
      dirty = true;
      continue;
    }
    e.left_ms -= elapsed;
    any = true;
  }
  if (dirty || (any && now - last_save_ms >= SINKHOLE_SAVE_MS)) save();
}

void floodGateLoop() {
  if (!booted) {
    booted = true;
    load();
    last_tick_ms = last_save_ms = millis();
    return;
  }
  tick();
}

#define NO_MATCH  -1
#define ANY_HOP   -2   // a * entry, which has no path position

// The 0-based path position of the first match, or NO_MATCH.
static int matchAt(const SinkholeEntry& e, const mesh::Packet* packet, uint8_t size, uint8_t count) {
  if (!e.hash_len) return ANY_HOP;
  uint8_t hops = e.depth && e.depth < count ? e.depth : count;
  // An entry shorter than the packet's hash width matches on its own bytes; a longer one on the packet's.
  uint8_t n = e.hash_len < size ? e.hash_len : size;
  for (uint8_t h = 0; h < hops; h++) {
    if (memcmp(&packet->path[h * size], e.hash, n) == 0) return h;
  }
  return NO_MATCH;
}

// Channel floods only. Every matching entry counts a hit, so watch entries see floods an enforcing entry drops.
bool floodGateAllowFlood(const mesh::Packet* packet, bool scope_known) {
  (void)scope_known;
  if (!active_count) return true;
  uint8_t type = packet->getPayloadType();
  if (type != PAYLOAD_TYPE_GRP_TXT && type != PAYLOAD_TYPE_GRP_DATA) return true;
  uint8_t size = packet->getPathHashSize();
  uint8_t count = packet->getPathHashCount();
  bool allow = true, by_width = false;
  for (int i = 0; i < SINKHOLE_MAX; i++) {
    SinkholeEntry& e = entries[i];
    if (!e.active) continue;
    if (e.scope == SCOPE_CHAN_ONE && (packet->payload_len < 1 || packet->payload[0] != e.channel)) continue;
    if (e.minbytes && size >= e.minbytes) continue;
    int at = matchAt(e, packet, size, count);
    if (at == NO_MATCH) continue;
    SinkholeCounts& c = counts[i];
    c.match++;
    if (at != ANY_HOP) c.hop[at < 3 ? at : 3]++;
    c.width[size - 1]++;
    c.last_ms = millis();
    if (e.watch) {
      stat_watched++;
      continue;
    }
    if (allow) by_width = e.minbytes != 0;
    allow = false;
  }
  if (!allow) {
    stat_drops++;
    if (by_width) stat_minbytes++;
  }
  return allow;
}

static int hexNibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

// * for any repeater, or 2 to 6 hex digits in whole bytes.
static bool parseHash(const char* s, SinkholeEntry& e) {
  if (strcmp(s, "*") == 0) {
    e.hash_len = 0;
    return true;
  }
  size_t n = strlen(s);
  if (n < 2 || n > 6 || n % 2) return false;
  for (size_t i = 0; i < n; i++) if (hexNibble(s[i]) < 0) return false;
  e.hash_len = n / 2;
  for (uint8_t i = 0; i < e.hash_len; i++) e.hash[i] = (hexNibble(s[2 * i]) << 4) | hexNibble(s[2 * i + 1]);
  return true;
}

static uint8_t channelHashOf(const uint8_t secret[16]) {
  uint8_t h;
  mesh::Utils::sha256(&h, 1, secret, 16);
  return h;
}

// public, #name (key is the first 16 bytes of sha256 of the name) or 0xNN.
static bool parseChannel(const char* s, uint8_t& hash) {
  if (strcmp(s, "public") == 0) {
    static const uint8_t PUBLIC_KEY[16] = {0x8b, 0x33, 0x87, 0xe9, 0xc5, 0xcd, 0xea, 0x6a,
                                           0xc9, 0xe5, 0xed, 0xba, 0xa1, 0x15, 0xcd, 0x72};
    hash = channelHashOf(PUBLIC_KEY);
    return true;
  }
  if (s[0] == '#' && s[1]) {
    uint8_t key[16];
    mesh::Utils::sha256(key, sizeof(key), (const uint8_t*)s, strlen(s));
    hash = channelHashOf(key);
    return true;
  }
  if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X') && strlen(s) == 4) {
    int hi = hexNibble(s[2]), lo = hexNibble(s[3]);
    if (hi < 0 || lo < 0) return false;
    hash = (hi << 4) | lo;
    return true;
  }
  return false;
}

static bool isNumber(const char* s) {
  if (!*s) return false;
  for (; *s; s++) if (*s < '0' || *s > '9') return false;
  return true;
}

static int split(char* s, char* tok[], int max) {
  int n = 0;
  while (*s && n < max) {
    while (*s == ' ') *s++ = 0;
    if (!*s) break;
    tok[n++] = s;
    while (*s && *s != ' ') s++;
  }
  return n;
}

// Reads `chan <channel>` at tok[i], advancing i past it; an error, or nullptr.
static const char* parseChannelArg(char* tok[], int n, int& i, SinkholeEntry& e) {
  const char* c = i + 1 < n ? tok[++i] : "";
  if (strlen(c) >= sizeof(e.label) || !parseChannel(c, e.channel)) return "Error: chan is public, #name or 0xNN";
  e.scope = SCOPE_CHAN_ONE;
  strcpy(e.label, c);
  return nullptr;
}

// Everything after the hash, in any order; an error, or nullptr.
static const char* parseRule(char* tok[], int n, SinkholeEntry& e, uint32_t& hours) {
  bool have_hours = false, have_chan = false, have_depth = false, have_min = false;
  hours = SINKHOLE_DEF_HOURS;
  e.scope = SCOPE_CHAN;
  e.depth = SINKHOLE_DEF_DEPTH;
  for (int i = 3; i < n; i++) {
    const char* t = tok[i];
    if (isNumber(t) && !have_hours) {
      have_hours = true;
      hours = strtoul(t, nullptr, 10);
      if (hours < 1 || hours > SINKHOLE_MAX_HOURS) return "Error: hours 1-168";
    } else if (strcmp(t, "chan") == 0 && !have_chan) {
      have_chan = true;
      const char* err = parseChannelArg(tok, n, i, e);
      if (err) return err;
    } else if (strcmp(t, "depth") == 0 && !have_depth) {
      have_depth = true;
      t = i + 1 < n ? tok[++i] : "";
      unsigned long d = isNumber(t) ? strtoul(t, nullptr, 10) : 0;
      if (strcmp(t, "any") == 0) e.depth = 0;
      else if (d >= 1 && d <= SINKHOLE_MAX_DEPTH) e.depth = d;
      else return "Error: depth 1-63 or any";
    } else if (strcmp(t, "minbytes") == 0 && !have_min) {
      have_min = true;
      t = i + 1 < n ? tok[++i] : "";
      if (strcmp(t, "2") != 0 && strcmp(t, "3") != 0) return "Error: minbytes 2-3";
      e.minbytes = t[0] - '0';
    } else {
      return "Error: sinkhole add|watch <hash|*> [hours] [chan <channel>] [depth <n|any>] [minbytes <2|3>]";
    }
  }
  if (!e.hash_len && have_depth) return "Error: depth needs a hash";
  if (!e.hash_len && !e.minbytes) return "Error: * needs minbytes";
  return nullptr;
}

static bool sameKey(const SinkholeEntry& a, const SinkholeEntry& b) {
  return a.hash_len == b.hash_len && memcmp(a.hash, b.hash, a.hash_len) == 0 && a.scope == b.scope
         && (a.scope != SCOPE_CHAN_ONE || a.channel == b.channel);
}

// At most 4 characters: 9999, 999k, 999M, 4G.
static const char* shortCount(uint32_t v, char* out) {
  if (v < 10000) sprintf(out, "%lu", (unsigned long)v);
  else if (v < 1000000) sprintf(out, "%luk", (unsigned long)(v / 1000));
  else if (v < 1000000000) sprintf(out, "%luM", (unsigned long)(v / 1000000));
  else sprintf(out, "%luG", (unsigned long)(v / 1000000000));
  return out;
}

static const char* shortSpan(uint32_t ms, char* out) {
  uint32_t mins = (ms + 59999) / 60000;
  if (mins < 60) sprintf(out, "%lum", (unsigned long)mins);
  else if (mins < 48 * 60) sprintf(out, "%luh", (unsigned long)((mins + 59) / 60));
  else sprintf(out, "%lud", (unsigned long)(mins / 1440));
  return out;
}

// With minbytes the key shows only the bytes a narrower flood can carry.
static const char* keyText(const SinkholeEntry& e, char out[7]) {
  uint8_t shown = e.minbytes && e.minbytes - 1 < e.hash_len ? e.minbytes - 1 : e.hash_len;
  if (shown) mesh::Utils::toHex(out, e.hash, shown);
  else strcpy(out, "*");
  return out;
}

// `<slot>. key:<k> rule:<r> chan:<c> [depth:<d>] [minbytes:<b>] left:<t>`
static int describe(int i, char* out, size_t cap) {
  const SinkholeEntry& e = entries[i];
  char key[7], left[8];
  int at = snprintf(out, cap, "%d. key:%s rule:%s chan:%s", i + 1, keyText(e, key), e.watch ? "watch" : "drop",
                    e.scope == SCOPE_CHAN ? "any" : e.label);
  if (e.hash_len && e.depth) at += snprintf(out + at, cap - at, " depth:%u", e.depth);
  else if (e.hash_len) at += snprintf(out + at, cap - at, " depth:any");
  if (e.minbytes) at += snprintf(out + at, cap - at, " minbytes:%u", e.minbytes);
  return at + snprintf(out + at, cap - at, " left:%s", shortSpan(e.left_ms, left));
}

// `match:<n> [hop:1x<n>/2x<n>/3x<n>/+x<n>] width:1x<n>/2x<n>/3x<n> last:<t>`
static int describeCounts(int i, char* out, size_t cap) {
  const SinkholeCounts& c = counts[i];
  char a[8], b[8], d[8], f[8];
  int at = snprintf(out, cap, "match:%s", shortCount(c.match, a));
  if (entries[i].hash_len) {
    at += snprintf(out + at, cap - at, " hop:1x%s/2x%s/3x%s/+x%s", shortCount(c.hop[0], a), shortCount(c.hop[1], b),
                   shortCount(c.hop[2], d), shortCount(c.hop[3], f));
  }
  at += snprintf(out + at, cap - at, " width:1x%s/2x%s/3x%s", shortCount(c.width[0], a), shortCount(c.width[1], b),
                 shortCount(c.width[2], d));
  if (!c.match) return at + snprintf(out + at, cap - at, " last:-");
  return at + snprintf(out + at, cap - at, " last:%s", shortSpan(millis() - c.last_ms, f));
}

static void handleAdd(char* tok[], int n, char* reply, bool watch) {
  SinkholeEntry e = {};
  if (n < 3 || !parseHash(tok[2], e)) {
    strcpy(reply, "Error: hash is 2-6 hex digits, whole bytes, or *");
    return;
  }
  uint32_t hours;
  const char* err = parseRule(tok, n, e, hours);
  if (err) {
    strcpy(reply, err);
    return;
  }
  e.active = 1;
  e.watch = watch;
  e.left_ms = hours * 3600000UL;
  SinkholeEntry* slot = nullptr;
  for (auto& x : entries) {
    if (x.active && sameKey(x, e)) { slot = &x; break; }
  }
  if (!slot) {
    for (auto& x : entries) {
      if (!x.active) { slot = &x; break; }
    }
  }
  if (!slot) {
    strcpy(reply, "Error: sinkhole full (16)");
    return;
  }
  *slot = e;
  int i = slot - entries;
  memset(&counts[i], 0, sizeof(counts[i]));
  save();
  strcpy(reply, "OK - ");
  describe(i, reply + 5, SINKHOLE_REPLY_MAX - 5);
}

static void handleRemove(char* tok[], int n, char* reply) {
  SinkholeEntry e = {};
  int i = 3;
  bool given = i < n;
  const char* err = nullptr;
  if (n < 3 || !parseHash(tok[2], e)) {
    err = "Error: sinkhole remove <hash|*> [chan <channel>]";
  } else if (given) {
    if (strcmp(tok[i], "chan") != 0) err = "Error: sinkhole remove <hash|*> [chan <channel>]";
    else if (!(err = parseChannelArg(tok, n, i, e)) && i + 1 != n) err = "Error: sinkhole remove <hash|*> [chan <channel>]";
  }
  if (err) {
    strcpy(reply, err);
    return;
  }
  int removed = 0;
  for (auto& x : entries) {
    if (!x.active || x.hash_len != e.hash_len || memcmp(x.hash, e.hash, e.hash_len) != 0) continue;
    if (given && !sameKey(x, e)) continue;
    x.active = 0;
    removed++;
  }
  if (!removed) {
    strcpy(reply, "Error: no such entry");
    return;
  }
  save();
  sprintf(reply, "OK - %d removed", removed);
}

static void handleClear(char* reply) {
  int removed = 0;
  for (auto& x : entries) {
    if (x.active) removed++;
    x.active = 0;
  }
  save();
  sprintf(reply, "OK - %d removed", removed);
}

static void handleStats(char* reply) {
  int used = 0;
  for (auto& e : entries) used += e.active != 0;
  sprintf(reply, "drops %lu (minbytes %lu), watch matches %lu, entries %d/%d", (unsigned long)stat_drops,
          (unsigned long)stat_minbytes, (unsigned long)stat_watched, used, SINKHOLE_MAX);
}

static int parseSlot(const char* s) {
  int slot = isNumber(s) ? atoi(s) : 0;
  return slot >= 1 && slot <= SINKHOLE_MAX ? slot : 0;
}

// `<slot>. <key> <rule>` per line after a blank first line, from slot `from` on, then `next <slot>` if more remain.
static void handleList(char* tok[], int n, char* reply) {
  int from = 1;
  if (n >= 3 && !(from = parseSlot(tok[2]))) {
    strcpy(reply, "Error: sinkhole list [from], slot 1-16");
    return;
  }
  size_t used = 0;
  bool any = false;
  reply[0] = 0;
  for (int i = from - 1; i < SINKHOLE_MAX; i++) {
    if (!entries[i].active) continue;
    any = true;
    char row[24], key[7];
    int len = snprintf(row, sizeof(row), "\n%d. %s %s", i + 1, keyText(entries[i], key), entries[i].watch ? "watch" : "drop");
    if (used + len + 9 >= SINKHOLE_REPLY_MAX) {
      sprintf(reply + used, "\nnext %d", i + 1);
      return;
    }
    memcpy(reply + used, row, len + 1);
    used += len;
  }
  if (!any) strcpy(reply, from == 1 ? "(none)" : "(none past that)");
}

// The rule in a slot, then its counters, after a blank first line.
static void handleShow(char* tok[], int n, char* reply) {
  int slot = n == 3 ? parseSlot(tok[2]) : 0;
  if (!slot) {
    strcpy(reply, "Error: sinkhole show <slot>, slot 1-16");
    return;
  }
  if (!entries[slot - 1].active) {
    sprintf(reply, "Error: slot %d is empty", slot);
    return;
  }
  reply[0] = '\n';
  int at = 1 + describe(slot - 1, reply + 1, SINKHOLE_REPLY_MAX - 1);
  if (at + 1 < SINKHOLE_REPLY_MAX) {
    reply[at++] = '\n';
    describeCounts(slot - 1, reply + at, SINKHOLE_REPLY_MAX - at);
  }
}

bool floodGateHandleCli(const ModCliContext& context, char* command, char* reply) {
  (void)context;
  if (memcmp(command, "sinkhole", 8) != 0 || (command[8] != 0 && command[8] != ' ')) return false;
  char* tok[12];
  int n = split(command, tok, 12);
  if (n == 1 || strcmp(tok[1], "list") == 0) handleList(tok, n, reply);
  else if (strcmp(tok[1], "show") == 0) handleShow(tok, n, reply);
  else if (strcmp(tok[1], "add") == 0) handleAdd(tok, n, reply, false);
  else if (strcmp(tok[1], "watch") == 0) handleAdd(tok, n, reply, true);
  else if (strcmp(tok[1], "remove") == 0) handleRemove(tok, n, reply);
  else if (strcmp(tok[1], "clear") == 0 && n == 2) handleClear(reply);
  else if (strcmp(tok[1], "stats") == 0 && n == 2) handleStats(reply);
  else strcpy(reply, "Error: sinkhole [list [from]] | show <slot> | add | watch | remove | clear | stats");
  return true;
}
