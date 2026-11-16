#include <helpers/diag_reports/DiagOut.h>

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

void outReset(DiagOut& o) {
  o.len = 0;
  o.lines = 0;
  o.truncated = false;
  o.buf[0] = 0;
}

void outLine(DiagOut& o, const char* fmt, ...) {
  size_t at = o.len + (o.lines ? 1 : 0);
  if (at >= DIAG_OUT_SIZE) {
    o.truncated = true;
    return;
  }
  va_list args;
  va_start(args, fmt);
  int n = vsnprintf(o.buf + at, DIAG_OUT_SIZE - at, fmt, args);
  va_end(args);
  if (n < 0 || at + (size_t)n >= DIAG_OUT_SIZE || n > DIAG_ROW_MAX) {
    o.buf[o.len] = 0;
    o.truncated = true;
    return;
  }
  if (o.lines) o.buf[o.len] = '\n';
  o.len = (uint16_t)(at + n);
  o.lines++;
}

void outSep(DiagOut& o) {
  outLine(o, "~");
}

bool outRow(const DiagOut& o, uint16_t* pos, char row[DIAG_ROW_MAX + 1]) {
  if (*pos >= o.len) return false;
  uint16_t start = *pos, end = start;
  // Take whole lines while the row still fits.
  while (end < o.len) {
    const char* nl = (const char*)memchr(o.buf + end, '\n', o.len - end);
    uint16_t line_end = nl ? (uint16_t)(nl - o.buf) : o.len;
    if (line_end - start > DIAG_ROW_MAX) break;
    end = line_end;
    if (end < o.len) end++;
  }
  uint16_t stop = end;
  if (stop > start && stop <= o.len && o.buf[stop - 1] == '\n') stop--;
  memcpy(row, o.buf + start, stop - start);
  row[stop - start] = 0;
  *pos = end;
  return true;
}

int outRowCount(const DiagOut& o) {
  char row[DIAG_ROW_MAX + 1];
  uint16_t pos = 0;
  int n = 0;
  while (outRow(o, &pos, row)) n++;
  return n;
}

bool outGetLine(const DiagOut& o, int index, char* out, size_t len) {
  const char* at = o.buf;
  const char* end = o.buf + o.len;
  for (int i = 0; i < o.lines; i++) {
    const char* nl = (const char*)memchr(at, '\n', end - at);
    const char* stop = nl ? nl : end;
    if (i == index) {
      size_t n = (size_t)(stop - at) < len - 1 ? (size_t)(stop - at) : len - 1;
      memcpy(out, at, n);
      out[n] = 0;
      return true;
    }
    at = stop + 1;
  }
  return false;
}

void noteReset(DiagNote& n) {
  n.n = 0;
}

void noteAdd(DiagNote& n, const char* fmt, ...) {
  if (n.n >= DIAG_NOTE_LINES) return;
  va_list args;
  va_start(args, fmt);
  vsnprintf(n.line[n.n], sizeof(n.line[0]), fmt, args);
  va_end(args);
  n.n++;
}

bool notePrint(const DiagNote* n, DiagOut& o) {
  if (n == nullptr || n->n == 0) return false;
  for (int i = 0; i < n->n; i++) outLine(o, "%s", n->line[i]);
  return true;
}

const char* diagOrb(uint8_t level) {
  switch (level) {
    case DIAG_GOOD: return "\U0001F7E2";
    case DIAG_CAUTION: return "\U0001F7E1";
    case DIAG_PROBLEM: return "\U0001F534";
    default: return "⚪";
  }
}

void diagHourText(uint8_t hour, bool brief, char* out, size_t len) {
  hour %= 24;
  int h12 = hour % 12 == 0 ? 12 : hour % 12;
  if (brief) snprintf(out, len, "%d%c", h12, hour < 12 ? 'a' : 'p');
  else snprintf(out, len, "%d%s", h12, hour < 12 ? "am" : "pm");
}

void diagSpanText(uint8_t start, uint8_t len_hours, char* out, size_t len) {
  char a[8], b[8];
  diagHourText(start, false, a, sizeof(a));
  diagHourText((uint8_t)((start + len_hours) % 24), false, b, sizeof(b));
  snprintf(out, len, "%s-%s", a, b);
}

void diagVolts(uint16_t mv, char* out, size_t len) {
  unsigned cents = (mv % 1000 + 5) / 10;
  snprintf(out, len, "%u.%02uV", mv / 1000, cents >= 100 ? 99 : cents);
}

void diagDateParts(uint32_t local_day, unsigned* day, unsigned* month) {
  long z = (long)local_day - 1 + 719468;
  long era = z / 146097;
  unsigned doe = (unsigned)(z - era * 146097);
  unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  unsigned mp = (5 * doy + 2) / 153;
  *day = doy - (153 * mp + 2) / 5 + 1;
  *month = mp < 10 ? mp + 3 : mp - 9;
}

const char* const DIAG_MONTHS[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

void diagDateText(uint32_t local_day, char* out, size_t len) {
  unsigned d, m;
  diagDateParts(local_day, &d, &m);
  snprintf(out, len, "%u %s", d, DIAG_MONTHS[m - 1]);
}

void diagLower(char* text) {
  for (; *text; text++) if (*text >= 'A' && *text <= 'Z') *text += 'a' - 'A';
}
