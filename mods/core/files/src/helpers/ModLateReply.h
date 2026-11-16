#pragma once

#include <stddef.h>
#include <stdint.h>

// Replies sent after a command has returned: to serial at once, or to the admin who asked as late CLI frames.

struct ModReplyTarget {
  bool remote;
  uint32_t sender_timestamp;
  uint8_t admin_key[32];
};

// Records who is asking; false for a remote command whose admin cannot be found.
bool modReplyCapture(uint32_t sender_timestamp, ModReplyTarget* out);
// Rows of at most 155 characters, `stride` bytes apart; spaced so a direct path or a flood delivers them in order.
void modReplyLate(const ModReplyTarget& target, const char* rows, size_t stride, int count);
