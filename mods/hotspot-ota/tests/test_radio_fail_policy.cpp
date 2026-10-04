#include <cstdio>
#include <helpers/esp32/RadioFailPolicy.h>

static int failures = 0;

static void expect(bool ok, const char* what) {
  if (!ok) {
    std::printf("FAIL: %s\n", what);
    failures++;
  }
}

int main() {
  const uint8_t cap = 5;
  const uint32_t start = 60, max = 900;

  for (uint8_t count = 0; count < cap; count++) {
    RadioFailAction a = radioFailAction(count, true, cap, start, max);
    expect(a.restart, "a durable count under the cap restarts");
  }
  expect(radioFailAction(5, true, cap, start, max).sleep_secs == 60, "first sleep is the start interval");
  expect(radioFailAction(6, true, cap, start, max).sleep_secs == 120, "sleep doubles");
  expect(radioFailAction(9, true, cap, start, max).sleep_secs == 900, "sleep is capped");
  expect(radioFailAction(255, true, cap, start, max).sleep_secs == 900, "a saturated count stays capped");
  expect(!radioFailAction(255, true, cap, start, max).restart, "a saturated count never restarts");

  for (int count = 0; count <= 255; count++) {
    RadioFailAction a = radioFailAction((uint8_t)count, false, cap, start, max);
    expect(!a.restart, "an unsaved count never restarts");
    expect(a.sleep_secs == max, "an unsaved count sleeps the longest interval");
  }

  std::printf("radio fail policy: %d failures\n", failures);
  return failures ? 1 : 0;
}
