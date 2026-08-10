# Firmware patch guidelines

Notes on patch conventions and ESP32 stack usage, for anyone editing
`patches/*.patch` or code under `src/helpers/esp32/`.

## Keep upstream edits to a hook, put the code in our own file

Every upstream line a patch adds is a line that can conflict on the next
release, so the rule is: patch the smallest hook you can and put the body in a
file we own, added through the sidecar's `build_src_filter`. Three hooks exist
today, and a new mod should reuse them rather than add a fourth:

| To add | Hook | Our file |
| --- | --- | --- |
| A CLI command | `handleModCommand` / `handleModSetCmd` / `handleModGetCmd` | `helpers/esp32/CommonCliMods.cpp` |
| Setup or loop behaviour | `modRadioInit` / `modLoop` / `modWantsPowerSaving` | `helpers/ModHooks.cpp` |
| A member of an upstream class | declare it in the upstream header only | e.g. `helpers/esp32/HotspotOtaBoard.cpp` |

Two things make this work. A member function may be **defined in any
translation unit**, so an upstream `.cpp` never needs the body. And the CLI
hooks are called **before** upstream's own if-else chain, not at its terminal
`else` -- that is what lets `start ota wan <url>` be matched ahead of upstream's
shorter `start ota` prefix, and it means a hook can deliberately shadow an
upstream command (`ver` does).

The other payoff is that two mods no longer edit the same region of the same
upstream file. `batt-saver` and `hotspot-ota` both used to patch
`simple_repeater/main.cpp`, which is why `batt-saver`'s patch had to be
generated against `hotspot-ota/0002`; both now go through `ModHooks.cpp`.

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

Sidecar schema (no enable/disable flag system exists yet; every patch in a
mod that's part of a target's `build-targets.yaml` `mods:` list is applied
unconditionally):

```yaml
id: "0002"
title: ota-rollback-guard
requires: ["hotspot-ota/0001"]   # mod-qualified patch IDs that must already be applied
env_flag: WITH_OTA_ROLLBACK_GUARD           # optional -- see below
build_src_filter: ["+<helpers/esp32/RollbackGuard.cpp>"]   # optional -- see below
```

`requires` entries are mod-qualified (`<mod-name>/<id>`, not a bare ID) --
two different mods can each have a `0001`, and CI tracks applied patches by
their qualified name to avoid one mod's patch satisfying another's
dependency by coincidence. CI verifies `requires` is satisfied, in order,
before applying any patch -- add a sidecar for every new patch, even one
with an empty `requires: []`.

## Board-specific constants belong in overrides.yaml, not the patch

If a patch needs a board-specific value (a GPIO pin, a timing constant),
don't hardcode it as a `#define` in the patch's new/modified source. Add it
to `variants/<board>/overrides.yaml` instead, and reference it in the patch
as a plain macro, matching upstream MeshCore's own idiom for board-specific
pins (`PIN_GPS_RX`, `PIN_VEXT_EN`, etc.).

Patches themselves never touch a board's `platformio.ini` -- that wiring is
generated at build time by `scripts/generate-board-config.py inject-env`,
which composes:

- every mod-in-scope's `env_flag` and `build_src_filter` entries (from that
  mod's patch sidecars, declared once, board-agnostic), with
- the target board's `overrides.yaml` `build_flags`/`partitions_override`

and inserts the result directly into the resolved `[env:<name>]` section of
the freshly-cloned upstream `platformio.ini`. It fails loudly rather than
guessing if the env section or an expected key can't be found, or if two
mods in scope declare the same `env_flag` -- rename one rather than let it
silently pick one. `scripts/tests/test_generate_board_config.py` covers
this logic directly.

**Which mods are "in scope" for a board is decided in `build-targets.yaml`
at the repo root**, not here and not in `overrides.yaml` -- that file lists
every (board, role) target this project builds and which mods each one
includes. Adding a mod to an existing board is a one-line change to that
target's `mods:` list; it never touches a patch.

Each mod also has a `mods/<name>/mod.yaml` with its own facts (currently
just `suffix`, used to compose the release asset's filename) -- distinct
from the per-patch sidecars, which stay focused on patch-apply ordering and
env contributions.

## CLI docs sync is still single-mod (known boundary, not solved here)

`build-release.yml`'s `cli_commands.md` sync step and
`scripts/generate-commands-json.py` still hardcode `mods/hotspot-ota/docs/`
as the one place CLI documentation lives. Patch application and env
injection are mod-agnostic as of this change; doc aggregation isn't. If a
second mod adds its own CLI commands, merging two mods' contributions into
one `commands.json`/`cli-additions.md` needs a real design (this repo has
never had to answer "how do two mods' docs combine"), not a mechanical
generalization -- left as a boundary for whoever adds that mod.

## Stack usage in CommonCLI and setup()

MeshCore's CLI dispatch (`CommonCLI::handleGetCmd()`, `handleSetCmd()`,
`handleCommand()`) combines every CLI command as a branch in one large
function body per verb. A stack-local variable added to one branch adds to
that whole function's combined frame size, not just the branch's own
footprint -- so a modestly-sized local here costs more stack budget than the
same variable would cost almost anywhere else in the codebase. Our own commands
now sit outside those functions (see the hook section above), so a local in
`CommonCliMods.cpp` costs only its own frame -- but the guidance still applies
to anything added to the upstream branches themselves.

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
