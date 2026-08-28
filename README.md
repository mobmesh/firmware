# Firmware - by GulfCoastMesh Mobile

Custom firmware for MeshCore running on ESP32 boards.

🔗 ["Give me the sales pitch, why do I need this?"](https://github.com/mobmesh/firmware/blob/main/README_PITCH.md)

This project layers its changes onto a fresh copy of the upstream MeshCore source instead of maintaining a separate long-term fork. Each build starts with the latest upstream release: each mod's own source files are copied in, then a small set of patches edits the upstream files that need to call them. That keeps the project current without slowly drifting out of sync with MeshCore.

The MeshCore source code itself is not stored in this repository. Instead, this repo contains the patches, board-specific configuration, and the GitHub Actions workflow that puts everything together and publishes the builds.

## Available Mods

Mods add features or changes to the standard MeshCore firmware. Each mod is maintained as a separate set of patches and can include its own board configuration and documentation.

| Mod           | Description                                                                                                                                                                                                                   | Main Features                                                                                                                                                                                         |
| ------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| [`hotspot-ota`](https://github.com/mobmesh/firmware/tree/main/mods/hotspot-ota) | Adds remote firmware updates over WiFi ( or cellular hotspot) and automatic rollback protection. A device can connect to an existing WiFi network, download a firmware image, verify it, and install it without needing to be onsite with the node. | Remote OTA updates, power control of external cell modems, firmware SHA-256 verification, firmware authenticity checks, OTA slot management, automatic rollback / recovery after failed updates, automatic clock sync via NTP whenever WiFi is joined, remote updates through MeshCore CLI commands |
| [`timing-safety`](https://github.com/mobmesh/firmware/tree/main/mods/timing-safety) | Small fixes for how the firmware tracks time. Keeps timers working correctly on devices that run for many weeks, and stops "time since last heard from" numbers from showing garbage right after a reboot. | Long-uptime timer fix, safer elapsed-time math across reboots |
| [`power-guard`](https://github.com/mobmesh/firmware/tree/main/mods/power-guard) | Keeps a bad situation from becoming an unrecoverable one, and puts the battery under its own management. Brownouts happen -- a flat pack, a cold morning, a cloudy week. Left alone, a node that browns out reboots straight into a loop that burns whatever charge is left and ends in a trip up the tower. This hibernates before it gets there, retries on a widening schedule, and comes back by itself once the battery does. Beyond the standard `powersaving on` / `off` it adds `powersaving auto`, which saves power only when the battery says to, and `powersaving safe`, the brownout failsafe. It also stops a mistyped `poweroff` from ending a node permanently. | Hibernation before the bootloop threshold, automatic recovery, power saving that engages only when it's needed, thresholds set over serial or the mesh and kept across reboots, `poweroff` requires a wake time and is refused over the mesh |

More information about each mod can be found in its own README under `mods/<name>/`.

## Web-Based Flasher

You can use the ⚡️[web-based flasher](https://tools.mobmesh.org/flasher) to flash a supported devices directly from your browser over USB.

The flasher requires Chrome, Edge, or Opera because it uses Web Serial. No additional software is needed.

It automatically uses the most recently built firmware for the board and variant you select.

This is not the official MeshCore flasher. It was <ins>built specifically</ins> for the custom firmware releases in this project.

Some mods may add extra options or requirements for a traditional flasher. Check the README for the specific mod if you need more information. For example, `hotspot-ota` changes how images are assigned to a device's OTA slots to support auto-recovery — see that mod's README for details.



# Behind the Curtain

* `mods/<name>/` contains the different features or modifications. A mod ships code two ways:

  * `files/` mirrors the upstream directory layout and is copied into the source tree before anything is patched. This is the mod's own code -- files nothing upstream has ever seen, so there is no diff to apply and they are edited directly.
  * `patches/*.patch` carries only the edits to files this mod did not create -- the upstream MeshCore source, or a file another mod added. Each has a `.meta.yaml` sidecar declaring its dependencies.

  `mod.yaml` holds facts about the mod as a whole -- its build flags and, where it has one, its image marker bit. Each mod also has its own documentation. The feature mods are `hotspot-ota`, `timing-safety` and `power-guard`; `shim` is internal plumbing that owns the hook points the others attach to.

* `variants/<board>/` contains configuration changes that are specific to a board. This includes things like GPIO pins, timing values, and sometimes a custom partition layout. The folder structure follows the same `variants/<board>/` layout used by upstream MeshCore.

  We don't copy board information that MeshCore already knows about. Things like the MCU, flash size, USB IDs, and PSRAM settings are taken directly from the upstream `boards/<board>.json` file.

* `scripts/generate-board-config.py` builds the final board configuration using the board's `overrides.yaml`, the board information from upstream, and the actual `partitions.bin` created during the build. This keeps us from having to manually enter the same information in multiple places.

* `scripts/rebase-patches.sh` moves the patch set onto a newer upstream ref. It is the only script here meant to be run by hand; everything else under `scripts/` and `scripts/tests/` is invoked by a workflow. Scripts a test imports are named with underscores, the rest with hyphens.

* `pages/shared/vendor/` holds third-party libraries used by more than one page, each with a `PROVENANCE.md` recording the exact release and how to re-verify it.

* `pages/flasher/` contains the web-based firmware flasher and its build files. The flasher is shared by all boards and mods, so it lives at the top level instead of under `docs/`.

* `.github/workflows/build-release.yml` handles building each board and variant against the latest upstream release and publishing the results.

Every mod's `files/` are copied in first, then patches are applied in numeric order within each mod. Every patch lists any other patches it depends on, and CI checks those dependencies before applying anything.

If a patch no longer applies cleanly to the current upstream version, the build fails and an issue is opened with the name of the affected patch. The system does not try to automatically merge or fix the patch.

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

`build-targets.yaml` at the repo root is the single source of truth for which (board, role) combinations get built and which mods each one includes -- the CI matrix is generated from it, not hand-maintained. Adding support for another board is normally just adding entries there plus a `variants/<board>/overrides.yaml` file; the mod itself should not need any changes unless the new board requires something the existing mod does not support.

## How It Works

A scheduled GitHub Actions run checks upstream for new release tags for each variant.

When a new release is found, the workflow:

1. Clones the upstream MeshCore repository at that tag.
2. Copies each in-scope mod's own source files into the tree.
3. Checks the patches against the board configuration.
4. Applies the patches.
5. Builds the firmware.

The patch checks are important because they catch configuration changes or other upstream changes that could cause problems. If a patch no longer applies, the build stops and an issue is opened identifying the patch that failed.

After a successful build, `pages/flasher/auto_boards.json` is regenerated using the board overrides, the upstream board information, and the actual `partitions.bin` from the build. This means the flasher configuration is generated from the build itself instead of being maintained separately by hand.

The firmware `.bin` file is then published as a GitHub release. It carries its own SHA-256 and its build identity inside the image, so there is no separate checksum file to keep in step with it.

The GitHub Pages flasher is only updated after every board and variant has built successfully. This means a failed build will not result in a broken version being published.

Builds can also be started manually from the GitHub Actions tab. You can choose a specific upstream ref or build only one variant instead of running the entire build matrix.

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