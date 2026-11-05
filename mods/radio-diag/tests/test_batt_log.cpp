#include <helpers/radio_diag/BattLog.h>
#include <helpers/radio_diag/DiagSummary.h>

#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

#define NOW_HOUR 500016U               // a multiple of 24, so local hour = slot at tz 0

static bool any(char lines[][BATT_LINE], int n, const char* text) {
  for (int i = 0; i < n; i++) if (strstr(lines[i], text)) return true;
  return false;
}

// One value per hour, oldest first, ending at NOW_HOUR.
static void ramp(BattLog& log, uint16_t from, int step, int hours) {
  memset(&log, 0, sizeof(log));
  for (int age = hours - 1; age >= 0; age--) {
    uint16_t mv = (uint16_t)(from + step * (hours - 1 - age));
    battSample(log, NOW_HOUR - age, 0, mv);
    battSample(log, NOW_HOUR - age, 0, (uint16_t)(mv + step));
  }
}

int main() {
  static BattLog log;
  static char lines[16][BATT_LINE];

  CHECK(battCharge(2400) == 0);
  CHECK(battCharge(4300) == 1000);
  CHECK(battCharge(3800) == 500);
  CHECK(battCharge(3750) == 400);

  // A slot keeps its low, high, first and last, and a new hour replaces the old one in its slot
  memset(&log, 0, sizeof(log));
  battSample(log, NOW_HOUR, 0, 3900);
  battSample(log, NOW_HOUR, 0, 3850);
  battSample(log, NOW_HOUR, 0, 3880);
  const BattHour& h = log.h[NOW_HOUR % BATT_HOURS];
  CHECK(h.lo == 3850 && h.hi == 3900 && h.first == 3900 && h.last == 3880);
  battSample(log, NOW_HOUR + 24, 0, 3700);
  CHECK(h.hour == NOW_HOUR + 24 && h.lo == 3700 && h.hi == 3700);

  // Nothing yet
  memset(&log, 0, sizeof(log));
  BattView v = battAnalyse(log, NOW_HOUR, 0, true, 2800);
  CHECK(v.hours == 0 && v.level == DIAG_UNKNOWN);
  int n = battFormat(v, lines, 16);
  CHECK(n == 1 && any(lines, n, "watching, 0 of 24h"));

  // A falling night is not a verdict until a whole cycle is in
  ramp(log, 3950, -5, 8);
  v = battAnalyse(log, NOW_HOUR, 0, true, 2800);
  CHECK(v.kind == BATT_K_WATCHING && v.level == DIAG_UNKNOWN && v.hours_left == 0);
  n = battFormat(v, lines, 16);
  CHECK(any(lines, n, "watching, 8 of 24h") && any(lines, n, "A verdict comes after 20 hours"));

  // But an empty cell is said at once
  ramp(log, 3480, -1, 3);
  v = battAnalyse(log, NOW_HOUR, 0, true, 2800);
  CHECK(v.kind == BATT_K_LOW && v.level == DIAG_PROBLEM);

  // Steady, full day
  ramp(log, 3950, 0, 24);
  v = battAnalyse(log, NOW_HOUR, 0, true, 2800);
  CHECK(v.kind == BATT_K_STEADY && v.level == DIAG_GOOD && v.hours_left == 0 && v.charge_hours == 0);
  n = battFormat(v, lines, 16);
  CHECK(any(lines, n, "steady") && any(lines, n, "Nothing to fix"));

  // Rising
  ramp(log, 3700, 10, 24);
  v = battAnalyse(log, NOW_HOUR, 0, true, 2800);
  CHECK(v.kind == BATT_K_CHARGING && v.level == DIAG_GOOD);

  // Falling with no charging at all
  ramp(log, 4000, -5, 24);
  v = battAnalyse(log, NOW_HOUR, 0, true, 2800);
  CHECK(v.kind == BATT_K_DRAINING && v.level == DIAG_CAUTION && v.hours_left > 24);
  n = battFormat(v, lines, 16);
  CHECK(any(lines, n, "draining, about") && any(lines, n, "days left") && any(lines, n, "Charge it, or add a panel"));

  // A solar day that charges but ends lower
  memset(&log, 0, sizeof(log));
  for (int age = 23; age >= 0; age--) {
    int local = (int)((NOW_HOUR - age) % 24);
    bool sun = local >= 9 && local < 15;
    uint16_t base = (uint16_t)(3950 - (23 - age) * 6);
    battSample(log, NOW_HOUR - age, 0, base);
    battSample(log, NOW_HOUR - age, 0, (uint16_t)(sun ? base + 40 : base - 5));
  }
  v = battAnalyse(log, NOW_HOUR, 0, true, 2800);
  CHECK(v.kind == BATT_K_NOT_RECOVERING && v.level == DIAG_CAUTION);
  CHECK(v.charge_hours == 6 && v.night_drain == 5 && v.net_mv < -30 && v.percent > 0);
  n = battFormat(v, lines, 16);
  CHECK(any(lines, n, "not recovering") && any(lines, n, "Check the panel") && any(lines, n, "Night drain 5 mV/h, charged 6 hours"));
  CHECK(any(lines, n, "6a    12p   6p    12a") && any(lines, n, "Net -") && any(lines, n, "24h "));
  char text[48];
  CHECK(battSummary(v, text, sizeof(text)) == DIAG_CAUTION && strstr(text, "not recovering") != nullptr);

  // Days left: charge above the floor over the average daily loss. 3,780 mV, 60 lost yesterday, 30 lost by noon
  memset(&log, 0, sizeof(log));
  battSample(log, NOW_HOUR - 24, 0, 3870);
  for (int i = 1; i < 24; i += 4) battSample(log, NOW_HOUR - 24 + i, 0, (uint16_t)(3870 - i * 2));
  battSample(log, NOW_HOUR, 0, 3810);
  CHECK(log.have_yesterday && log.yesterday_mv == 3870 && log.today_mv == 3810);
  battSample(log, NOW_HOUR + 12, 0, 3780);
  v = battAnalyse(log, NOW_HOUR + 12, 0, true, 2800);
  CHECK(v.hours_left == 112);
  n = battFormat(v, lines, 16);
  CHECK(any(lines, n, "about 5 days left"));
  // The floor is the node's own: a higher floor leaves less time and a lower percent
  BattView higher = battAnalyse(log, NOW_HOUR + 12, 0, true, 3300);
  CHECK(higher.hours_left < v.hours_left && higher.percent < v.percent);

  // Low in charge terms: 3,600 mV losing 50 a day has little more than a day
  memset(&log, 0, sizeof(log));
  battSample(log, NOW_HOUR - 24, 0, 3650);
  for (int i = 1; i < 24; i += 4) battSample(log, NOW_HOUR - 24 + i, 0, (uint16_t)(3650 - i * 2));
  battSample(log, NOW_HOUR, 0, 3600);
  v = battAnalyse(log, NOW_HOUR, 0, true, 2800);
  CHECK(v.hours_left > 24 && v.hours_left < 36);

  // A part day gives no yesterday, so no time left
  memset(&log, 0, sizeof(log));
  battSample(log, NOW_HOUR - 3, 0, 3900);
  battSample(log, NOW_HOUR, 0, 3850);
  CHECK(!log.have_yesterday);
  v = battAnalyse(log, NOW_HOUR, 0, true, 2800);
  CHECK(v.hours_left == 0);

  // No loss over the two days: no time left
  memset(&log, 0, sizeof(log));
  battSample(log, NOW_HOUR - 24, 0, 3800);
  battSample(log, NOW_HOUR, 0, 3900);
  v = battAnalyse(log, NOW_HOUR, 0, true, 2800);
  CHECK(log.have_yesterday && v.hours_left == 0);

  // Near empty and falling fast
  ramp(log, 3700, -8, 24);
  v = battAnalyse(log, NOW_HOUR, 0, true, 2800);
  CHECK(v.kind == BATT_K_LOW && v.level == DIAG_PROBLEM);
  n = battFormat(v, lines, 16);
  CHECK(any(lines, n, "🔴 low") && any(lines, n, "Charge it now"));

  // A board without a measured divider gets the trend only
  ramp(log, 4000, -5, 24);
  v = battAnalyse(log, NOW_HOUR, 0, false, 2800);
  CHECK(v.kind == BATT_K_DRAINING && v.percent == -1 && v.hours_left == 0 && !v.estimates);
  n = battFormat(v, lines, 16);
  CHECK(any(lines, n, "trend only") && !any(lines, n, "left"));

  // A reading outside one lithium cell is treated the same
  ramp(log, 8778, 0, 24);
  v = battAnalyse(log, NOW_HOUR, 0, true, 2800);
  CHECK(!v.estimates && v.percent == -1 && v.kind == BATT_K_STEADY);

  // Gaps show as spaces, the scale follows the day, and the time zone moves the hours
  memset(&log, 0, sizeof(log));
  battSample(log, NOW_HOUR - 2, 0, 3800);
  battSample(log, NOW_HOUR, 0, 4000);
  v = battAnalyse(log, NOW_HOUR, -5, true, 2800);
  CHECK(v.lo_mv == 3800 && v.hi_mv == 4000 && v.lo_hour == 17 && v.hi_hour == 19);
  CHECK(v.chart[21] == 3800 && v.chart[23] == 4000 && v.chart[22] == 0 && v.chart_start == 20);
  n = battFormat(v, lines, 16);
  CHECK(any(lines, n, "▁ █") && any(lines, n, "Low 3.80 V at 5pm, high 4.00 V at 7pm"));

  // A flat day stays level, and a rise and a fall reach the bottom and top bars
  ramp(log, 3900, 0, 24);
  v = battAnalyse(log, NOW_HOUR, 0, true, 2800);
  n = battFormat(v, lines, 16);
  CHECK(any(lines, n, "24h ▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄"));
  memset(&log, 0, sizeof(log));
  for (int age = 2; age >= 0; age--) battSample(log, NOW_HOUR - age, 0, age == 1 ? 4000 : 3800);
  v = battAnalyse(log, NOW_HOUR, 0, true, 2800);
  n = battFormat(v, lines, 16);
  CHECK(any(lines, n, "▁█▁"));

  // Low and high come first, so the bars and their hours stay together in one message
  CHECK(strstr(lines[2], "Low ") && strstr(lines[3], "24h "));

  // The full report fits the firmware's line limit
  CHECK(n <= 8);

  // The current hour, only just begun, does not dilute the night drain
  memset(&log, 0, sizeof(log));
  for (int age = 3; age >= 1; age--) {
    battSample(log, NOW_HOUR - age, 0, (uint16_t)(3900 - age * 10));
    battSample(log, NOW_HOUR - age, 0, (uint16_t)(3900 - age * 10 - 6));
  }
  battSample(log, NOW_HOUR, 0, 3880);
  v = battAnalyse(log, NOW_HOUR, 0, true, 2800);
  CHECK(v.night_drain == 6);

  // Readings older than a day are ignored
  memset(&log, 0, sizeof(log));
  battSample(log, NOW_HOUR - 24, 0, 3900);
  v = battAnalyse(log, NOW_HOUR, 0, true, 2800);
  CHECK(v.hours == 0);

  printf(failures ? "%d FAILED\n" : "OK\n", failures);
  return failures ? 1 : 0;
}
