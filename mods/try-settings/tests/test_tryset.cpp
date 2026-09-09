// Behavioural cases for try-settings, run on the host against the real integration source.
#include "harness.h"

#include <helpers/esp32/TrySetIntegration.h>

#include <cstdio>
#include <string>

static int failures = 0;
static const ModCliContext CTX{0, "test", "test"};

static void check(bool ok, const char* what) {
  if (!ok) { printf("FAIL: %s\n", what); failures++; }
}

static std::string cli(const char* command) {
  char cmd[192], reply[160];
  snprintf(cmd, sizeof(cmd), "%s", command);
  memset(reply, 0, sizeof(reply));
  trySetHandleCli(CTX, cmd, reply);
  return reply;
}

// The module's slot is file-static and outlives a case, so a leftover trial is reverted
// through the CLI before the fakes are wiped.
static void start(const char* key = "tx", const char* value = "20") {
  fake.temp_radio_live = false;   // so revert takes the slot path, not the radio one
  fake.set_fails.clear();         // so the revert's restore is not refused
  cli("tryset revert");
  fake.reset();
  fake.prefs[key] = value;
  trySetLoop();   // consumes the boot pass
}

static void saveFailureLeavesSettingUntouched() {
  start();
  fake.save_ok = false;
  std::string r = cli("tryset 300 tx 14");
  check(r.rfind("Error", 0) == 0, "save failure is reported as an error");
  check(fake.prefs["tx"] == "20", "save failure leaves the setting untouched");
  check(!fake.saved, "save failure leaves no record");
}

static void recordIsWrittenBeforeTheSettingMoves() {
  start();
  std::string r = cli("tryset 300 tx 14");
  check(r.rfind("OK", 0) == 0, "a good trial is accepted");
  check(fake.prefs["tx"] == "14", "the trial value is applied");
  check(fake.saved, "a rollback record exists");
  check(std::string(fake.record.snapshot) == "20", "the record holds the original value");
}

static void failedApplyRestoresAndClears() {
  start();
  fake.set_fails.insert("tx");
  fake.set_persists_before_failing.insert("tx");
  std::string r = cli("tryset 300 tx 14");
  check(r.find("refused") != std::string::npos, "the original failure is reported");
  check(fake.prefs["tx"] == "20", "a failed apply restores the snapshot");
  check(!fake.saved, "a failed apply removes the prepared record");
}

static void expiryRestoresTheSnapshot() {
  start();
  cli("tryset 300 tx 14");
  fake.clock += 299;
  trySetLoop();
  check(fake.prefs["tx"] == "14", "the trial holds until it expires");
  fake.clock += 2;
  trySetLoop();
  check(fake.prefs["tx"] == "20", "expiry restores the snapshot");
  check(!fake.saved, "expiry clears the record");
}

// `booted` is file-static, so a reboot is a fresh process rather than a flag reset. This
// case runs alone, before anything has consumed the boot pass.
static void rebootRestoresRatherThanResuming() {
  fake.reset();
  fake.prefs["tx"] = "14";           // the trial value, persisted before the reset
  TrySetSlot rec{};
  strcpy(rec.key, "tx");
  strcpy(rec.snapshot, "20");
  strcpy(rec.trial, "14");
  rec.expires_at = fake.clock + 3600; // deadline still in the future
  fake.record = rec;
  fake.saved = true;

  trySetLoop();
  check(fake.prefs["tx"] == "20", "a record found at boot is reverted, not resumed");
  check(!fake.saved, "the record is cleared once reverted");
}

static void failedRestoreKeepsTheRecord() {
  start();
  cli("tryset 300 tx 14");
  fake.set_fails.insert("tx");
  fake.clock += 400;
  trySetLoop();
  check(fake.saved, "a refused restore keeps the record for a retry");
  fake.set_fails.clear();
  fake.clock += 1;   // the retry is rate-limited to one attempt per second
  trySetLoop();
  check(fake.prefs["tx"] == "20", "the retry restores the snapshot");
  check(!fake.saved, "a successful retry clears the record");
}

static void revertReportsRestoreFailure() {
  start();
  cli("tryset 300 tx 14");
  fake.set_fails.insert("tx");
  std::string r = cli("tryset revert");
  check(r.rfind("Error", 0) == 0, "a refused revert is reported as an error");
  check(fake.saved, "a refused revert keeps the record");
  check(fake.prefs["tx"] == "14", "a refused revert leaves the trial value in place");

  fake.set_fails.clear();
  r = cli("tryset revert");
  check(r.rfind("OK", 0) == 0, "the retry succeeds");
  check(fake.prefs["tx"] == "20", "the retry restores the snapshot");
  check(!fake.saved, "the successful revert clears the record");
}

static void failedRestoreIsRateLimited() {
  start();
  cli("tryset 300 tx 14");
  fake.set_fails.insert("tx");
  fake.clock += 400;

  size_t before = fake.dispatched.size();
  for (int i = 0; i < 50; i++) trySetLoop();   // many loop passes inside one RTC second
  size_t attempts = fake.dispatched.size() - before;
  check(attempts <= 1, "a failing restore is attempted at most once per second");

  fake.clock += 1;
  before = fake.dispatched.size();
  for (int i = 0; i < 50; i++) trySetLoop();
  check(fake.dispatched.size() - before <= 1, "the next second allows one more attempt");
  check(fake.saved, "the record survives the failed retries");
}

static void trialsCannotOverlap() {
  start();
  cli("tryset 300 tx 14");
  std::string r = cli("tryset 300 cad on");
  check(r.rfind("Error", 0) == 0, "a second key trial is refused");
  check(r.find("tx") != std::string::npos, "the refusal names the running trial");

  fake.temp_radio_live = true;
  r = cli("tryset 300 tx 14");
  check(r.rfind("Error", 0) == 0, "a key trial is refused while radio is live");
  check(r.find("radio") != std::string::npos, "the refusal names the radio trial");
}

static void radioRejectsPartialMinutes() {
  start();
  std::string r = cli("tryset 119 radio 915.0,250,11,5");
  check(r.rfind("Error", 0) == 0, "a duration that is not whole minutes is refused");
  check(r.find("60") != std::string::npos && r.find("120") != std::string::npos,
        "the refusal offers the two nearest valid durations");
  check(!fake.temp_radio_live, "nothing was dispatched to tempradio");

  r = cli("tryset 120 radio 915.0,250,11,5");
  check(fake.temp_radio_live, "a whole-minute duration reaches tempradio");
}

static void unsupportedKeyIsDeclined() {
  start();
  fake.get_missing.insert("radio.fem.rxgain");
  std::string r = cli("tryset 300 radio.fem.rxgain off");
  check(r.find("unsupported") != std::string::npos, "an unreadable key is declined");
  check(!fake.saved, "a declined key writes no record");
}

static void replyStaysInsideTheCallerBuffer() {
  start();
  fake.set_fails.insert("tx");
  // A refusal longer than the reply buffer must be truncated, not overrun it.
  char cmd[64] = "tryset 300 tx 14";
  char guarded[161];
  memset(guarded, 0xAA, sizeof(guarded));
  trySetHandleCli(CTX, cmd, guarded);
  check((unsigned char)guarded[160] == 0xAA, "the byte past the reply buffer is untouched");
  check(strlen(guarded) < 160, "the reply fits the buffer");
}

int main(int argc, char** argv) {
  if (argc > 1 && strcmp(argv[1], "reboot") == 0) {
    rebootRestoresRatherThanResuming();
    if (failures) { printf("%d check(s) failed\n", failures); return 1; }
    printf("all checks passed\n");
    return 0;
  }

  saveFailureLeavesSettingUntouched();
  recordIsWrittenBeforeTheSettingMoves();
  failedApplyRestoresAndClears();
  expiryRestoresTheSnapshot();
  failedRestoreKeepsTheRecord();
  revertReportsRestoreFailure();
  failedRestoreIsRateLimited();
  trialsCannotOverlap();
  radioRejectsPartialMinutes();
  unsupportedKeyIsDeclined();
  replyStaysInsideTheCallerBuffer();

  if (failures) { printf("%d check(s) failed\n", failures); return 1; }
  printf("all checks passed\n");
  return 0;
}
