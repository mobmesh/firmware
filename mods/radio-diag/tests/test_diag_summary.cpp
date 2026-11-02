#include <helpers/radio_diag/DiagSummary.h>
#include <helpers/radio_diag/LinkReport.h>
#include <helpers/radio_diag/RfScan.h>

#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

#define NOW 1000000UL

static bool any(char lines[][DIAG_LINE], int n, const char* text) {
  for (int i = 0; i < n; i++) if (strstr(lines[i], text)) return true;
  return false;
}

static DiagArea* area(DiagSummary& s, const char* name, uint8_t level, uint8_t rank, const char* title) {
  DiagArea* a = diagAdd(s, "*", name);
  a->level = level;
  a->rank = rank;
  a->title = title;
  snprintf(a->text, sizeof(a->text), "%s text", name);
  snprintf(a->fix[0], sizeof(a->fix[0]), "%s fix", name);
  return a;
}

int main() {
  char text[48], lines[16][DIAG_LINE];

  CHECK(diagErrors(10, 1, text, sizeof(text)) == DIAG_UNKNOWN);
  CHECK(diagErrors(990, 10, text, sizeof(text)) == DIAG_GOOD && strcmp(text, "1.0% failed decodes") == 0);
  CHECK(diagErrors(950, 50, text, sizeof(text)) == DIAG_CAUTION);
  CHECK(diagErrors(800, 200, text, sizeof(text)) == DIAG_PROBLEM);
  CHECK(diagLoad(10, 300, text, sizeof(text)) == DIAG_UNKNOWN);
  CHECK(diagLoad(60, 1000, text, sizeof(text)) == DIAG_GOOD && strcmp(text, "channel 6% busy") == 0);
  CHECK(diagLoad(600, 1000, text, sizeof(text)) == DIAG_PROBLEM);

  DiagSummary s;
  memset(&s, 0, sizeof(s));
  area(s, "Links", DIAG_GOOD, 5, "weak link");
  area(s, "Clock", DIAG_GOOD, 10, "clock off");
  int n = diagFormat(s, lines, 16);
  CHECK(any(lines, n, "healthy repeater"));
  CHECK(any(lines, n, "Nothing to fix."));

  area(s, "Errors", DIAG_UNKNOWN, 7, "decode errors")->learning = true;
  s.uptime_s = 7200;
  n = diagFormat(s, lines, 16);
  CHECK(any(lines, n, "still learning (2h up)"));

  memset(&s, 0, sizeof(s));
  area(s, "LNA", DIAG_UNKNOWN, 3, "LNA");
  n = diagFormat(s, lines, 16);
  CHECK(any(lines, n, "healthy repeater"));      // no study is not learning

  memset(&s, 0, sizeof(s));
  area(s, "Clock", DIAG_CAUTION, 10, "clock off");
  area(s, "Links", DIAG_CAUTION, 5, "weak link");
  n = diagFormat(s, lines, 16);
  CHECK(strstr(lines[0], "weak link") != nullptr);
  CHECK(any(lines, n, "Best fix: Links fix"));
  area(s, "Errors", DIAG_PROBLEM, 7, "decode errors")->see = "diag rfscan";
  n = diagFormat(s, lines, 16);
  CHECK(strstr(lines[0], "decode errors") != nullptr);   // a problem outranks any caution
  CHECK(any(lines, n, "See: diag rfscan"));

  LinkTable t;
  memset(&t, 0, sizeof(t));
  CHECK(linkPathsSummary(t, NOW, text, sizeof(text)) == 3);
  int a = linkIndex(t, 0xC5, 1, NOW), b = linkIndex(t, 0x7A, 1, NOW);
  for (int k = 0; k < 18; k++) linkAddPacket(t, a, 5, NOW);
  for (int k = 0; k < 2; k++) linkAddPacket(t, b, 5, NOW);
  CHECK(linkPathsSummary(t, NOW, text, sizeof(text)) == 1 && strcmp(text, "90% via C5") == 0);
  for (int k = 0; k < 10; k++) linkAddPacket(t, b, 5, NOW);
  CHECK(linkPathsSummary(t, NOW, text, sizeof(text)) == 0);
  CHECK(linkSummary(t, 7, NOW, text, sizeof(text)) == 0 && strcmp(text, "2 solid") == 0);
  CHECK(linkClockSummary(t, NOW, text, sizeof(text)) == 3);

  RfScanData d;
  d.centre = 910.525f;
  d.lna = 1;
  for (int i = 0; i < RFSCAN_BAND_STEPS; i++) d.band[i] = -70;
  for (int sw = 0; sw < RFSCAN_SWEEPS; sw++)
    for (int i = 0; i < RFSCAN_STEPS; i++) d.raw[sw][i] = -70;
  CHECK(rfScanSummary(d, text, sizeof(text)) == 0 && strcmp(text, "band clear") == 0);
  for (int sw = 0; sw < RFSCAN_SWEEPS; sw++) d.raw[sw][24] = -58;
  CHECK(rfScanSummary(d, text, sizeof(text)) == 0 && strcmp(text, "off-channel steady 912.53 12 dB") == 0);
  for (int sw = 0; sw < RFSCAN_SWEEPS; sw++) d.raw[sw][15] = -58;
  CHECK(rfScanSummary(d, text, sizeof(text)) == 1 && strcmp(text, "steady 910.28 (-0.25) 12 dB") == 0);
  for (int sw = 0; sw < RFSCAN_SWEEPS; sw++) d.raw[sw][16] = -62;
  CHECK(rfScanSummary(d, text, sizeof(text)) == 2 && strcmp(text, "mesh channel steady 8 dB") == 0);

  for (int i = 0; i < n; i++) CHECK(strlen(lines[i]) < DIAG_LINE);
  printf(failures ? "%d FAILED\n" : "OK\n", failures);
  return failures ? 1 : 0;
}
