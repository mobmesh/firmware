#include <helpers/diag_report/TempReport.h>
#include <helpers/diag_report/DiagSummary.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

#define DAY0 20400U

static bool any(char lines[][TEMP_LINE], int n, const char* text) {
  for (int i = 0; i < n; i++) if (strstr(lines[i], text)) return true;
  return false;
}

// One day of CPU readings from lo at dawn to hi in the afternoon, with a steady battery.
static void day(BattHistory& h, HistToday& t, uint32_t d, float lo, float hi) {
  HistToday done;
  const uint8_t hours[] = {4, 9, 15, 21};
  const float temp[] = {lo, (lo + hi) / 2, hi, (lo + hi) / 2};
  for (int i = 0; i < 4; i++) {
    if (histSample(t, done, d, hours[i], 3900, temp[i])) histClose(h, done);
  }
}

int main() {
  static BattHistory h;
  static HistToday t;
  static char lines[16][TEMP_LINE];

  // No sensor and nothing recorded.
  histInit(h);
  memset(&t, 0, sizeof(t));
  TempView v = tempAnalyse(h, t, NAN);
  CHECK(v.kind == TEMP_K_NONE && !v.has_now);
  int n = tempFormat(v, lines, 16);
  CHECK(n == 1 && any(lines, n, "⚪ Not Available"));

  // A reading now but no day filed yet.
  v = tempAnalyse(h, t, 23.4f);
  CHECK(v.kind == TEMP_K_WATCHING && v.has_now && v.now_c == 23);
  n = tempFormat(v, lines, 16);
  CHECK(any(lines, n, "Now 23 °C"));

  // A mild month: normal, five weekly rows, coldest and warmest, nothing to fix.
  for (uint32_t d = 0; d < 30; d++) day(h, t, DAY0 + d, 8 + d % 3, 30 + d % 5);
  v = tempAnalyse(h, t, 21.0f);
  CHECK(v.kind == TEMP_K_NORMAL && v.level == DIAG_GOOD);
  CHECK(v.days == 30 && v.tmin == 8 && v.tmax == 34 && v.cold_days == 0 && v.hot_days == 0);
  n = tempFormat(v, lines, 16);
  CHECK(any(lines, n, "🌡️ CPU: 🟢 Normal"));
  CHECK(any(lines, n, "Week of   Low  High °C"));
  int head = -1, tail = -1;
  for (int i = 0; i < n; i++) {
    if (strstr(lines[i], "Week of")) head = i;
    if (strstr(lines[i], "Coldest")) tail = i;
  }
  CHECK(head >= 0 && tail - head - 1 == TEMP_WEEKS);
  CHECK(any(lines, n, "Coldest 8 °C on") && any(lines, n, "warmest 34 °C on"));
  CHECK(!any(lines, n, "Below 0") && any(lines, n, "Nothing to Fix"));

  // Freezing nights this week: below freezing, with the count.
  for (uint32_t d = 30; d < 33; d++) day(h, t, DAY0 + d, -4, 10);
  v = tempAnalyse(h, t, -2.0f);
  CHECK(v.kind == TEMP_K_COLD && v.level == DIAG_CAUTION && v.cold_days == 3);
  n = tempFormat(v, lines, 16);
  CHECK(any(lines, n, "Below Freezing") && any(lines, n, "Below 0 °C on 3 days"));
  CHECK(any(lines, n, "Expect Slow Charging"));

  // A baking enclosure outranks the cold.
  day(h, t, DAY0 + 33, 2, 71);
  v = tempAnalyse(h, t, 40.0f);
  CHECK(v.kind == TEMP_K_HOT && v.hot_days == 1);
  n = tempFormat(v, lines, 16);
  CHECK(any(lines, n, "Running Hot") && any(lines, n, "65 °C or more on 1 day") && any(lines, n, "Shade It"));
  CHECK(n <= 12);                      // the longest report; the firmware holds 12 lines

  // Cold days older than a week stay in the counts but no longer set the verdict.
  for (uint32_t d = 34; d < 45; d++) day(h, t, DAY0 + d, 6, 25);
  v = tempAnalyse(h, t, 15.0f);
  CHECK(v.kind == TEMP_K_NORMAL && v.cold_days == 3 && v.hot_days == 1);

  // Weeks start on Monday: local day 20767 is Monday 9 Nov 2026, so Sunday 8 Nov closes the week of 2 Nov.
  histInit(h);
  memset(&t, 0, sizeof(t));
  for (uint32_t d = 20766; d <= 20770; d++) day(h, t, d, 5, 15);
  v = tempAnalyse(h, t, 10.0f);
  n = tempFormat(v, lines, 16);
  CHECK(any(lines, n, "2 Nov ") && any(lines, n, "9 Nov "));
  CHECK(!any(lines, n, "8 Nov ") && !any(lines, n, "3 Nov "));

  if (failures == 0) printf("temp report: all checks passed\n");
  return failures == 0 ? 0 : 1;
}
