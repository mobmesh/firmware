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
| `CoreCli.cpp` | `ver`, the `stats-` commands over the remote CLI, and the refusal of a `poweroff` that would never wake ([CLI reference](docs/cli-additions.md)) |
| `ModTemperature.h` | The CPU die temperature for mods (not ambient or battery), averaged as floats so readings below 0 °C hold |
| `ModIdle.h` | The ESP32 1 ms idle rest, inlined into the main loop |
| `ModProbe.cpp`, `CoreProbe.cpp` | `ping` and `trace` over MeshCore TRACE: one probe at a time, results sent to serial or as a late reply to the admin who asked ([CLI reference](docs/cli-additions.md#ping)) |
| `ModServices.cpp` | What mods ask of MeshCore: read settings and regions, send group and direct packets, sign, follow a tempradio trial. One copy serves the repeater and the room server, acting on what MyMesh hands over at startup. |

## Where code goes

- **shim:** only what has to sit inside an upstream file: hook call points and the accessors that read upstream internals.
- **core:** code used by two or more mods, or that applies to the whole board. It reaches upstream only through shim's hooks.
- **A feature mod:** code used by that mod alone.

[↩ Back to firmware readme](../../README.md)
