# core

[↩ Back to firmware readme](../../README.md)

## Shared Code for Every MobMesh Build

`core` holds the code several mods share, so each mod does not carry its own copy. Every
target includes it, as it includes `shim`.

| File | What it provides |
| --- | --- |
| `ModFS.h` | One storage name for mods: SPIFFS on ESP32, InternalFS on nRF52 |
| `ModPlatform.h` | Chip-family mapping for mod code |
| `ModCliRows.cpp` | Commands that answer with up to 6 replies, each sent as its own message over LoRa |

## Where code goes

- **shim:** only what has to sit inside an upstream file: hook call points and the accessors that read upstream internals.
- **core:** code used by two or more mods, or that applies to the whole board. It reaches upstream only through shim's hooks.
- **A feature mod:** code used by that mod alone.

[↩ Back to firmware readme](../../README.md)
