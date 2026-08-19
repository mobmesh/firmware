# Rebasing the mods onto a new upstream release

This project carries its features as patches applied to a fresh clone of upstream
MeshCore rather than as a fork. When upstream tags a release the patches no longer
apply to, `sync-build-release` fails and opens an issue. This document is the
procedure for getting back to green, and the reasoning behind the choices that keep
the job small.

See `mods/hotspot-ota/docs/firmware-patch-guidelines.md` for the conventions that
apply when adding or moving code between mods rather than only reconciling existing
hunks, and `mods/shim/README.md` for what the hook layer provides.

## Where the drift actually is

Only a minority of the patch set can conflict with upstream at all. Most mod code
lives in files the mods themselves create, which upstream never touches.

| mod | upstream files patched | hunks |
| --- | --- | --- |
| `shim` | `simple_repeater/main.cpp`, `simple_room_server/main.cpp`, `CommonCLI.cpp`, `CommonCLI.h` | 11 |
| `timing-safety` | `simple_repeater/MyMesh.cpp`, both `UITask.cpp`, `ArduinoHelpers.h` | 11 |
| `hotspot-ota` | `MeshCore.h`, `ESP32Board.h` | 2 |
| `power-guard` | none | 0 |

Twenty-four hunks across ten files is the entire conflict surface. Everything else
patches files created by `shim` — `ModHooks.h/.cpp`, `CommonCliMods.cpp` — or lives
under `variants/<board>/`.

`shim` owns the four upstream *insertion points*: the two `main.cpp` files and
`CommonCLI.cpp/.h`. No other mod may patch them, and
`scripts/tests/test_patch_ownership.py` fails the build if one tries. A mod needing
to reach an upstream call site adds a hook to `shim` and calls it from its own file.
This is what keeps four mods from colliding in the same region of the same file, and
it is why `power-guard` — the newest and largest mod — carries no upstream drift at
all.

## The sidecar declares what a patch needs

Every patch has a `NNNN.meta.yaml` beside it. Read it before touching the patch: it
records what the patch assumes, and those assumptions are the first thing a rebase
invalidates.

```yaml
id: "0001"
title: power-guard
requires: ["shim/0001", "hotspot-ota/0001", "hotspot-ota/0002"]
env_flag: WITH_POWER_GUARD
build_src_filter: []
```

`requires` names patches that must already be applied. These are usually **context
dependencies, not code dependencies**: two patches editing neighbouring regions of
the same shim-created file, where the second one's context lines only exist once the
first has run. CI checks the whole list before applying anything and fails naming the
missing patch and the mod order that produced it.

This matters during a rebase because resolving a conflict can silently change what a
patch depends on. Splitting a hunk into a new patch, moving a hunk between mods, or
dropping a hunk another patch anchored against all change `requires`, and none of them
announce it. After `finish`, re-read the sidecar of every patch that was touched.

`build_src_filter` lists the source files a mod contributes to the build. A missing
entry does not fail the patch stage; it fails much later at link time, on an undefined
reference that names nothing useful.

`upstream_prs` records upstream pull requests a patch exists to carry ahead of merge.
The canary checks their state daily; once one merges the patch is probably redundant
and needs re-reviewing rather than rebasing.

## Do not hand-edit the patch files

Reconciling hunks by editing `.patch` context lines is slow and produces corrupt
patches. Replay the patches as commits on the ref they *do* apply to and let git's
three-way merge move them:

```sh
./scripts/rebase-patches.sh start repeater-v1.17.1 repeater-v1.18.0
# resolve conflicts in the printed workdir, then:
#   git -C <workdir> add -A && git -C <workdir> rebase --continue
# repeat until the rebase completes, then:
./scripts/rebase-patches.sh finish
```

`finish` rewrites `mods/*/patches/*.patch` in place from the rebased commits,
applying them in the order CI does: `core_mods` from `build-targets.yaml` first, then
each target's own mods, then each mod's patches sorted by filename.

CI provides a head start. When a patch fails to apply, the workflow attempts a
three-way merge and uploads the result as a `patch-rescue-*` artifact on the run.
That diff is **not validated and not built** — a starting point, not a fix.

## A conflict is a question about intent, not text

The work is not making hunks apply again. It is deciding whether each hunk should
still exist. Three outcomes:

**Keep it — positional conflict.** Upstream added code where this project adds its
own: the end of a virtual list, the end of an `else if` chain, the end of `loop()`.
Keep both sides. This is the common case and usually the whole job.

**Drop it — upstream fixed it.** Upstream adopts the same fix independently, and
keeping the local version leaves a no-op diff carried forever. Take upstream's, drop
the hunk, and delete anything it leaves orphaned such as a now-unused `#include`.

**Drop it — upstream rewrote it.** The code a hunk patched no longer exists in a
form the fix applies to; polling replaced by an event API is the usual shape. The
bug it addressed no longer exists there.

Redundant hunks are the dangerous case, because they carry **no failure signal**. A
patch duplicating a fix upstream has already made will keep applying cleanly forever.
Two defences:

- A patch's `.meta.yaml` sidecar may declare `upstream_prs: [1972, 1349]`.
  `patch-drift-canary` checks their state daily and flags any that merge.
- When resolving a conflict, read the surrounding code, not only the markers.

## Keep the conflict surface small

Conflict risk tracks *adjacency to where upstream appends*, not churn volume. A file
taking heavy churn may produce one conflict while a three-line change produces
another; what they have in common is that both sides append to the same tail.

`git apply` matches three lines of context, so an insertion point needs roughly four
lines of separation from upstream's to survive. Where it costs nothing, anchor
additions at a stable interior point rather than the end of a list:

- Hook calls go at the **top** of `loop()`, not the bottom. A hook with no ordering
  dependency anchors far better against `void loop() {` than against whatever
  upstream most recently appended.
- The mod command hook is spliced in **before** the long-lived `dutycycle` branch in
  `handleCommand`, converting it to an `else if` chain, rather than being appended
  after the last branch. Order is semantically free there — no other branch's key
  collides — and `dutycycle` is a far older anchor than the chain's tail.

This is not worth doing where it hurts readability. A declaration belongs beside the
related declarations it documents even when that is a contested spot; a keep-both
conflict is trivial, and scattering related code into quiet corners of a header costs
more than it saves.

## Always build before committing

`git apply` succeeding proves nothing about whether the result compiles, and the
resolutions that matter most — dropped hunks, adapted hunks — are exactly the ones
that can break the build. Build every env in `build-targets.yaml`:

```sh
python3 -m venv /tmp/pioenv && /tmp/pioenv/bin/pip install -q platformio pyyaml

# Inject the flags CI would inject, then build. Run inject-env from the repo root and
# pass that target's full mod list from build-targets.yaml -- core_mods first.
# Omitting a mod silently drops its build_src_filter entries; the link then fails on
# an undefined reference rather than on anything naming the cause.
python3 scripts/generate-board-config.py inject-env \
  --board heltec_v4 --env heltec_v4_repeater \
  --platformio-ini <upstream>/variants/heltec_v4/platformio.ini \
  --mods shim,hotspot-ota,timing-safety,power-guard

# inject-env refuses a second run once a board_build.partitions line is present, so
# restore the file before re-injecting:
#   git -C <upstream> checkout -- variants/heltec_v4/platformio.ini
(cd <upstream> && PLATFORMIO_BUILD_FLAGS="-D 'OTA_MOD_BUILD_DATE=\"local\"' -D 'OTA_MOD_SHORT_SHA=\"local\"'" \
  /tmp/pioenv/bin/pio run -e heltec_v4_repeater)
```

A clean build is still not proof the change took effect. Board values arrive as
`-D` flags behind `#ifndef` fallbacks, so a renamed or dropped constant compiles
cleanly against its default. Confirm at runtime — an error string that prints its
own bounds, or a reported threshold — rather than trusting the compiler.

## Early warning

`patch-drift-canary` runs daily against upstream's `main` and `dev` branches and
maintains a single tracking issue labelled `patch-drift`. It exists because
`sync-build-release` only discovers drift when upstream tags a release, which is
precisely when there is pressure to ship. The canary moves that discovery days or
weeks earlier and closes its own issue once the patches apply again.

Alongside the apply probe it checks assumptions the patches depend on but do not
themselves patch. Those checks are gated on the mod being present in some build
target, so dropping a mod retires its check rather than leaving a permanent false
alarm.
