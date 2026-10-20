#include <cstdio>
#include <helpers/esp32/OtaAutoPolicy.h>

static int failures = 0;

static void expect(bool ok, const char* what) {
  if (!ok) {
    std::printf("FAIL: %s\n", what);
    failures++;
  }
}

int main() {
  uint8_t days = 0;
  expect(otaAutoParseInterval("7d", days) && days == 7, "7d parses");
  expect(otaAutoParseInterval("4d", days) && days == 4, "minimum 4d parses");
  expect(otaAutoParseInterval("90d", days) && days == 90, "maximum 90d parses");
  expect(!otaAutoParseInterval("3d", days), "3d is below the minimum");
  expect(!otaAutoParseInterval("91d", days), "91d is above the maximum");
  expect(!otaAutoParseInterval("7", days), "the d is required");
  expect(!otaAutoParseInterval("7d ", days), "trailing text is refused");
  expect(!otaAutoParseInterval("d", days), "digits are required");
  expect(!otaAutoParseInterval("0007d", days), "overlong numbers are refused");

  uint8_t confirm = 0;
  expect(otaAutoParseConfirm("2", confirm) && confirm == 2, "2 parses");
  expect(otaAutoParseConfirm("10", confirm) && confirm == 10, "10 parses");
  expect(!otaAutoParseConfirm("1", confirm), "1 is below the minimum");
  expect(!otaAutoParseConfirm("11", confirm), "11 is above the maximum");
  expect(!otaAutoParseConfirm("2d", confirm), "confirm takes no unit");

  const uint32_t anchor = 1790000000u, week = 7 * OTA_AUTO_DAY_SECS;
  expect(otaAutoNextDue(anchor, 7, 0, anchor) == anchor + week, "first check is one interval after the anchor");
  expect(otaAutoNextDue(anchor, 7, anchor + week, anchor + week) == anchor + 2 * week, "next check follows the last");
  expect(otaAutoNextDue(anchor, 7, anchor + week + 5, anchor + week + 5) == anchor + 2 * week, "a late check keeps the grid");
  expect(otaAutoNextDue(anchor, 7, anchor + 10 * week + 1, anchor + 10 * week + 1) == anchor + 11 * week,
         "a forward jump gives one catch-up, then the grid");
  expect(otaAutoNextDue(anchor, 7, anchor + 4 * week, anchor + week + 60) == anchor + 2 * week,
         "a clock corrected back a full interval or more counts from now");
  expect(otaAutoNextDue(anchor, 7, anchor + week + 1, anchor + week - 300) == anchor + 2 * week,
         "a small correction back past the due point does not repeat the check");
  expect(otaAutoNextDue(anchor, 7, anchor + week + 1, anchor + 2) == anchor + 2 * week,
         "a correction just under an interval still keeps the last check");
  expect(otaAutoNextDue(anchor, 14, 0, anchor) == anchor + 2 * week, "interval change keeps the anchor");

  expect(!otaAutoClockBelievable(1772323200u, 1790000000u), "1 Mar 2026 seed is older than the build");
  expect(otaAutoClockBelievable(1790000001u, 1790000000u), "a clock after the build is believable");
  expect(!otaAutoClockBelievable(100u, 0u), "a near-zero clock is never believable");

  uint8_t key_a[32] = {1}, key_b[32] = {2};
  expect(otaAutoNodeOffset(key_a) < OTA_AUTO_DAY_SECS, "offset is under a day");
  expect(otaAutoNodeOffset(key_a) == otaAutoNodeOffset(key_a), "offset is stable");
  expect(otaAutoNodeOffset(key_a) != otaAutoNodeOffset(key_b), "different keys spread");

  expect(otaAutoSighting(false, 5) == 1, "a new image restarts the count");
  expect(otaAutoSighting(true, 1) == 2, "the same image counts up");
  expect(otaAutoSighting(true, 255) == 255, "the count saturates");

  if (failures == 0) std::printf("ota auto policy: all checks passed\n");
  return failures == 0 ? 0 : 1;
}
