#pragma once

#include <stddef.h>
#include <string.h>

#define NAME_BADGE          " \xE1\xB4\xB9"
// 23 bytes of name fit an advert that carries a location; the base keeps what the badge leaves.
#define NAME_BADGE_BASE_MAX 19

// The name to advertise: the stored name cut to NAME_BADGE_BASE_MAX bytes at a character boundary, plus the badge.
inline const char* nameBadgeTx(const char* stored, char out[32]) {
#ifdef NO_ADVERT_BADGE
  (void)out;
  return stored;
#else
  size_t len = strlen(stored);
  if (len > NAME_BADGE_BASE_MAX) {
    len = NAME_BADGE_BASE_MAX;
    while (len > 0 && ((unsigned char)stored[len] & 0xC0) == 0x80) len--;
  }
  while (len > 0 && stored[len - 1] == ' ') len--;
  memcpy(out, stored, len);
  const char* badge = len ? NAME_BADGE : NAME_BADGE + 1;   // an empty name sends the bare badge
  memcpy(out + len, badge, strlen(badge) + 1);
  return out;
#endif
}
