# shim - Hook Points for Everything Else

Owns every place a patch reaches into upstream to get called. Not a feature: with
no other mod built, all hooks are no-ops and the firmware behaves as upstream does.

## What it owns

| Upstream file | Insertion |
|---|---|
| `examples/simple_repeater/main.cpp` | `modRadioInit()`, `modLoop()`, `modWantsPowerSaving()`, `the_mesh`'s declared type |
| `examples/simple_room_server/main.cpp` | same, minus power saving |
| `examples/simple_repeater/MyMesh.cpp` | `modHandleCliCommand()` |
| `examples/simple_room_server/MyMesh.cpp` | `modHandleCliCommand()` |

It ships `helpers/ModHooks.h` from `files/`. The selected mods' integration declarations
generate `ModHooks.cpp`, `CommonCliMods.cpp` and `ModMesh.h` in the temporary upstream tree.
Feature mods own their handlers and hook bodies rather than patching shared aggregate files.

`ModMesh.h` is the interposer. `main.cpp` declares `the_mesh` as `ModMesh<MyMesh>` rather
than `MyMesh`, which makes it the most-derived type: its overrides win dispatch even for the
49 virtuals `MyMesh` overrides itself, and `Base::` still reaches `MyMesh`'s implementation.
Interposing the other way -- between `mesh::Mesh` and `MyMesh` -- reaches none of them,
because `MyMesh` overrides them and chains upward by qualified name.

A mod declares mesh overrides under `integration.hooks.mesh`, split by how several mods
combine on one virtual: a `consumer` returns true to claim the call and stop the chain, as
the CLI chain does; an `observer` sees every call and returns nothing. Value-producing
virtuals are refused -- two mods answering `getRetransmitDelay()` cannot both be right, and
ordering hides that rather than resolving it. With no mesh hooks declared the template is
empty and adds no vtable entries.

`CommonCliMods.cpp` is dispatched ahead of upstream's own chain, so a mod can match
a longer prefix before a shorter upstream one (`start ota wan ...` before `start ota`).

Mods that rewrite upstream code rather than add call sites -- `timing-safety` --
have nothing to hook and depend on nothing.

## Composition

`mod.yaml` declares each integration header and the phases it contributes. The generator
supports additive pre-radio and loop phases, one exclusive radio-init policy, an OR-reduced
power-saving phase, and priority-ordered first-match CLI handlers.

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
