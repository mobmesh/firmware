#include <cstdio>
#include <helpers/esp32/ProbationPolicy.h>

static int failures = 0;

static void expect(bool ok, const char* what) {
  if (!ok) {
    std::printf("FAIL: %s\n", what);
    failures++;
  }
}

int main() {
  expect(probationNeedsMeshCheck(true, true, false, true), "a new image on a hearing node is checked");
  expect(!probationNeedsMeshCheck(false, true, false, true), "a confirmed image is never checked");
  expect(!probationNeedsMeshCheck(true, false, false, true), "a node that heard nothing is not checked");
  expect(!probationNeedsMeshCheck(true, true, true, true), "a known-good image is not checked");
  expect(!probationNeedsMeshCheck(true, true, false, false), "low battery skips the check");

  expect(probationAdvertDue(0, 0), "first advert at the start of the window");
  expect(!probationAdvertDue(1000, 1), "no second advert before the gap");
  expect(probationAdvertDue(PROBATION_ADVERT_GAP_MS, 1), "second advert after one gap");
  expect(probationAdvertDue(2 * PROBATION_ADVERT_GAP_MS, 2), "third advert after two gaps");
  expect(!probationAdvertDue(PROBATION_WINDOW_MS, PROBATION_ADVERTS), "no more than the cap");
  expect(!probationWindowOver(PROBATION_WINDOW_MS - 1), "window still open");
  expect(probationWindowOver(PROBATION_WINDOW_MS), "window over");

  const uint32_t day = 24UL * 3600 * 1000;
  expect(probationHeardRecently(true, 1000, 1000 + 2 * day), "heard two days ago counts");
  expect(!probationHeardRecently(true, 1000, 1000 + 4 * day), "heard four days ago does not");
  expect(!probationHeardRecently(false, 0, 5000), "never heard does not");
  expect(probationHeardRecently(true, 0xfffff000u, 0x00001000u), "uptime wrap is handled");

  uint32_t total = 0;
  expect(probationParseRx("{\"recv\":9,\"sent\":3,\"flood_tx\":1,\"direct_tx\":2,\"flood_rx\":5,\"direct_rx\":7,\"recv_errors\":0}", total)
         && total == 12, "flood_rx plus direct_rx");
  expect(!probationParseRx("Unknown command", total), "a non-stats reply is refused");

  RejectOutcome crash = probationRejectOutcome(true, false);
  expect(crash.reject && !crash.remember, "a crash rejects at once");
  RejectOutcome first = probationRejectOutcome(false, false);
  expect(!first.reject && first.remember, "a first non-crash rollback earns a retry");
  RejectOutcome second = probationRejectOutcome(false, true);
  expect(second.reject, "a second rollback after the retry rejects");

  if (failures == 0) std::printf("probation policy: all checks passed\n");
  return failures == 0 ? 0 : 1;
}
