# Firmware patch guidelines

Rules motivated by real hardware failures -- see [`docs/incidents.md`](incidents.md)
for the full writeups. Read this before editing `patches/*.patch` or anything
under `src/helpers/esp32/`.

## Stack depth on ESP32 (Heltec V4)

Two separate incidents reproduced the exact same failure signature on real
hardware: a Heltec V4 that doesn't boot and exposes no USB serial console at
all -- not a crash with a visible error, just silent instability that looks
like a bricked device. Both were ESP32 task-stack exhaustion, reached by two
different mechanisms:

- **Oversized stack-local variables** in `CommonCLI::handleGetCmd()` /
  `handleSetCmd()` / `handleCommand()`. MeshCore's CLI dispatch is one large,
  many-branch function per verb, not split per-command -- adding a stack
  local here costs more than adding it almost anywhere else in the codebase,
  because it's added to a frame that's already large from every other
  branch's own locals.
- **Added call depth** in `setup()` (both `examples/simple_repeater/main.cpp`
  and `examples/simple_room_server/main.cpp`) and anything called
  synchronously from it before the CLI/Serial task is up. This point in
  execution is already several frames deep by the time `radio_init()`
  succeeds -- a new synchronous call chain added here (e.g. into SPIFFS or
  another library with non-trivial internal stack usage) can tip it over
  even if no single added variable is large.

**Rule:** in either of those code paths --

1. Prefer `static` locals over stack locals for anything non-trivial in size.
   Safe here specifically because MeshCore's CLI is single-threaded and
   processes one command at a time -- no reentrancy risk from a static.
2. If new work in `setup()`'s call chain touches SPIFFS, WiFi, or another
   library with a deep call chain, defer it to the first `loop()` iteration
   instead of doing it synchronously in `setup()` (a one-shot flag checked at
   the top of a `poll()`-style function called from `loop()`, as
   `RollbackGuard` does, is the established pattern here).
3. **Verify on real hardware, not just a successful compile.** Both incidents
   compiled and linked cleanly -- the failure only showed up as a boot loop
   or missing serial console when actually flashed. A clean CI build is
   necessary but not sufficient for changes touching either of the two areas
   above.
