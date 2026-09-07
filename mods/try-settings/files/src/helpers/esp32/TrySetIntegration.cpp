#include <helpers/esp32/TrySetIntegration.h>

#include <Arduino.h>
#include <helpers/esp32/TrySet.h>

// Seconds, not minutes: `set` takes values, not durations. The floor exists because the radio
// proxy converts to minutes, and anything shorter would floor to a zero upstream rejects.
#define TRY_SET_MIN_SECS 60
#define TRY_SET_MAX_SECS 86400UL

// Keys that can make a node unreachable, or that can only be judged on site. An allowlist:
// a key earns its place by being one of those, not by being settable.
static const char* const TRY_SET_KEYS[] = {
  // Gain and power: nothing temporary exists for these at all.
  "tx",
  "radio.rxgain",
  "radio.fem.rxgain",
  "radio.fem.txgain",
  // Site RF: Dispatcher::loop() re-reads these, so a trial takes hold within a calibration tick.
  "int.thresh",
  "cad",
  "agc.reset.interval",
  // Strand risk: these can cut a node off from the far mesh while it still answers up close.
  "flood.max",
  "flood.max.unscoped",
  "flood.max.advert",
  "repeat",
  "dutycycle",
  nullptr,
};

static TrySetSlot slot;
static bool booted = false;

static bool keyAllowed(const char* key) {
  for (int i = 0; TRY_SET_KEYS[i]; i++) {
    if (strcmp(key, TRY_SET_KEYS[i]) == 0) return true;
  }
  return false;
}

// Upstream answers a read with "> value"; anything else (notably "Error: unsupported" from a
// board that cannot control what was asked for) means there is nothing to snapshot.
static bool cliGet(const char* key, char* out, size_t out_len) {
  char cmd[64], rep[192];
  snprintf(cmd, sizeof(cmd), "get %s", key);
  rep[0] = 0;
  modCliDispatch(0, cmd, rep);
  if (rep[0] != '>') return false;
  const char* v = rep + 1;
  while (*v == ' ') v++;
  strncpy(out, v, out_len - 1);
  out[out_len - 1] = 0;
  return out[0] != 0;
}

static bool cliSet(const char* key, const char* value, char* rep, size_t rep_len) {
  char cmd[128];
  snprintf(cmd, sizeof(cmd), "set %s %s", key, value);
  rep[0] = 0;
  modCliDispatch(0, cmd, rep);
  return strncmp(rep, "OK", 2) == 0;
}

static void restore(const TrySetSlot& s) {
  char rep[192];
  cliSet(s.key, s.snapshot, rep, sizeof(rep));
}

static void clearSlot() {
  slot.clear();
  trySetErase();
}

// A live tempradio trial is upstream's to own; formatting its params back into set-args is all
// this needs to do.
static bool liveTempRadio(char* out, size_t out_len) {
  float freq, bw;
  uint8_t sf, cr;
  if (!modTempRadioGet(&freq, &bw, &sf, &cr)) return false;
  snprintf(out, out_len, "%g,%g,%u,%u", freq, bw, (unsigned)sf, (unsigned)cr);
  return true;
}

static void startRadio(uint32_t secs, const char* value, char* reply) {
  char cmd[128];
  // tempradio counts in minutes and rejects zero, which is what the 60s floor above protects.
  snprintf(cmd, sizeof(cmd), "tempradio %s,%u", value, (unsigned)(secs / 60));
  modCliDispatch(0, cmd, reply);   // upstream's own validation, and its own reply
}

static void startKey(uint32_t secs, const char* key, const char* value, char* reply) {
  if (!keyAllowed(key)) {
    sprintf(reply, "Error: %s is not trialable", key);
    return;
  }
  if (slot.active()) {
    sprintf(reply, "Error: %s trial running -- tryset keep|revert", slot.key);
    return;
  }

  char snapshot[24];
  if (!cliGet(key, snapshot, sizeof(snapshot))) {
    sprintf(reply, "Error: %s unsupported on this board", key);
    return;
  }

  char rep[192];
  if (!cliSet(key, value, rep, sizeof(rep))) {
    // radio.rxgain saves the pref before it reports the failure, so a refusal that left the
    // value changed would strand it with no trial to take it back.
    cliSet(key, snapshot, rep, sizeof(rep));
    strncpy(reply, rep, 160);
    reply[160] = 0;
    return;
  }

  slot.clear();
  strncpy(slot.key, key, sizeof(slot.key) - 1);
  strncpy(slot.snapshot, snapshot, sizeof(slot.snapshot) - 1);
  strncpy(slot.trial, value, sizeof(slot.trial) - 1);
  slot.expires_at = modClockGet() + secs;
  trySetSave(slot);

  sprintf(reply, "OK - tryset %us (reverts unless kept)", (unsigned)secs);
}

static void handleStart(const char* args, char* reply) {
  const char* p = args;
  while (*p >= '0' && *p <= '9') p++;
  if (p == args || *p != ' ') {
    strcpy(reply, "Error: usage tryset <secs> <key> <value>");
    return;
  }
  uint32_t secs = (uint32_t)atol(args);
  if (secs < TRY_SET_MIN_SECS) {
    strcpy(reply, "Error: minimum 60s");
    return;
  }
  if (secs > TRY_SET_MAX_SECS) {
    strcpy(reply, "Error: maximum 86400s");
    return;
  }

  while (*p == ' ') p++;
  const char* sp = strchr(p, ' ');
  if (!sp) {
    strcpy(reply, "Error: usage tryset <secs> <key> <value>");
    return;
  }
  char key[24];
  size_t len = (size_t)(sp - p);
  if (len == 0 || len >= sizeof(key)) {
    strcpy(reply, "Error: bad key");
    return;
  }
  memcpy(key, p, len);
  key[len] = 0;
  while (*sp == ' ') sp++;
  if (*sp == 0) {
    strcpy(reply, "Error: missing value");
    return;
  }

  if (strcmp(key, "radio") == 0) {
    startRadio(secs, sp, reply);
  } else {
    startKey(secs, key, sp, reply);
  }
}

static void handleKeep(char* reply) {
  char live[48];
  if (liveTempRadio(live, sizeof(live))) {
    char cmd[96];
    snprintf(cmd, sizeof(cmd), "set radio %s", live);
    modCliDispatch(0, cmd, reply);   // upstream replies "OK - reboot to apply"
    return;
  }
  if (slot.active()) {
    sprintf(reply, "OK - %s kept", slot.key);
    clearSlot();
    return;
  }
  strcpy(reply, "Error: no live trial to keep");
}

static void handleRevert(char* reply) {
  char live[48];
  if (liveTempRadio(live, sizeof(live))) {
    // Upstream has no cancel, so re-arm a one-minute trial of the saved params: the radio is
    // back within 2s and the timer then reverts to the same values it already holds.
    char saved[48];
    if (!cliGet("radio", saved, sizeof(saved))) {
      strcpy(reply, "Error: cannot read saved radio params");
      return;
    }
    char cmd[96], rep[192];
    snprintf(cmd, sizeof(cmd), "tempradio %s,1", saved);
    rep[0] = 0;
    modCliDispatch(0, cmd, rep);
    strcpy(reply, "OK - radio reverting to saved params");
    return;
  }
  if (slot.active()) {
    sprintf(reply, "OK - %s reverted", slot.key);
    restore(slot);
    clearSlot();
    return;
  }
  strcpy(reply, "Error: no live trial");
}

static void handleGet(char* reply) {
  char live[48];
  if (liveTempRadio(live, sizeof(live))) {
    sprintf(reply, "radio %s (tempradio)", live);
    return;
  }
  if (slot.active()) {
    uint32_t now = modClockGet();
    uint32_t left = slot.expires_at > now ? slot.expires_at - now : 0;
    sprintf(reply, "%s %s %us", slot.key, slot.trial, (unsigned)left);
    return;
  }
  strcpy(reply, "(none)");
}

void trySetLoop() {
  if (!booted) {
    booted = true;
    // A slot that survived a reboot is not resumed: an unscheduled restart is most likely a
    // brownout, and a brownout leaves the RTC deadline it was counting against unverifiable.
    if (trySetLoad(slot)) {
      restore(slot);
      clearSlot();
    }
    return;
  }
  if (!slot.active()) return;
  if (modClockGet() < slot.expires_at) return;
  restore(slot);
  clearSlot();
}

bool trySetHandleCli(const ModCliContext& context, char* command, char* reply) {
  if (memcmp(command, "get tryset", 10) == 0 && (command[10] == 0 || command[10] == ' ')) {
    handleGet(reply);
    return true;
  }
  if (memcmp(command, "tryset", 6) != 0) return false;
  if (command[6] != 0 && command[6] != ' ') return false;

  const char* args = command[6] == 0 ? "" : command + 7;
  while (*args == ' ') args++;

  if (memcmp(args, "keep", 4) == 0 && (args[4] == 0 || args[4] == ' ')) {
    handleKeep(reply);
  } else if (memcmp(args, "revert", 6) == 0 && (args[6] == 0 || args[6] == ' ')) {
    handleRevert(reply);
  } else if (*args == 0) {
    handleGet(reply);
  } else {
    handleStart(args, reply);
  }
  return true;
}
