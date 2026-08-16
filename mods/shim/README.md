# shim - Hook Points for Everything Else

Owns every place our patches reach into upstream to get called. Not a feature: with
no other mod built, all hooks are no-ops and the firmware behaves as upstream does.

## What it owns

| Upstream file | Insertion |
|---|---|
| `examples/simple_repeater/main.cpp` | `modRadioInit()`, `modLoop()`, `modWantsPowerSaving()` |
| `examples/simple_room_server/main.cpp` | same, minus power saving |
| `src/helpers/CommonCLI.cpp` | one dispatch line per verb handler (3) |
| `src/helpers/CommonCLI.h` | the three `handleMod*` declarations |

It creates the files those calls land in -- `helpers/ModHooks.{h,cpp}` and
`helpers/esp32/CommonCliMods.cpp` -- as empty scaffolds that mods fill in.

`CommonCliMods.cpp` is dispatched ahead of upstream's own chain, so a mod can match
a longer prefix before a shorter upstream one (`start ota wan ...` before `start ota`).

Mods that rewrite upstream code rather than add call sites -- `timing-safety` --
have nothing to hook and depend on nothing.

## Limitation: shared hook bodies

`ModHooks.cpp` and `CommonCliMods.cpp` are single shared files. Every mod adds its
guarded block to the same regions, and each patch is generated against the blocks
already present, so apply order is a total order regardless of what a mod logically
needs.

Consequence: `batt-saver` calls nothing in the OTA stack but declares
`hotspot-ota/0002`, because its patch context contains `RollbackGuard::poll()`.
Against shim alone it fails at `ModHooks.cpp:7`.

Decoupling this requires per-mod hook files with a registration mechanism -- weak
symbols, or a link-time list -- instead of one shared body per hook.

## Ordering

`shim` must come first in every target's `mods:` list. CI enforces it via each
patch's `requires:`. `scripts/tests/test_patch_ownership.py` asserts no other mod
patches the insertion points above.

**Contributes no suffix** -- must not change any released asset's filename.
