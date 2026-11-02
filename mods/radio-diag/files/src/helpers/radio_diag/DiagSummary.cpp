#include <helpers/radio_diag/DiagSummary.h>

#include <stdio.h>
#include <string.h>

#define ERRORS_MIN      50
#define ERRORS_CAUTION  2
#define ERRORS_PROBLEM  10
#define LOAD_MIN_S      600
#define LOAD_CAUTION    25
#define LOAD_PROBLEM    50

DiagArea* diagAdd(DiagSummary& s, const char* icon, const char* name) {
  if (s.count >= DIAG_AREAS_MAX) return nullptr;
  DiagArea* a = &s.areas[s.count++];
  memset(a, 0, sizeof(*a));
  a->icon = icon;
  a->name = name;
  a->level = DIAG_UNKNOWN;
  return a;
}

int diagErrors(uint32_t received, uint32_t errors, char* out, size_t len) {
  uint32_t total = received + errors;
  if (total < ERRORS_MIN) {
    snprintf(out, len, "%lu of %d pkts", (unsigned long)total, ERRORS_MIN);
    return DIAG_UNKNOWN;
  }
  uint32_t tenths = (uint32_t)((uint64_t)errors * 1000 / total);
  snprintf(out, len, "%lu.%lu%% failed", (unsigned long)(tenths / 10), (unsigned long)(tenths % 10));
  if (tenths >= ERRORS_PROBLEM * 10) return DIAG_PROBLEM;
  if (tenths >= ERRORS_CAUTION * 10) return DIAG_CAUTION;
  return DIAG_GOOD;
}

int diagLoad(uint32_t air_s, uint32_t uptime_s, char* out, size_t len) {
  if (uptime_s < LOAD_MIN_S) {
    snprintf(out, len, "%lu of %d min up", (unsigned long)(uptime_s / 60), LOAD_MIN_S / 60);
    return DIAG_UNKNOWN;
  }
  int pct = (int)((uint64_t)air_s * 100 / uptime_s);
  snprintf(out, len, "channel %d%% busy", pct);
  if (pct >= LOAD_PROBLEM) return DIAG_PROBLEM;
  if (pct >= LOAD_CAUTION) return DIAG_CAUTION;
  return DIAG_GOOD;
}

static const char* orb(uint8_t level) {
  switch (level) {
    case DIAG_GOOD: return "\U0001F7E2";
    case DIAG_CAUTION: return "\U0001F7E1";
    case DIAG_PROBLEM: return "\U0001F534";
    default: return "⚪";
  }
}

#define ADD(...) do { if (n < max_lines) snprintf(lines[n++], DIAG_LINE, __VA_ARGS__); } while (0)

int diagFormat(const DiagSummary& s, char lines[][DIAG_LINE], int max_lines) {
  int best = -1, unknown = 0;
  for (int i = 0; i < s.count; i++) {
    const DiagArea& a = s.areas[i];
    if (a.level == DIAG_UNKNOWN && a.learning) unknown++;
    if (a.level != DIAG_CAUTION && a.level != DIAG_PROBLEM) continue;
    if (best < 0) {
      best = i;
      continue;
    }
    const DiagArea& b = s.areas[best];
    if (a.level > b.level || (a.level == b.level && a.rank < b.rank)) best = i;
  }

  int n = 0;
  if (best >= 0) {
    ADD("\U0001FA7A Diag:    %s %s", orb(s.areas[best].level), s.areas[best].title);
  } else if (unknown > 0) {
    unsigned long h = s.uptime_s / 3600;
    ADD("\U0001FA7A Diag:    ⚪ still learning (%luh up)", h);
  } else {
    ADD("\U0001FA7A Diag:    \U0001F7E2 healthy repeater");
  }
  for (int i = 0; i < s.count; i++) {
    const DiagArea& a = s.areas[i];
    ADD("%s %-8s %s %s", a.icon, a.name, orb(a.level), a.text);
  }
  if (best >= 0) {
    const DiagArea& a = s.areas[best];
    ADD("Best fix: %s", a.fix[0]);
    if (a.fix[1][0]) ADD("%s", a.fix[1]);
    if (a.see) ADD("See: %s", a.see);
  } else if (unknown > 0) {
    ADD("Check again in a day.");
  } else {
    ADD("Nothing to fix.");
  }
  return n;
}
