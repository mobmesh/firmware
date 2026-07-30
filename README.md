# Firmware

Custom firmware for MeshCore running on ESP32 boards.

This project uses a set of patches that are applied to a fresh copy of the upstream MeshCore source instead of maintaining a separate long-term fork. Each build starts with the latest upstream release and applies our changes on top of it. This makes it easier to stay up to date without the project slowly getting out of sync with MeshCore.

The MeshCore source code itself is not stored in this repository. Instead, this repo contains the patches, board-specific configuration, and the GitHub Actions workflow that puts everything together and publishes the builds.

## Repository Layout

* `mods/<name>/` contains the different features or modifications. Each mod has its own `patches/*.patch` files, along with a `.meta.yaml` file for each patch. The metadata files define patch dependencies. Each mod also has its own documentation. Currently, `hotspot-ota` is the only mod.

* `variants/<board>/` contains configuration changes that are specific to a board. This includes things like GPIO pins, timing values, and sometimes a custom partition layout. The folder structure follows the same `variants/<board>/` layout used by upstream MeshCore.

  We don't copy board information that MeshCore already knows about. Things like the MCU, flash size, USB IDs, and PSRAM settings are taken directly from the upstream `boards/<board>.json` file.

* `scripts/generate-board-config.py` builds the final board configuration using the board's `overrides.yaml`, the board information from upstream, and the actual `partitions.bin` created during the build. This keeps us from having to manually enter the same information in multiple places.

* `pages/flasher/` contains the web-based firmware flasher and its build files. The flasher is shared by all boards and mods, so it lives at the top level instead of under `docs/`.

* `.github/workflows/build-release.yml` handles building each board and variant against the latest upstream release and publishing the results.

Patches are applied in numeric order within each mod. Every patch also lists any other patches it depends on. CI checks these dependencies before applying anything.

If a patch no longer applies cleanly to the current upstream version, the build fails and an issue is opened with the name of the affected patch. The system does not try to automatically merge or fix the patch.

## Supported Boards

| Variant     | Board     | Upstream Tag     | Release Asset                   |
| ----------- | --------- | ---------------- | ------------------------------- |
| Repeater    | Heltec V4 | `repeater-v*`    | `heltec_v4_rep_ota-vX.Y.Z.bin`  |
| Room Server | Heltec V4 | `room-server-v*` | `heltec_v4_room_ota-vX.Y.Z.bin` |

The Repeater and Room Server variants use separate upstream tag sequences, so they are built and released independently.

Adding support for another board should be fairly simple. In most cases, you only need to add a `variants/<board>/overrides.yaml` file and add the board to the build matrix. The mod itself should not need any changes unless the new board requires something that the existing mod does not support.

## How It Works

A scheduled GitHub Actions run checks upstream for new release tags for each variant.

When a new release is found, the workflow:

1. Clones the upstream MeshCore repository at that tag.
2. Checks the patches against the board configuration.
3. Applies the patches.
4. Builds the firmware.

The patch checks are important because they catch configuration changes or other upstream changes that could cause problems. If a patch no longer applies, the build stops and an issue is opened identifying the patch that failed.

After a successful build, `pages/flasher/boards.json` is regenerated using the board overrides, the upstream board information, and the actual `partitions.bin` from the build. This means the flasher configuration is generated from the build itself instead of being maintained separately by hand.

The firmware `.bin` file and its `.sha256` checksum are then published as a GitHub release.

The GitHub Pages flasher is only updated after every board and variant has built successfully. This means a failed build will not result in a broken version being published.

Builds can also be started manually from the GitHub Actions tab. You can choose a specific upstream ref or build only one variant instead of running the entire build matrix.

## Releases

Releases are named using the variant and the upstream tag they were built from.

For example:

`Repeater v1.16.0 - ota_mod`

Each release includes:

* `<asset-basename>-vX.Y.Z.bin` — the firmware image
* `<asset-basename>-vX.Y.Z.bin.sha256` — the checksum for the firmware image

The release notes come directly from the upstream MeshCore release for that tag.

You can flash the `.bin` file the same way you would flash an official MeshCore release. You can also use the web-based flasher provided by this project.

## Web-Based Flasher

You can use the [web-based flasher](https://mobmesh.github.io/) to flash a supported board directly from your browser over USB.

The flasher requires Chrome, Edge, or Opera because it uses Web Serial. No additional software is needed.

It automatically uses the most recently built firmware for the board and variant you select.

This is not the official MeshCore flasher. It was built specifically for the firmware releases in this project.

Some mods may add extra options or requirements to the flasher. Check the README for the specific mod if you need more information. For example, `hotspot-ota` adds support for selecting the OTA slot that should be used.

## Requirements

You need an ESP32 board that is already supported by MeshCore and has a matching `variants/<board>/overrides.yaml` file in this repository.

Some mods may also require additional hardware.

For example, `hotspot-ota` requires an external power switch for its WiFi hotspot. Check the README for the mod you are using for the wiring and hardware requirements.

## About

Custom firmware for MeshCore.
