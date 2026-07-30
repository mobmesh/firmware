# Firmware

Custom firmware for [MeshCore](https://github.com/meshcore-dev/MeshCore) on ESP32 boards, built from a maintained set of patches instead of a long-lived fork. Every build pulls a fresh copy of upstream and applies our patches on top, so we stay current with upstream releases automatically rather than slowly drifting out of sync.

There's no MeshCore source checked into this repo. What lives here is the patches (grouped by feature under `mods/`), per-board config overrides, and the GitHub Actions workflow that stitches it all together and publishes the result.

## Layout

- **`mods/<name>/`** — one folder per feature. Each has its own `patches/*.patch`, a `.meta.yaml` sidecar per patch declaring dependencies, and its own docs. Right now `hotspot-ota` is the only one — see [its README](mods/hotspot-ota/README.md).
- **`variants/<board>/`** — board config, but only the parts we're adding on top of upstream (GPIO pins, timing constants, occasionally a custom partition scheme). Named to match upstream's own `variants/<board>/` layout. Anything upstream already knows about a board — mcu, flash size, USB IDs, psram — gets read from their `boards/<board>.json` instead of copied here. See [`variants/heltec_v4/README.md`](variants/heltec_v4/README.md).
- **`scripts/generate-board-config.py`** — takes a board's `overrides.yaml`, upstream's own board facts, and the actual built `partitions.bin`, and turns them into build flags plus `pages/flasher/boards.json`. Nothing gets typed in twice.
- **`pages/flasher/`** — the browser flasher and its build assets, served via GitHub Pages. This is shared across every board and mod, so it lives at the top level rather than under `docs/` (which is just for documentation, not binaries).
- **`.github/workflows/build-release.yml`** — builds each board/variant against the latest upstream release and publishes it.

Patches apply in numeric order within a mod, and each one's sidecar lists which other patches it needs — CI checks that before touching anything. If a patch doesn't apply cleanly against the current upstream tag, the build fails and opens an issue naming the patch. No auto-merge attempts.

## Supported boards

| Variant | Board | Upstream tag | Release asset |
|---|---|---|---|
| Repeater | Heltec V4 | `repeater-v*` | `heltec_v4_rep_ota-vX.Y.Z.bin` |
| Room Server | Heltec V4 | `room-server-v*` | `heltec_v4_room_ota-vX.Y.Z.bin` |

Repeater and Room Server track separate upstream tag sequences, so they're built and released independently. Adding a new board is mostly just adding a `variants/<board>/overrides.yaml` and a line in the build matrix — the mod itself doesn't need to change unless the new board needs something the mod doesn't already handle.

## How it works

A scheduled run checks upstream for a new release tag on each variant. When one shows up, the workflow clones upstream at that tag, checks the patches against the board's overrides (this is where config drift would get caught), applies the patches, and builds. If a patch doesn't apply, the build stops there and files an issue naming it.

Once the build succeeds, `pages/flasher/boards.json` gets regenerated straight from the board's overrides, upstream's own facts, and the actual `partitions.bin` that just got built — nothing here is something we typed in by hand. The `.bin` and a `.sha256` get published as a release, and only after every board/variant has built successfully does a separate job push `pages/` to GitHub Pages. A broken build can't publish anything.

You can also kick off a build manually from the Actions tab, and point it at a specific upstream ref or a single variant if you don't want the full matrix.

## Releases

Releases are named after the variant and the upstream tag they were built from — e.g. "Repeater v1.16.0 - ota_mod" — and each one ships:

- `<asset-basename>-vX.Y.Z.bin` — the firmware image
- `<asset-basename>-vX.Y.Z.bin.sha256` — its checksum

The release notes are just upstream's own notes for that tag. Flash the `.bin` the same way you'd flash an official MeshCore release, or use the flasher below.

## Web-based flasher

[**Open the flasher**](https://mobmesh.github.io/firmware/flasher/) — flashes a board straight from your browser over USB. Needs Chrome, Edge, or Opera (Web Serial support), nothing else. It always grabs whatever was most recently built for the board/variant you pick.

It's not MeshCore's own flasher, just one built for our releases. Check a mod's own README if it adds anything flasher-specific — `hotspot-ota` adds OTA-slot targeting, for instance.

## Requirements

- An ESP32 board MeshCore already supports, with a `variants/<board>/overrides.yaml` here for whatever this project adds on top.
- Any hardware a given mod needs — `hotspot-ota` wants an external power switch for its WiFi hotspot, for example. Check that mod's README for wiring details.
