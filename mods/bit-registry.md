# Mod bit registry

Every firmware image carries a `u32` at file offset 272 naming which mods it was built
with. One bit per mod, little-endian. `scripts/patch_ota_metadata.py` writes it;
that script's module docstring describes the block it sits in.

## The register

| bit | value | mod | marker | who reads it |
| --- | --- | --- | --- | --- |
| 0 | `0x00000001` | *unassigned* | | |
| 1 | `0x00000002` | `hotspot-ota` | `H0TSP0T` | the OTA download, to refuse an image that could not itself be updated |
| 2–31 | | *unassigned* | | |

## Rules

**A bit is set from the binary, never from the build config.** CI searches the built image
for the mod's `image_marker` and sets the bit only when it is there. A target that lists a
mod whose marker is absent gets a warning and a clear bit, not a failed build.

**Keep the marker referenced from live code.** `-fdata-sections` plus `--gc-sections` drops
an unreferenced string, and `__attribute__((used))` does not stop the linker — it is a
compiler attribute, and this toolchain predates `retain`.

**A mod earns a bit only once it has a marker.** No marker, no entry here, no bit. That is
what lets a clear bit mean *absent* rather than *unknown*.

**Bits are permanent.** Never renumber one, and never reuse a retired mod's — an old node
reading a new image would misidentify what it is holding. A retired mod's row stays, marked
retired, and the next mod takes the next free bit.

**32 bits is a lifetime budget, not a concurrent one.** Widening to `u64` is possible later
under a layout version bump; the 12 reserved bytes after the field are there for it.

## Adding a mod to the register

1. Put a marker in the mod's own source, so its presence is evidence:
   ```cpp
   static const char SOME_MOD_MARKER[] __attribute__((used)) = "MARKER";
   ```
   `used` matters — nothing reads it at runtime and the linker would otherwise drop it.
2. Declare it in `mods/<name>/mod.yaml`:
   ```yaml
   bit: 2
   image_marker: "MARKER"
   ```
3. Add the row above.
4. `scripts/tests/test_patch_ota_metadata.py` checks the bits are unique and in range.
