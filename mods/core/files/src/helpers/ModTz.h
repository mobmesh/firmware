#pragma once

#include <stdint.h>

// The node's local time offset, whole hours from UTC (-12 to +14, no daylight saving), for every mod's reports.

int8_t modTzHours();
bool modTzSet(int8_t hours);             // false when it could not be saved
// "-5h", "-5", "+9": optional sign, digits, optional h; false outside -12..+14.
bool modTzParse(const char* text, int8_t* hours);
