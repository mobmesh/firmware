#include <helpers/esp32/TrySetIntegration.h>

#include <Arduino.h>
#include <helpers/esp32/TrySet.h>

// Seconds, not minutes: `set` takes values, not durations. The floor exists because the radio
// proxy converts to minutes, and anything shorter would floor to a zero upstream rejects.
#define TRY_SET_MIN_SECS 60
#define TRY_SET_MAX_SECS 86400UL

// The reply buffer a caller supplies: `char reply[160]` on the serial path in main.cpp, and
// no larger on the mesh path. Index 160 is one past the end of it.
#define TRY_SET_REPLY_MAX 160

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

// One restore attempt per RTC second, so a persistently failing one cannot hammer flash.
// Compared by equality, not ordering, so a brownout clock jump costs one attempt at most.
static uint32_t last_restore_try = 0;

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

// The record is the only way back to the original value, so a refused restore keeps it:
// erasing on failure would strand the node on the trial value with nothing left to undo it.
static bool restore(const TrySetSlot& s) {
  char rep[192];
  return cliSet(s.key, s.snapshot, rep, sizeof(rep));
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

// A radio trial lives in upstream's timers, a key trial in the slot: separate state, so
// without this both could run at once and keep|revert would act on the radio one only.
static bool trialRunning(char* what, size_t what_len) {
  char live[48];
  if (liveTempRadio(live, sizeof(live))) {
    snprintf(what, what_len, "radio");
    return true;
  }
  if (slot.active()) {
    snprintf(what, what_len, "%s", slot.key);
    return true;
  }
  return false;
}

static void startRadio(uint32_t secs, const char* value, char* reply) {
  char cmd[128];
  // tempradio counts in whole minutes, so anything finer than that is not expressible.
  // Rejecting it beats silently running 119s as 60s.
  if (secs % 60) {
    sprintf(reply, "Error: radio trials take whole minutes -- use %u or %u",
            (unsigned)(secs / 60 * 60), (unsigned)((secs / 60 + 1) * 60));
    return;
  }
  snprintf(cmd, sizeof(cmd), "tempradio %s,%u", value, (unsigned)(secs / 60));
  modCliDispatch(0, cmd, reply);   // upstream's own validation, and its own reply
}

static void startKey(uint32_t secs, const char* key, const char* value, char* reply) {
  if (!keyAllowed(key)) {
    sprintf(reply, "Error: %s is not trialable", key);
    return;
  }
  char snapshot[24];
  if (!cliGet(key, snapshot, sizeof(snapshot))) {
    sprintf(reply, "Error: %s unsupported on this board", key);
    return;
  }

  // The record goes to flash before the setting moves, so a reset between the two finds
  // either an untouched node or a trial it can undo -- never a trial value with no way back.
  TrySetSlot prepared = {};   // zeroed whole, so a 23-char value still lands terminated
  strncpy(prepared.key, key, sizeof(prepared.key) - 1);
  strncpy(prepared.snapshot, snapshot, sizeof(prepared.snapshot) - 1);
  strncpy(prepared.trial, value, sizeof(prepared.trial) - 1);
  prepared.expires_at = modClockGet() + secs;
  if (!trySetSave(prepared)) {
    sprintf(reply, "Error: could not record rollback -- %s unchanged", key);
    return;
  }

  char rep[192];
  if (!cliSet(key, value, rep, sizeof(rep))) {
    // radio.rxgain saves the pref before it reports the failure, so a refusal that left the
    // value changed would strand it with no trial to take it back.
    char restore_rep[192];
    cliSet(key, snapshot, restore_rep, sizeof(restore_rep));
    trySetErase();
    strncpy(reply, rep, TRY_SET_REPLY_MAX - 1);
    reply[TRY_SET_REPLY_MAX - 1] = 0;
    return;
  }

  slot = prepared;

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

  char running[32];
  if (trialRunning(running, sizeof(running))) {
    sprintf(reply, "Error: %s trial running -- tryset keep|revert", running);
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
    // Same rule as the expiry and boot paths: a refused restore keeps the record, because
    // it is the only way back, and says so rather than reporting a revert that did not happen.
    if (!restore(slot)) {
      sprintf(reply, "Error: %s could not be restored -- trial kept, retrying", slot.key);
      return;
    }
    sprintf(reply, "OK - %s reverted", slot.key);
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
      // Zeroing the deadline hands the retry to the expiry path: after a brownout that
      // deadline is the untrustworthy part.
      last_restore_try = modClockGet();
      if (restore(slot)) clearSlot(); else slot.expires_at = 0;
    }
    return;
  }
  if (!slot.active()) return;
  uint32_t now = modClockGet();
  if (now < slot.expires_at) return;
  if (now == last_restore_try) return;
  last_restore_try = now;
  if (restore(slot)) clearSlot();
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
