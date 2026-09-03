# Firmware - by GulfCoastMesh Mobile

Custom firmware for MeshCore running on ESP32 boards.

🔗 ["Give me the sales pitch, why do I need this?"](https://github.com/mobmesh/firmware/blob/main/README_PITCH.md)

This project layers its changes onto a fresh copy of the upstream MeshCore source instead of maintaining a separate long-term fork. Each build starts with the latest upstream release: each mod's own source files are copied in, then a small set of patches edits the upstream files that need to call them. That keeps the project current without slowly drifting out of sync with MeshCore.

The MeshCore source code itself is not stored in this repository. Instead, this repo contains the patches, board-specific configuration, and the GitHub Actions workflow that puts everything together and publishes the builds.

## Available Mods

Mods add features or changes to the standard MeshCore firmware. Each mod can ship owned source, integration declarations, upstream patches, board configuration, and documentation.

| Mod           | Description                                                                                                                                                                                                                   | Main Features                                                                                                                                                                                         |
| ------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| [`hotspot-ota`](https://github.com/mobmesh/firmware/tree/main/mods/hotspot-ota) | Adds remote firmware updates over WiFi ( or cellular hotspot) and automatic rollback protection. A device can connect to an existing WiFi network, download a firmware image, verify it, and install it without needing to be onsite with the node. | Remote OTA updates, power control of external cell modems, firmware SHA-256 verification, firmware authenticity checks, OTA slot management, automatic rollback / recovery after failed updates, automatic clock sync via NTP whenever WiFi is joined, remote updates through MeshCore CLI commands, downloads that run in the background so the node keeps repeating, and an update in progress that can be cancelled |
| [`timing-safety`](https://github.com/mobmesh/firmware/tree/main/mods/timing-safety) | Small fixes for how the firmware tracks time. Keeps timers working correctly on devices that run for many weeks, and stops "time since last heard from" numbers from showing garbage right after a reboot. | Long-uptime timer fix, safer elapsed-time math across reboots |
| [`power-guard`](https://github.com/mobmesh/firmware/tree/main/mods/power-guard) | Keeps a bad situation from becoming an unrecoverable one, and puts the battery under its own management. Brownouts happen -- a flat pack, a cold morning, a cloudy week. Left alone, a node that browns out reboots straight into a loop that burns whatever charge is left and ends in a trip up the tower. This hibernates before it gets there, retries on a widening schedule, and comes back by itself once the battery does. Beyond the standard `powersaving on` / `off` it adds `powersaving auto`, which saves power only when the battery says to, and `powersaving safe`, the brownout failsafe. It also stops a mistyped `poweroff` from ending a node permanently. | Hibernation before the bootloop threshold, automatic recovery, power saving that engages only when it's needed, thresholds set over serial or the mesh and kept across reboots, `poweroff` requires a wake time and is refused over the mesh |

More information about each mod can be found in its own README under `mods/<name>/`.

## Web-Based Flasher

You can use the ⚡️[web-based flasher](https://tools.mobmesh.org/flasher) to flash a supported devices directly from your browser over USB.

The flasher requires Chrome, Edge, or Opera because it uses Web Serial. No additional software is needed.

It automatically uses the most recently built firmware for the board and variant you select.

This is not the official MeshCore flasher. It was <ins>built specifically</ins> for the custom firmware releases in this project.

Some mods may add extra options or requirements for a traditional flasher. Check the README for the specific mod if you need more information. For example, `hotspot-ota` changes how images are assigned to a device's OTA slots to support auto-recovery — see that mod's README for details.



## Supported Boards 
<sub><i>* more boards are on the way - " lookin' at you Grumpy "</sub></i>

| Variant     | Board          | Upstream Tag     | Release Asset                      |
| ----------- | -------------- | ---------------- | ----------------------------------- |
| Repeater    | Heltec V4      | `repeater-v*`    | `heltec_v4_rep_mobmesh-vX.Y.Z.bin`  |
| Room Server | Heltec V4      | `room-server-v*` | `heltec_v4_room_mobmesh-vX.Y.Z.bin` |
| Repeater    | Xiao ESP32-C3  | `repeater-v*`    | `xiao_c3_rep_mobmesh-vX.Y.Z.bin`    |
| Room Server | Xiao ESP32-C3  | `room-server-v*` | `xiao_c3_room_mobmesh-vX.Y.Z.bin`   |

Each also ships a `-merged.bin` alongside it: the same firmware plus the bootloader,
partition table and otadata in one file, written at offset 0 to a blank board. The plain
`.bin` is the app alone, for an OTA slot or an update over an existing install.

Each Variant/Board pair uses its own release tag, so they are all built and released independently even when they share an upstream tag sequence (e.g. both boards' Repeater builds track `repeater-v*`).

`build-targets.yaml` at the repo root is the single source of truth for roles, release channels, and which (board, role) combinations get built with which mods. The CI matrix is generated from it, not hand-maintained. Adding support for another board is normally just adding entries there plus a `variants/<board>/overrides.yaml` file; the resolver rejects a mod when the board does not satisfy its required capabilities.

## How It Works

Every morning a GitHub Actions run checks upstream MeshCore for new release tags, one per variant.

When it finds one:

1. Clone upstream at that tag.
2. Check the plan — every patch's dependencies come first, and every board can actually do what its mods ask of it.
3. Copy in each mod's own source files.
4. Generate the glue that wires those mods into upstream.
5. Apply the patches, testing each one against the source before it goes in.
6. Build.
7. Stamp each build so the modifications it includes are identifiable in the binary itself.
8. Boot it under emulation and confirm it comes up, on the boards set up for that.

**It is pass or fail, with no middle.** Every patch has to apply cleanly, the stamped image has to verify, and it has to boot. Miss any one of those and that build is dead and an issue is opened naming what broke. There is no partial build and no "close enough."

Usually the news arrives earlier than that. `patch-drift-canary` runs the same applicability check every day against upstream's development branches, so drift tends to show up before there is a release to break.

**The flasher configures itself.** After a successful build, `pages/flasher/auto_boards.json` is regenerated from the board overrides, upstream's board information, and the actual `partitions.bin` the build produced. Nothing about it is hand-maintained.

**The flasher waits for all of them.** It updates only after every board and variant has succeeded, so a half-finished matrix never puts a broken option in front of someone flashing a device.

The published `.bin` carries its own SHA-256 and its build identity inside the image, so there is no separate checksum file to keep in step with it.

Builds can also be started by hand from the Actions tab, against a specific upstream ref or a single variant instead of the whole matrix.

## Releases

Releases are named using the variant and the upstream tag they were built from.

For example:

`Repeater v1.16.0 - mobmesh`

Each release includes:

* `<asset-basename>-vX.Y.Z.bin` — the app image, for an OTA slot or an update over an existing install
* `<asset-basename>-vX.Y.Z-merged.bin` — the same firmware plus bootloader, partition table and otadata, written at offset 0 to a blank board

The release notes come directly from the upstream MeshCore release for that tag.

You can flash the `.bin` file the same way you would flash an official MeshCore release. You can also use the web-based flasher provided by this project.

## Requirements

You need an ESP32 board that is already supported by MeshCore and has a matching `variants/<board>/overrides.yaml` file in this repository.

Some mods may also require additional hardware -or- manual configuration overrides due to RAM and storage limitations.

For example, `hotspot-ota` requires an external power switch to control an external cellular hotspot. Check the README for the mod you are using for the wiring and hardware requirements.

## About

Custom firmware for MeshCore by Mobmesh a member of GulfCoastMesh
