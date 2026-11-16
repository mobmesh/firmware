#include <helpers/diag_reports/DiagOut.h>

#include "check.h"

int main() {
  static DiagOut o;
  char row[DIAG_ROW_MAX + 1], line[200], t[16];
  uint16_t pos = 0;

  outReset(o);
  CHECK(!outRow(o, &pos, row) && outRowCount(o) == 0);
  outLine(o, "one %d", 1);
  outSep(o);
  outLine(o, "three");
  CHECK(o.lines == 3 && !o.truncated);
  CHECK(outGetLine(o, 1, line, sizeof(line)));
  CHECK_STR(line, "~");
  CHECK(!outGetLine(o, 3, line, sizeof(line)));
  pos = 0;
  CHECK(outRow(o, &pos, row));
  CHECK_STR(row, "one 1\n~\nthree");
  CHECK(!outRow(o, &pos, row));

  // Rows break only between lines and never pass 155 characters.
  outReset(o);
  for (int i = 0; i < 12; i++) outLine(o, "line %02d %s", i, "0123456789012345678901234567890123456789");
  CHECK(o.lines == 12);
  int rows = 0, seen = 0;
  pos = 0;
  while (outRow(o, &pos, row)) {
    rows++;
    CHECK(strlen(row) <= DIAG_ROW_MAX && row[0] == 'l' && row[strlen(row) - 1] == '9');
    for (const char* c = row; (c = strstr(c, "line ")) != nullptr; c++) seen++;
  }
  CHECK(rows == 4 && seen == 12 && outRowCount(o) == 4);

  // A line longer than a row is dropped and flagged, and the arena stays intact.
  outReset(o);
  outLine(o, "ok");
  outLine(o, "%0200d", 1);
  CHECK(o.truncated && o.lines == 1);
  pos = 0;
  CHECK(outRow(o, &pos, row));
  CHECK_STR(row, "ok");

  // The arena never overflows.
  outReset(o);
  for (int i = 0; i < 40; i++) outLine(o, "%0100d", i);
  CHECK(o.truncated && o.len < DIAG_OUT_SIZE && o.buf[o.len] == 0);

  diagHourText(0, false, t, sizeof(t));
  CHECK_STR(t, "12am");
  diagHourText(17, false, t, sizeof(t));
  CHECK_STR(t, "5pm");
  diagHourText(12, true, t, sizeof(t));
  CHECK_STR(t, "12p");
  diagSpanText(8, 9, t, sizeof(t));
  CHECK_STR(t, "8am-5pm");
  diagSpanText(22, 4, t, sizeof(t));
  CHECK_STR(t, "10pm-2am");
  diagVolts(3977, t, sizeof(t));
  CHECK_STR(t, "3.98V");
  diagVolts(3999, t, sizeof(t));
  CHECK_STR(t, "3.99V");
  diagVolts(4000, t, sizeof(t));
  CHECK_STR(t, "4.00V");
  diagDateText(1, t, sizeof(t));
  CHECK_STR(t, "1 Jan");
  diagDateText(20835, t, sizeof(t));
  CHECK_STR(t, "16 Jan");
  strcpy(t, "Net Loss");
  diagLower(t);
  CHECK_STR(t, "net loss");
  CHECK(strcmp(diagOrb(DIAG_UNKNOWN), "⚪") == 0);
  DONE();
}
