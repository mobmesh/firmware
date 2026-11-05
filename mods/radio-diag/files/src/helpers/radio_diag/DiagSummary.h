#pragma once

#include <stddef.h>
#include <stdint.h>

// Pure diag-summary text: one line per area, the worst sets the header, one best fix. No radio, clock or storage.

#define DIAG_LINE      72
#define DIAG_AREAS_MAX 9
#define DIAG_GOOD      0
#define DIAG_CAUTION   1
#define DIAG_PROBLEM   2
#define DIAG_UNKNOWN   3

struct DiagArea {
  const char* icon;
  const char* name;
  uint8_t level;
  char text[48];
  uint8_t rank;                        // fix priority, causes before symptoms; lower wins
  const char* title;                   // header words when this area sets it
  char fix[2][48];
  const char* see;                     // detail command, or nullptr
  bool learning;                       // unknown only until more traffic or uptime arrives
};

struct DiagSummary {
  DiagArea areas[DIAG_AREAS_MAX];
  uint8_t count;
  uint32_t uptime_s;
};

DiagArea* diagAdd(DiagSummary& s, const char* icon, const char* name);
int diagErrors(uint32_t received, uint32_t errors, char* out, size_t len);
int diagLoad(uint32_t air_s, uint32_t uptime_s, char* out, size_t len);
int diagFormat(const DiagSummary& s, char lines[][DIAG_LINE], int max_lines);
