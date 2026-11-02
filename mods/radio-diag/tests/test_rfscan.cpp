#include <helpers/radio_diag/RfScan.h>

#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

static RfScanData quiet() {
  RfScanData d;
  d.centre = 910.525f;
  d.lna = 1;
  for (int b = 0; b < RFSCAN_BAND_STEPS; b++) d.band[b] = -70;
  for (int sw = 0; sw < RFSCAN_SWEEPS; sw++)
    for (int b = 0; b < RFSCAN_STEPS; b++) d.raw[sw][b] = -70;
  return d;
}

static bool any(char lines[][RFSCAN_LINE], int n, const char* text) {
  for (int i = 0; i < n; i++) if (strstr(lines[i], text)) return true;
  return false;
}

int main() {
  char lines[20][RFSCAN_LINE];

  RfScanData d = quiet();
  int n = rfScanFormat(d, lines, 20);
  CHECK(strstr(lines[0], "fl-70 LNA on") != nullptr);
  CHECK(n == 2 + 9 + 1);
  CHECK(any(lines, n, "nearby band clear"));
  CHECK(any(lines, n, "910 ⚫ · ◂mesh"));

  d = quiet();
  for (int sw = 0; sw < RFSCAN_SWEEPS; sw++) d.raw[sw][24] = -58;
  n = rfScanFormat(d, lines, 20);
  CHECK(any(lines, n, "steady 912.53 (+2.00) 12 dB"));

  d = quiet();
  for (int sw = 0; sw < 4; sw++) d.raw[sw][4] = -50;
  n = rfScanFormat(d, lines, 20);
  CHECK(any(lines, n, "bursts 907.53 (-3.00) 4/6"));

  d = quiet();
  d.lna = RFSCAN_LNA_NA;
  n = rfScanFormat(d, lines, 20);
  CHECK(strstr(lines[0], "LNA n/a") != nullptr);
  for (int i = 0; i < n; i++) CHECK(strlen(lines[i]) < RFSCAN_LINE);

  printf(failures ? "%d FAILED\n" : "OK\n", failures);
  return failures ? 1 : 0;
}
