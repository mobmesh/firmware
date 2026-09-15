# shim - Hook Points for Everything Else

Owns every place a patch reaches into upstream to get called. Not a feature: with
no other mod built, all hooks are no-ops and the firmware behaves as upstream does.

## What it owns

| Upstream file | Insertion |
|---|---|
| `examples/simple_repeater/main.cpp` | `modRadioInit()`, `modLoop()`, `modWantsPowerSaving()` |
| `examples/simple_room_server/main.cpp` | same, minus power saving |
| `examples/simple_repeater/MyMesh.cpp` | CLI dispatch, region resolution/export, receive observation, transmit completion |
| `examples/simple_room_server/MyMesh.cpp` | Same hooks; native room-server forwarding policy remains unchanged |

It ships `helpers/ModHooks.h` from `files/`. The selected mods' integration declarations
generate `ModHooks.cpp` and `CommonCliMods.cpp` in the temporary upstream tree. Feature
mods own their handlers and hook bodies rather than patching shared aggregate files.

`CommonCliMods.cpp` is dispatched ahead of upstream's own chain, so a mod can match
a longer prefix before a shorter upstream one (`start ota wan ...` before `start ota`).

Mods that rewrite upstream code rather than add call sites -- `timing-safety` --
have nothing to hook and depend on nothing.

## Composition

`mod.yaml` declares each integration header and the phases it contributes. The generator
supports additive pre-radio and loop phases, one exclusive radio-init policy, an OR-reduced
power-saving phase, and priority-ordered first-match CLI handlers. Region routing
and region export each have one exclusive owner; receive and transmit observation
call every selected contributor in composition order. Without an owner, routing
and export defer to upstream and observation hooks do nothing.

`MOD_WITH_TX_HOOKS` is emitted once by `inject-env` when any selected mod declares
transmit observation. It enables packet-identity tracking and completion forwarding
in both roles. Group transmission refuses without tracking; `sync-settings` also
rejects a build missing this flag.

The reverse hooks expose public identity, detached signing, policy reads, CLI
dispatch, and upstream group transmission. Private identity bytes do not cross
the hook boundary. Encryption, packet queueing, radio access, and airtime estimation
remain upstream operations.

Composition fails before compilation for missing headers or symbols, unsupported phases,
duplicate symbols, multiple radio-init policies, or invalid aggregate destinations. The
generated sources are deterministic and refuse to overwrite an upstream file.

The result uses ordinary direct C++ calls. There is no runtime registry, static constructor,
heap allocation, or linker-section behavior.

## Ordering

The resolved mod order controls additive hook order. CLI priority is explicit in each
contributing manifest. `radio_init_policy` has at most one owner.

`scripts/tests/test_patch_ownership.py` asserts no other mod patches the upstream insertion
points. `scripts/tests/test_mod_composition.py` covers phase validation, subset composition,
ordering, output ownership, and byte-deterministic generation.

**Contributes no suffix** -- must not change any released asset's filename.
