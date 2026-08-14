# batt-saver TODO

Open work before this mod goes on any target. Ordered by what blocks deployment.

## 1. Add a `batt.saver` CLI command (blocker)

No way to countermand the mod once it engages. `powersaving off` doesn't help --
the sleep test is `powersaving_enabled || modWantsPowerSaving()`, so the operator
loses. Headless build, no serial while napping: a miscalibrated unit needs a USB
reflash to recover.

Add to `CommonCliMods.cpp` (hook surface already exists):

- `set batt.saver off|auto` -- runtime override, not persisted
- `get batt.saver` -- active state, `_last_mv`, `_transitions`

Also retires the write-only accessors `lastMilliVolts()` / `transitionCount()`,
which currently have no callers.

## 2. Debounce the implausible-reading release

`BattSaver::loop()` calls `setActive(false)` immediately on `mv == 0 ||
mv >= BATT_SAVER_IMPLAUSIBLE_MV`, skipping the `_agree` counter every other
transition goes through. One spurious high read drops saving. Route it through
`_agree`, or comment why it's exempt.

## 3. Compile it in CI

No target lists `batt-saver`, so the patch is never applied and never built --
CI stays green regardless. Add a job that applies the full stack including this
mod and compiles one target.

## 4. Fix the README dependency line

README says ModHooks.cpp "comes from hotspot-ota/0001"; `0001.meta.yaml`
correctly requires `hotspot-ota/0002`. Align the README.

## 5. Bench-test before enabling

Never run on hardware. Verify engage/release at the real 3300/3600 thresholds,
that mesh CLI still reaches a napping node, and measured current in both states.
