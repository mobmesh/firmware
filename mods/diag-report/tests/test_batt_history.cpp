#include <helpers/diag_report/BattHistory.h>
#include <helpers/diag_report/DiagSummary.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

#define DAY0 20400U                    // a local day number in mid-November 2025

static bool any(char lines[][HIST_LINE], int n, const char* text) {
  for (int i = 0; i < n; i++) if (strstr(lines[i], text)) return true;
  return false;
}

// One day: a morning dip to `morning_mv` at 6am, a sunny afternoon, temperatures from lo to hi.
static void day(BattHistory& h, HistToday& t, uint32_t d, uint16_t morning_mv, float lo, float hi) {
  HistToday done;
  const uint8_t hours[] = {0, 6, 9, 14, 20};
  const uint16_t mv[] = {(uint16_t)(morning_mv + 20), morning_mv, (uint16_t)(morning_mv + 10),
                         (uint16_t)(morning_mv + 200), (uint16_t)(morning_mv + 120)};
  const float temp[] = {lo, lo, (lo + hi) / 2, hi, (lo + hi) / 2};
  for (int i = 0; i < 5; i++) {
    if (histSample(t, done, d, hours[i], mv[i], temp[i])) histClose(h, done);
  }
}

int main() {
  static BattHistory h;
  static HistToday t;
  static char lines[16][HIST_LINE];

  // The morning low only looks before noon; temperatures span the whole day.
  histInit(h);
  memset(&t, 0, sizeof(t));
  day(h, t, DAY0, 3900, 10, 30);
  CHECK(t.day == DAY0 && t.morning_mv == 3900);
  CHECK(t.tmin == 10 && t.tmax == 30);
  CHECK(h.last_day == 0);              // today is not on file until the day turns

  // The next day's first reading files yesterday.
  HistToday done;
  CHECK(histSample(t, done, DAY0 + 1, 3, 3950, 12));
  histClose(h, done);
  CHECK(h.last_day == DAY0 && h.d[DAY0 % HIST_DAYS].morning == (3900 - 2500 + 4) / 8);
  CHECK(h.d[DAY0 % HIST_DAYS].tmin == 10 && h.d[DAY0 % HIST_DAYS].tmax == 30);

  // A clock moved back starts the day afresh and files nothing.
  uint32_t before = h.last_day;
  CHECK(!histSample(t, done, DAY0 - 3, 8, 3800, 15));
  CHECK(h.last_day == before && t.day == DAY0 - 3);

  // Under a week of mornings: watching, with no verdict yet.
  histInit(h);
  memset(&t, 0, sizeof(t));
  for (uint32_t d = 0; d < 4; d++) day(h, t, DAY0 + d, 3900, 10, 25);
  HistView v = histAnalyse(h, t, false);
  CHECK(v.kind == HIST_K_WATCHING && v.level == DIAG_UNKNOWN && v.days == 4);
  CHECK(v.missing == 0);               // days before the watch began are not missing
  int n = histFormat(v, lines, 16);
  CHECK(any(lines, n, "Watching, 4 of 7 days"));
  CHECK(any(lines, n, "Verdict After 7 Days"));

  // Thirty steady mornings hold, with no warnings.
  histInit(h);
  memset(&t, 0, sizeof(t));
  for (uint32_t d = 0; d < 30; d++) day(h, t, DAY0 + d, (uint16_t)(3900 + (d % 3) * 8), 8, 36);
  v = histAnalyse(h, t, true);
  CHECK(v.kind == HIST_K_HOLDING && v.level == DIAG_GOOD);
  CHECK(v.days == 30 && v.held == v.pairs && v.pairs == 29 && v.missing == 0);
  n = histFormat(v, lines, 16);
  CHECK(any(lines, n, "🟢 Holding"));
  CHECK(any(lines, n, "Held 29 of 29 days, 0 missing"));
  CHECK(!any(lines, n, "CPU") && !any(lines, n, "°C"));   // temperature lives in diag temp
  CHECK(any(lines, n, "Nothing to Fix"));
  CHECK(any(lines, n, "Nov") || any(lines, n, "Dec") || any(lines, n, "Oct"));

  // A steady 12 mV a day fall loses ground, and the cold days are called out.
  histInit(h);
  memset(&t, 0, sizeof(t));
  for (uint32_t d = 0; d < 30; d++) day(h, t, DAY0 + d, (uint16_t)(3900 - d * 12), d < 2 ? -3 : 4, 15);
  v = histAnalyse(h, t, false);
  CHECK(v.kind == HIST_K_LOSING && v.level == DIAG_CAUTION);
  CHECK(v.net_mv < -300 && v.net_days == 29);
  n = histFormat(v, lines, 16);
  CHECK(any(lines, n, "🟡 Losing Ground"));
  CHECK(any(lines, n, "Check the Panel"));

  // Low only where the divider is measured.
  histInit(h);
  memset(&t, 0, sizeof(t));
  for (uint32_t d = 0; d < 10; d++) day(h, t, DAY0 + d, 3340, 10, 20);
  CHECK(histAnalyse(h, t, true).kind == HIST_K_LOW);
  CHECK(histAnalyse(h, t, false).kind == HIST_K_HOLDING);

  // Skipped days are marked missing, and a gap longer than the window leaves nothing stale.
  histInit(h);
  memset(&t, 0, sizeof(t));
  for (uint32_t d = 0; d < 10; d++) day(h, t, DAY0 + d, 3900, 10, 20);
  for (uint32_t d = 13; d < 20; d++) day(h, t, DAY0 + d, 3900, 10, 20);
  v = histAnalyse(h, t, false);
  CHECK(v.missing == 3 && v.days == 17);
  for (uint32_t d = 60; d < 62; d++) day(h, t, DAY0 + d, 3700, 10, 20);
  v = histAnalyse(h, t, false);
  CHECK(v.days == 2 && v.low_mv >= 3696 && v.low_mv <= 3704);

  // Temperatures are still filed with each day, for diag temp to read.
  CHECK(histDay(h, t, DAY0 + 61).tmin == 10 && histDay(h, t, DAY0 + 61).tmax == 20);
  CHECK(n <= 10);

  if (failures == 0) printf("batt history: all checks passed\n");
  return failures == 0 ? 0 : 1;
}
