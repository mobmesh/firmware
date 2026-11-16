#pragma once

#include <stddef.h>
#include <stdint.h>

// One shared output arena for every diag report, plus the text helpers they share. Pure: no radio, clock or storage.

#define DIAG_GOOD      0
#define DIAG_CAUTION   1
#define DIAG_PROBLEM   2
#define DIAG_UNKNOWN   3

#define DIAG_OUT_SIZE  1100
#define DIAG_ROW_MAX   155             // text that fits one reply datagram
#define DIAG_ROWS_MAX  6               // replies one command may send

struct DiagOut {
  char buf[DIAG_OUT_SIZE];             // lines joined by '\n'
  uint16_t len;
  uint8_t lines;
  bool truncated;                      // a line did not fit and was dropped
};

void outReset(DiagOut& o);
void outLine(DiagOut& o, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
void outSep(DiagOut& o);               // the "~" line between measurements and interpretation
// Copies the next row of whole lines into `row` and advances `pos`; false when nothing is left.
bool outRow(const DiagOut& o, uint16_t* pos, char row[DIAG_ROW_MAX + 1]);
int outRowCount(const DiagOut& o);
// Line `index` copied out, for tests and callers that need one line; false past the end.
bool outGetLine(const DiagOut& o, int index, char* out, size_t len);

// Interpretation lines that replace a report's default advice when other evidence explains its verdict.
#define DIAG_NOTE_LINES 7
struct DiagNote {
  char line[DIAG_NOTE_LINES][48];
  uint8_t n;
};
void noteReset(DiagNote& n);
void noteAdd(DiagNote& n, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
// Writes the note's lines; false when it is empty, so the caller prints its default advice.
bool notePrint(const DiagNote* n, DiagOut& o);

const char* diagOrb(uint8_t level);
void diagHourText(uint8_t hour, bool brief, char* out, size_t len);        // "8am", or brief "8a"
void diagSpanText(uint8_t start, uint8_t len_hours, char* out, size_t len); // "8am-5pm"
void diagVolts(uint16_t mv, char* out, size_t len);                         // "3.98V"
void diagDateParts(uint32_t local_day, unsigned* day, unsigned* month);     // local day 1 = 1 Jan 1970
void diagDateText(uint32_t local_day, char* out, size_t len);               // "14 Oct"
void diagLower(char* text);
extern const char* const DIAG_MONTHS[12];
