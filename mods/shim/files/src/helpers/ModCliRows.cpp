#include <Arduino.h>
#include <string.h>
#include <helpers/ModHooks.h>
#include <helpers/TxtDataHelpers.h>

// Each frame fits upstream's own reply datagram: 4-byte timestamp, flags byte, text.
static const size_t ROW_TEXT_MAX = 155;
static const uint8_t ROW_LIMIT = 6;
static const uint32_t ROW_SPACING_MILLIS = 1500;

static char rows[ROW_LIMIT][ROW_TEXT_MAX + 1];
static uint8_t row_count;
static uint8_t row_next;
static uint32_t rows_for;
static uint32_t last_stamp;
static bool serial_started;

void modCliBegin(uint32_t sender_timestamp) {
  row_count = row_next = 0;
  rows_for = sender_timestamp;
  serial_started = false;
}

bool modCliAddRow(uint32_t sender_timestamp, const char* text) {
  if (sender_timestamp == 0) {
    Serial.print(serial_started ? "" : "  -> ");
    Serial.println(text);
    serial_started = true;
    return true;
  }
  if (sender_timestamp != rows_for || row_count >= ROW_LIMIT) return false;
  strncpy(rows[row_count], text, ROW_TEXT_MAX);
  rows[row_count][ROW_TEXT_MAX] = 0;
  row_count++;
  return true;
}

bool modCliNextRow(uint32_t sender_timestamp, uint8_t* frame, size_t* len, uint32_t* delay_millis) {
  if (sender_timestamp != rows_for || row_next >= row_count) {
    row_count = row_next = 0;
    return false;
  }
  uint32_t stamp = modClockGet();
  if (stamp <= sender_timestamp) stamp = sender_timestamp + 1;
  if (stamp <= last_stamp) stamp = last_stamp + 1;
  last_stamp = stamp;
  size_t text_len = strlen(rows[row_next]);
  memcpy(frame, &stamp, 4);
  frame[4] = TXT_TYPE_CLI_DATA << 2;
  memcpy(&frame[5], rows[row_next], text_len);
  *len = 5 + text_len;
  *delay_millis = row_next * ROW_SPACING_MILLIS;
  row_next++;
  return true;
}
