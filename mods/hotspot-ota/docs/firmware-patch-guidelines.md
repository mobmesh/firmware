# Firmware patch guidelines

Notes on patch conventions and ESP32 stack usage, for anyone editing
`patches/*.patch` or code under `src/helpers/esp32/`.

## Patch naming and dependency sidecars

Each patch is named `<zero-padded-numeric-ID>_<descriptive-name>.patch` --
underscore separates the ID from the name unambiguously (hyphens still
separate words within the name itself), e.g. `0001_hotspot-fetch-ota.patch`.
This follows the same convention as Django migrations (`0001_initial.py`)
and is parsed via a single split on the first `_`.

Every patch has a sidecar named by **ID only**, not by the patch's full
filename: `0001.meta.yaml`, not `0001_hotspot-fetch-ota.patch.meta.yaml`.
This is deliberate -- if a patch's descriptive name is ever changed later,
only its `title:` field needs updating, not the sidecar's filename too.

Sidecar schema (kept minimal -- no enable/disable flag system exists yet;
every patch in a mod is applied unconditionally):

```yaml
id: "0002"
title: ota-rollback-guard
requires: ["0001"]   # patch IDs (from this same mod) that must already be applied
```

CI verifies `requires` is satisfied, in order, before applying any patch --
add a sidecar for every new patch, even one with an empty `requires: []`.

## Board-specific constants belong in overrides.yaml, not the patch

If a patch needs a board-specific value (a GPIO pin, a timing constant),
don't hardcode it as a `#define` in the patch's new/modified source. Add it
to `variants/<board>/overrides.yaml` (see the root README) instead, and
reference it in the patch as a plain macro -- it gets injected as a `-D`
build flag into that board's `platformio.ini`, matching upstream MeshCore's
own idiom for board-specific pins (`PIN_GPS_RX`, `PIN_VEXT_EN`, etc.). Run
`scripts/generate-board-config.py flags --board <board>` to see the exact
lines that should be in the patch, and
`scripts/generate-board-config.py verify-flags --board <board> --patch <path>`
to confirm they're actually present (CI runs this automatically).

## Stack usage in CommonCLI and setup()

MeshCore's CLI dispatch (`CommonCLI::handleGetCmd()`, `handleSetCmd()`,
`handleCommand()`) combines every CLI command as a branch in one large
function body per verb. A stack-local variable added to one branch adds to
that whole function's combined frame size, not just the branch's own
footprint -- so a modestly-sized local here costs more stack budget than the
same variable would cost almost anywhere else in the codebase.

`setup()` (in `examples/simple_repeater/main.cpp` and
`examples/simple_room_server/main.cpp`) is similar in a different way: it's
already several calls deep by the time radio initialization completes, so a
new synchronous call chain added there -- into SPIFFS, WiFi, or another
library with non-trivial internal stack usage -- adds on top of an already
substantial baseline.

**Guidelines:**

1. Prefer `static` over a stack-local for any non-trivial-sized variable in
   either of the above. This is safe here specifically because MeshCore's CLI
   is single-threaded and processes one command at a time -- no reentrancy
   concern from a static.
2. If new work in `setup()`'s call chain needs file or network I/O, do it on
   the first `loop()` iteration instead of synchronously in `setup()` (a
   one-shot flag checked at the top of a function called from `loop()`, as
   `RollbackGuard::poll()` does, is the established pattern here).
3. Confirm changes to either area on real hardware before merging, not just a
   successful build -- this class of issue compiles and links cleanly and
   only shows up when actually running.

## The CLI reply buffer is a fixed 160 bytes

Every CLI handler writes its response into a caller-owned `char reply[160]`
(declared in `examples/simple_repeater/main.cpp` and
`examples/simple_room_server/main.cpp`), via plain `sprintf`/`strcpy` with no
length check. A response that exceeds 160 bytes overflows that stack buffer
and crashes the device -- compiles and links fine, only shows up when the
command is actually run. Before adding or extending a CLI response, total up
its worst-case length (including any `%s` fields at their maximum size) and
confirm it comfortably fits, leaving headroom rather than sizing to the
exact limit.

## `File::operator bool()` goes false once closed

An Arduino `File` object stops reporting itself as open as soon as
`.close()` is called on it, even though nothing about the read/write that
just happened was unsuccessful. Checking a `File`'s open/valid status for a
decision made *after* `.close()` -- rather than capturing that status into a
`bool` beforehand -- silently takes the "file wasn't open" branch every
time, regardless of whether the preceding read or write actually succeeded.
Capture the status you need before closing, not after.
