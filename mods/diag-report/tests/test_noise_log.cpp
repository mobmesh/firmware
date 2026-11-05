#include <helpers/diag_report/DiagSummary.h>
#include <helpers/diag_report/NoiseLog.h>

#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

#define NOW_HOUR 500016U               // a multiple of 24, so local hour = slot at tz 0
#define LNA      1

static bool any(char lines[][NOISE_LINE], int n, const char* text) {
  for (int i = 0; i < n; i++) if (strstr(lines[i], text)) return true;
  return false;
}

static void fill(NoiseLog& log, int8_t floor_dbm, uint16_t recv, uint16_t errors) {
  memset(&log, 0, sizeof(log));
  for (int l = 0; l < 24; l++) noiseAddHour(log, {NOW_HOUR - 24 + l, floor_dbm, LNA, recv, errors, 1});
}

static void set(NoiseLog& log, int from, int to, int8_t floor_dbm, uint16_t recv, uint16_t errors) {
  for (int l = from; l < to; l++) noiseAddHour(log, {NOW_HOUR - 24 + l, floor_dbm, LNA, recv, errors, 1});
}

static int report(const NoiseView& v, char lines[][NOISE_LINE]) {
  int n = noiseFormat(v, "C5FB", lines, 16);
  for (int i = 0; i < n; i++) CHECK(strlen(lines[i]) < NOISE_LINE - 1);
  return n;
}

int main() {
  NoiseLog log;
  NoiseView v;
  char lines[16][NOISE_LINE], text[48], t[8];
  int n;

  noiseHourText(0, false, t, sizeof(t));
  CHECK(strcmp(t, "12am") == 0);
  noiseHourText(17, false, t, sizeof(t));
  CHECK(strcmp(t, "5pm") == 0);
  noiseHourText(12, true, t, sizeof(t));
  CHECK(strcmp(t, "12p") == 0);

  memset(&log, 0, sizeof(log));
  v = noiseAnalyse(log, NOW_HOUR, 0, LNA, NOISE_NO_MARGIN);
  CHECK(v.hours == 0 && v.kind == NOISE_K_NONE && v.level == DIAG_UNKNOWN);
  n = report(v, lines);
  CHECK(any(lines, n, "Watching, 0 of 24h"));

  set(log, 20, 23, -112, 100, 1);
  v = noiseAnalyse(log, NOW_HOUR, 0, LNA, NOISE_NO_MARGIN);
  CHECK(v.hours == 3 && v.level == DIAG_UNKNOWN);
  CHECK(noiseSummary(v, text, sizeof(text)) == DIAG_UNKNOWN && strcmp(text, "watching, 3 of 24h") == 0);

  // Quiet site
  fill(log, -112, 100, 1);
  set(log, 9, 12, -111, 100, 1);
  v = noiseAnalyse(log, NOW_HOUR, 0, LNA, 10);
  CHECK(v.hours == 24 && v.kind == NOISE_K_QUIET && v.level == DIAG_GOOD && v.floor_dbm == -112);
  n = report(v, lines);
  CHECK(strstr(lines[0], "Quiet Site") != nullptr);
  CHECK(any(lines, n, "Nothing to fix."));
  CHECK(!any(lines, n, "err"));
  CHECK(noiseSummary(v, text, sizeof(text)) == DIAG_GOOD && strcmp(text, "quiet, floor -112") == 0);

  // Loud 8am-5pm, decodes fail with it
  fill(log, -112, 100, 1);
  set(log, 8, 17, -105, 100, 10);
  v = noiseAnalyse(log, NOW_HOUR, 0, LNA, 10);
  CHECK(v.kind == NOISE_K_LOUD_LOSS && v.level == DIAG_PROBLEM && v.max_rise == 7);
  CHECK(v.loud_start == 8 && v.loud_len == 9 && v.floor_start == 17 && v.floor_len == 15);
  n = report(v, lines);
  CHECK(strstr(lines[0], "Loud 8am-5pm, Losing Packets") != nullptr);
  CHECK(any(lines, n, "Floor -112 (5pm-8am). Not mesh traffic."));
  CHECK(strstr(lines[2], "12a-4a  +0 dB") != nullptr && strstr(lines[2], "err") == nullptr);
  CHECK(strstr(lines[4], "8a-12p  +7 dB  9% err") != nullptr);
  CHECK(strstr(lines[6], "4p-8p   +0 to +7 dB") != nullptr);
  CHECK(any(lines, n, "Links keep 3+ dB margin through it."));
  CHECK(any(lines, n, "Try: diag radio.rfscan at 8am to find it."));
  CHECK(noiseSummary(v, text, sizeof(text)) == DIAG_PROBLEM && strcmp(text, "+7 dB 8am-5pm, losing packets") == 0);

  // Same noise, decodes unaffected
  set(log, 8, 17, -105, 100, 1);
  v = noiseAnalyse(log, NOW_HOUR, 0, LNA, 10);
  CHECK(v.kind == NOISE_K_LOUD_OK && v.level == DIAG_CAUTION);
  n = report(v, lines);
  CHECK(strstr(lines[0], "Loud 8am-5pm, No Loss") != nullptr);
  CHECK(!any(lines, n, "err"));

  // Same noise, deeper than the weakest link's margin
  v = noiseAnalyse(log, NOW_HOUR, 0, LNA, 4);
  CHECK(v.kind == NOISE_K_LOUD_DROP && v.level == DIAG_PROBLEM && v.drops);
  n = report(v, lines);
  CHECK(any(lines, n, "C5FB drops below its limit 8am-5pm."));

  // No neighbour judged: no margin line
  v = noiseAnalyse(log, NOW_HOUR, 0, LNA, NOISE_NO_MARGIN);
  n = report(v, lines);
  CHECK(v.kind == NOISE_K_LOUD_OK && !any(lines, n, "margin") && !any(lines, n, "drops"));

  // Busy mesh, not an outside source
  set(log, 8, 17, -105, 400, 4);
  v = noiseAnalyse(log, NOW_HOUR, 0, LNA, 10);
  n = report(v, lines);
  CHECK(any(lines, n, "Busy mesh hours."));

  // Failing decodes without a noise rise
  fill(log, -112, 100, 1);
  set(log, 16, 20, -112, 100, 7);
  v = noiseAnalyse(log, NOW_HOUR, 0, LNA, 10);
  CHECK(v.kind == NOISE_K_ERRORS && v.level == DIAG_CAUTION && v.err_start == 16);
  n = report(v, lines);
  CHECK(strstr(lines[0], "Errors, Not Noise") != nullptr);
  CHECK(strstr(lines[6], "4p-8p   +0 dB  7% err") != nullptr);
  CHECK(any(lines, n, "Try: diag radio.rfscan at 4pm."));
  CHECK(noiseSummary(v, text, sizeof(text)) == DIAG_CAUTION && strcmp(text, "errors from 4pm, no noise rise") == 0);

  // Too few packets: no error verdict either way
  fill(log, -112, 2, 0);
  set(log, 16, 20, -112, 2, 2);
  v = noiseAnalyse(log, NOW_HOUR, 0, LNA, 10);
  CHECK(v.kind == NOISE_K_QUIET);

  // Hours in the other LNA state, or older than a day, do not count
  fill(log, -112, 100, 1);
  for (int l = 8; l < 17; l++) noiseAddHour(log, {NOW_HOUR - 24 + l, -90, 0, 100, 1, 1});
  v = noiseAnalyse(log, NOW_HOUR, 0, LNA, 10);
  CHECK(v.hours == 15 && v.kind == NOISE_K_QUIET);
  n = report(v, lines);
  CHECK(strstr(lines[4], "8a-12p  no data") != nullptr && strstr(lines[4], "⚪") != nullptr);
  CHECK(any(lines, n, "15 of 24h watched so far."));
  v = noiseAnalyse(log, NOW_HOUR + 30, 0, LNA, 10);
  CHECK(v.hours == 0);

  // Stored hours are UTC; the report is local
  fill(log, -112, 100, 1);
  set(log, 13, 22, -105, 100, 1);
  v = noiseAnalyse(log, NOW_HOUR, -5, LNA, 10);
  CHECK(v.loud_start == 8 && v.loud_len == 9);

  // Scattered bursts
  fill(log, -112, 100, 1);
  set(log, 2, 3, -100, 100, 1);
  set(log, 10, 11, -100, 100, 1);
  set(log, 19, 20, -100, 100, 1);
  v = noiseAnalyse(log, NOW_HOUR, 0, LNA, 20);
  CHECK(v.scattered);
  n = report(v, lines);
  CHECK(strstr(lines[0], "Loud At Times") != nullptr);

  uint32_t recv, errors;
  fill(log, -112, 100, 1);
  log.h[3].counts_valid = 0;
  CHECK(noiseCounts(log, NOW_HOUR, &recv, &errors) && recv == 2300 && errors == 23);
  memset(&log, 0, sizeof(log));
  CHECK(!noiseCounts(log, NOW_HOUR, &recv, &errors));

  printf(failures ? "%d FAILED\n" : "OK\n", failures);
  return failures ? 1 : 0;
}
