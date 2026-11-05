#include <helpers/diag_report/BattLog.h>
#include <helpers/diag_report/DiagSummary.h>

#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

#define NOW_HOUR 500016U               // a multiple of 24, so local hour = slot at tz 0

static bool any(char lines[][BATT_LINE], int n, const char* text) {
  for (int i = 0; i < n; i++) if (strstr(lines[i], text)) return true;
  return false;
}

// One whole local day: sun from 9am to 3pm, ending lower or higher than it began.
static void wholeDay(BattLog& log, uint32_t start, int from, int to, int hours = 24) {
  for (int h = 0; h < hours; h++) {
    uint16_t base = (uint16_t)(from + (to - from) * h / 24);
    bool sun = h >= 9 && h < 15;
    battSample(log, start + h, 0, base);
    battSample(log, start + h, 0, (uint16_t)(sun ? base + 40 : base - 2));
  }
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
  BattView v = battAnalyse(log, NOW_HOUR, 0, true);
  CHECK(v.hours == 0 && v.level == DIAG_UNKNOWN);
  int n = battFormat(v, lines, 16);
  CHECK(n == 1 && any(lines, n, "Watching, 0 of 24h"));

  // A falling night is not a verdict until a whole cycle is in
  ramp(log, 3950, -5, 8);
  v = battAnalyse(log, NOW_HOUR, 0, true);
  CHECK(v.kind == BATT_K_WATCHING && v.level == DIAG_UNKNOWN);
  n = battFormat(v, lines, 16);
  CHECK(any(lines, n, "Watching, 8 of 24h") && any(lines, n, "Verdict After 20 Hours"));

  // But a low cell is said at once, at the fixed voltage
  ramp(log, 3352, -1, 3);
  v = battAnalyse(log, NOW_HOUR, 0, true);
  CHECK(v.now_mv == 3349 && v.kind == BATT_K_LOW && v.level == DIAG_PROBLEM);
  n = battFormat(v, lines, 16);
  CHECK(any(lines, n, "🔴 Low") && any(lines, n, "Charge Now: it may stop soon"));
  ramp(log, 3354, -1, 3);
  v = battAnalyse(log, NOW_HOUR, 0, true);
  CHECK(v.now_mv == 3351 && v.kind == BATT_K_WATCHING);

  // Steady, full day
  ramp(log, 3950, 0, 24);
  v = battAnalyse(log, NOW_HOUR, 0, true);
  CHECK(v.kind == BATT_K_STEADY && v.level == DIAG_GOOD && v.charge_hours == 0);
  n = battFormat(v, lines, 16);
  CHECK(any(lines, n, "Steady") && any(lines, n, "Nothing to Fix"));

  // Rising
  ramp(log, 3700, 10, 24);
  v = battAnalyse(log, NOW_HOUR, 0, true);
  CHECK(v.kind == BATT_K_CHARGING && v.level == DIAG_GOOD);

  // Falling with no charging at all
  ramp(log, 4000, -5, 24);
  v = battAnalyse(log, NOW_HOUR, 0, true);
  CHECK(v.kind == BATT_K_DRAINING && v.level == DIAG_CAUTION);
  n = battFormat(v, lines, 16);
  CHECK(any(lines, n, "Draining") && any(lines, n, "Charge It: or add a panel"));

  // A solar day that charges but ends lower is only a net loss
  memset(&log, 0, sizeof(log));
  for (int age = 23; age >= 0; age--) {
    int local = (int)((NOW_HOUR - age) % 24);
    bool sun = local >= 9 && local < 15;
    uint16_t base = (uint16_t)(3950 - (23 - age) * 6);
    battSample(log, NOW_HOUR - age, 0, base);
    battSample(log, NOW_HOUR - age, 0, (uint16_t)(sun ? base + 40 : base - 5));
  }
  v = battAnalyse(log, NOW_HOUR, 0, true);
  CHECK(v.kind == BATT_K_NET_LOSS && v.level == DIAG_CAUTION);
  CHECK(v.charge_hours == 6 && v.night_drain == 5 && v.net_mv < -30);
  n = battFormat(v, lines, 16);
  CHECK(any(lines, n, "🟡 Net Loss Today") && any(lines, n, "Watch It: may just be low sun day."));
  CHECK(any(lines, n, "Night drain 5 mV/h, charged 6 hours"));
  CHECK(any(lines, n, "6a    12p   6p    12a") && any(lines, n, "Net -"));
  CHECK(!any(lines, n, "%") && !any(lines, n, " left"));
  char text[48];
  CHECK(battSummary(v, text, sizeof(text)) == DIAG_CAUTION && strstr(text, "net loss today") != nullptr);

  // Two whole days falling, and the last 24 hours too, is a pattern
  memset(&log, 0, sizeof(log));
  wholeDay(log, NOW_HOUR - 48, 3950, 3900);
  wholeDay(log, NOW_HOUR - 24, 3900, 3850);
  wholeDay(log, NOW_HOUR, 3850, 3800, 21);
  CHECK(log.full_days == 2 && log.before_mv == 3950 && log.yesterday_mv == 3900 && log.day_mv == 3850);
  v = battAnalyse(log, NOW_HOUR + 20, 0, true);
  CHECK(v.kind == BATT_K_NOT_RECOVERING && v.level == DIAG_CAUTION);
  n = battFormat(v, lines, 16);
  CHECK(n == 8 && strstr(lines[0], "🟡 Not Recovering Daily") && strstr(lines[1], "Now 3.81 V"));
  CHECK(strstr(lines[3], "12") && strstr(lines[n - 1], "➜ Check the Panel: it isn't refilling each day"));
  // It fits two messages with the bars and their hours together in the first
  size_t first = strlen(lines[0]) + strlen(lines[1]) + strlen(lines[2]) + strlen(lines[3]) + 3;
  size_t second = strlen(lines[4]) + strlen(lines[5]) + strlen(lines[6]) + strlen(lines[7]) + 3;
  CHECK(first <= 155 && second <= 155);

  // One bad day, with a flat day before it, is the weather: the windows overlap, so yesterday alone proves nothing
  memset(&log, 0, sizeof(log));
  wholeDay(log, NOW_HOUR - 48, 3950, 3950);
  wholeDay(log, NOW_HOUR - 24, 3950, 3900);
  v = battAnalyse(log, NOW_HOUR - 1, 0, true);
  CHECK(v.kind == BATT_K_NET_LOSS);
  wholeDay(log, NOW_HOUR, 3900, 3850, 9);
  v = battAnalyse(log, NOW_HOUR + 8, 0, true);
  CHECK(log.full_days == 2 && v.kind == BATT_K_NET_LOSS);

  // A single recorded day cannot be a pattern, and a missing day starts the count again
  memset(&log, 0, sizeof(log));
  wholeDay(log, NOW_HOUR - 24, 3900, 3850);
  wholeDay(log, NOW_HOUR, 3850, 3800, 21);
  CHECK(log.full_days == 1);
  CHECK(battAnalyse(log, NOW_HOUR + 20, 0, true).kind == BATT_K_NET_LOSS);
  memset(&log, 0, sizeof(log));
  wholeDay(log, NOW_HOUR - 96, 3950, 3900);
  wholeDay(log, NOW_HOUR - 24, 3900, 3850);
  wholeDay(log, NOW_HOUR, 3850, 3800, 21);
  CHECK(log.full_days == 1);

  // A part day is not a whole day
  memset(&log, 0, sizeof(log));
  battSample(log, NOW_HOUR - 3, 0, 3900);
  battSample(log, NOW_HOUR, 0, 3850);
  CHECK(log.full_days == 0);

  // A board without a measured divider gets no low warning, only the trend
  ramp(log, 3300, -5, 24);
  v = battAnalyse(log, NOW_HOUR, 0, false);
  CHECK(v.kind == BATT_K_DRAINING);

  // A reading outside one lithium cell is not low
  ramp(log, 2000, 0, 24);
  v = battAnalyse(log, NOW_HOUR, 0, true);
  CHECK(v.kind == BATT_K_STEADY);
  ramp(log, 8778, 0, 24);
  v = battAnalyse(log, NOW_HOUR, 0, true);
  CHECK(v.kind == BATT_K_STEADY);

  // Gaps show as spaces, the scale follows the day, and the time zone moves the hours
  memset(&log, 0, sizeof(log));
  battSample(log, NOW_HOUR - 2, 0, 3800);
  battSample(log, NOW_HOUR, 0, 4000);
  v = battAnalyse(log, NOW_HOUR, -5, true);
  CHECK(v.lo_mv == 3800 && v.hi_mv == 4000 && v.lo_hour == 17 && v.hi_hour == 19);
  CHECK(v.chart[21] == 3800 && v.chart[23] == 4000 && v.chart[22] == 0 && v.chart_start == 20);
  n = battFormat(v, lines, 16);
  CHECK(any(lines, n, "▁ █") && any(lines, n, "Low 3.80 V at 5pm, high 4.00 V at 7pm"));

  // A flat day stays level, and a rise and a fall reach the bottom and top bars
  ramp(log, 3900, 0, 24);
  v = battAnalyse(log, NOW_HOUR, 0, true);
  n = battFormat(v, lines, 16);
  CHECK(any(lines, n, "▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄"));
  memset(&log, 0, sizeof(log));
  for (int age = 2; age >= 0; age--) battSample(log, NOW_HOUR - age, 0, age == 1 ? 4000 : 3800);
  v = battAnalyse(log, NOW_HOUR, 0, true);
  n = battFormat(v, lines, 16);
  CHECK(any(lines, n, "▁█▁"));

  // The full report fits the firmware's line limit
  CHECK(n <= 8);

  // The current hour, only just begun, does not dilute the night drain
  memset(&log, 0, sizeof(log));
  for (int age = 3; age >= 1; age--) {
    battSample(log, NOW_HOUR - age, 0, (uint16_t)(3900 - age * 10));
    battSample(log, NOW_HOUR - age, 0, (uint16_t)(3900 - age * 10 - 6));
  }
  battSample(log, NOW_HOUR, 0, 3880);
  v = battAnalyse(log, NOW_HOUR, 0, true);
  CHECK(v.night_drain == 6);

  // Readings older than a day are ignored
  memset(&log, 0, sizeof(log));
  battSample(log, NOW_HOUR - 24, 0, 3900);
  v = battAnalyse(log, NOW_HOUR, 0, true);
  CHECK(v.hours == 0);

  printf(failures ? "%d FAILED\n" : "OK\n", failures);
  return failures ? 1 : 0;
}
