# miniz decompressor (vendored)

`miniz_tinfl.h`, `miniz_common.h` and `miniz_tinfl.c.inc` are from
[richgel999/miniz](https://github.com/richgel999/miniz) release 3.1.2, MIT licence (`LICENSE`).

Changes from upstream:

- `miniz_tinfl.c` is renamed `miniz_tinfl.c.inc` so the build does not compile it on its own, and its
  `#include "miniz.h"` reads `#include "miniz_tinfl.h"`.
- `miniz_export.h` is a one-line stand-in for the file miniz's CMake build generates.

`../SyncTinfl.cpp` compiles it for nRF52 only. ESP32 chips decompress with the copy in their mask ROM.
