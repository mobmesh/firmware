#pragma once

// Mod storage under one name on every chip family; on ESP32 it is SPIFFS itself, so the code is unchanged there.
#if defined(ESP32)
#include <SPIFFS.h>
#define ModFS SPIFFS
#elif defined(NRF52_PLATFORM)
#include <Adafruit_LittleFS.h>
#include <InternalFileSystem.h>

using namespace Adafruit_LittleFS_Namespace;

// SPIFFS calling conventions over InternalFS; "w" truncates on ESP32 but appends here, so a write removes first.
class ModFSClass {
public:
  bool begin(bool format_on_fail = false) { (void)format_on_fail; return InternalFS.begin(); }
  bool exists(const char* path) { return InternalFS.exists(path); }
  bool remove(const char* path) { return InternalFS.remove(path); }
  File open(const char* path, const char* mode = "r", bool create = false) {
    (void)create;
    if (mode[0] == 'w') {
      InternalFS.remove(path);
      return InternalFS.open(path, FILE_O_WRITE);
    }
    return InternalFS.open(path, FILE_O_READ);
  }
};

static ModFSClass ModFS;
#else
#error "ModFS.h: no storage mapping for this platform"
#endif
