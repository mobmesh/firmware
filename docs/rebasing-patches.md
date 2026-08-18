# Rebasing the mods onto a new upstream release

This project carries its features as patches against upstream MeshCore rather than a
fork (see `mods/*/README.md`). When upstream tags a release that our patches no longer
apply to, `sync-build-release` fails loudly and opens an issue. This is the procedure
for getting back to green.

## Don't hand-edit the patch files

Rebasing 30+ hunks across a dozen files by editing `.patch` context lines is slow and
error-prone. Replay the patches as commits on the ref they *do* apply to, then let git's
three-way merge move them:

```sh
./scripts/rebase-patches.sh start repeater-v1.16.0 repeater-v1.17.0
# resolve conflicts in the printed workdir, then:
#   git -C <workdir> add -A && git -C <workdir> rebase --continue
# repeat until the rebase completes, then:
./scripts/rebase-patches.sh finish
```

`finish` rewrites `mods/*/patches/*.patch` in place from the rebased commits. It applies
the patches in the same order CI does (mods in `build-targets.yaml` order, then each
mod's patches sorted by filename).

CI also gives you a head start: when a patch fails to apply, the workflow attempts a
three-way merge and uploads the result as a `patch-rescue-*` artifact on the run. That
diff is **not** validated and **not** built -- it is a starting point, not a fix.

## A conflict is a question about intent, not text

The important part is not making the hunks apply again. It is deciding whether each hunk
should still exist. Three outcomes, all of which occurred in the 1.16 -> 1.17 rebase:

**Keep it (positional conflict).** Upstream added code at the same place we add ours --
the end of a virtual list, the end of an `else if` chain, the end of `loop()`. Keep both
sides. Five of the nine conflicts were this.

**Drop it (upstream fixed it).** `EnvironmentSensorManager.cpp` and
`MicroNMEALocationProvider.h` had adopted `(long)(millis() - x) > 0`, exactly what our
`millis_passed()` expands to. Keeping our version would have been a no-op diff carried
forever. Take upstream's and delete the hunk -- and delete anything it left orphaned,
such as a now-unused `#include`.

**Drop it (upstream rewrote it).** `UITask::loop()`'s button handling moved to an
event-based `user_btn.check()` API, deleting the `digitalRead`/`_prevBtnState` polling
one hunk existed to fix. The bug it fixed no longer exists in that form.

Redundant hunks are the dangerous case, because they have **no failure signal**. A patch
that duplicates a fix upstream already made keeps applying cleanly forever. Two defences:

- Sidecars may declare `upstream_prs: [...]`. `patch-drift-canary` checks their state
  daily and flags any that have merged.
- When resolving a conflict, read the surrounding code, not just the markers.

## Anchor away from append points

Conflict risk tracks *adjacency to where upstream appends*, not churn volume.
`CommonCLI.cpp` took the heaviest churn in 1.17 (+106/-80) and produced one conflict;
`MeshCore.h` changed three lines and also produced one. Every positional conflict was at
the tail of a list both sides append to.

`git apply` matches three lines of context, so an insertion point needs roughly four
lines of separation from upstream's to survive. Where it costs nothing, anchor our
additions at a stable interior point instead of the end:

- `RollbackGuard::poll()` sits at the **top** of `loop()`, not the bottom. It is a flag
  and deadline check with no ordering dependency, and `void loop() {` is a far more
  stable anchor than the end of the body.
- The `ota.fw.url` / `ota.wan.pwr` / `ota.slot` branches in `handleGetCmd` sit
  immediately after the long-lived `dutycycle` branch rather than at the end of the
  chain. Order is semantically free there -- no other get-branch's key starts with ours,
  so nothing is shadowed.

This is not worth doing where it hurts readability. `startOTAUpdateFromURL` stays next to
the other OTA virtuals in `MeshCore.h` even though that is a contested spot; a keep-both
conflict there is trivial, and scattering related declarations to quiet corners of a
header costs more than it saves.

## Always build before committing

`git apply` succeeding proves nothing about whether the result compiles -- and the
resolutions that matter most (dropped hunks, adapted hunks) are exactly the ones that
can break the build. Build every env in `build-targets.yaml`:

```sh
python3 -m venv /tmp/pioenv && /tmp/pioenv/bin/pip install -q platformio pyyaml

# For each target: inject the flags CI would inject, then build. Run inject-env from
# the repo root, and pass that target's full mod list from build-targets.yaml --
# core_mods first. Omitting a mod silently drops its build_src_filter entries and the
# link fails on an undefined reference rather than anything that names the cause.
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

For the 1.17.0 rebase all four envs built: heltec_v4 repeater 20.4% flash, room server
20.3%, Xiao_C3 repeater 70.1%, room server 69.8%.

## Early warning

`patch-drift-canary` runs daily against upstream's `main` and `dev` branches and
maintains a single tracking issue labelled `patch-drift`. It exists because
`sync-build-release` only discovers drift when upstream tags a release -- precisely when
you want to ship. The canary moves that discovery days or weeks earlier, and closes its
own issue once the patches apply again.
