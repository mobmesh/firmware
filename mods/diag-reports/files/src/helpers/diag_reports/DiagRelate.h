#pragma once

#include <helpers/diag_reports/DiagEvidence.h>

// Each report's interpretation, written from all the evidence rather than the report's own data alone. Pure.

void relateLinks(const DiagEvidence& e, const LinkTable& t, uint8_t sf, uint32_t now_ms, DiagNote& n);
void relateNoise(const DiagEvidence& e, const LinkTable& t, const NoiseView& v, DiagNote& n);
void relateScan(const DiagEvidence& e, DiagNote& n);
// Where a loud source likely is, from how strong, how steady and when: one line, or empty.
void relateSourceHint(const DiagEvidence& e, int8_t max_rise, char* out, size_t len);
void relateLna(const DiagEvidence& e, DiagNote& n);
void relateBatt(const DiagEvidence& e, const DiagStore& s, const BattView& v, DiagNote& n);
void relateHist(const DiagStore& s, const HistView& v, DiagNote& n);
void relateTemp(const DiagEvidence& e, const DiagStore& s, const TempView& v, DiagNote& n);

// Battery loss over the 30-day ring: how much worse a busy or cold day is than a quiet or warm one, mV per day; 0 = not shown.
int relateDayDrain(const DiagStore& s, bool by_cold);
