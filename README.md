# MobMesh firmware

MeshCore Enhanced+ is custom MeshCore firmware for ESP32 boards by MobMesh, a
GulfCoastMesh member. It is MeshCore with added safety protections and features built in.
It builds against new upstream MeshCore releases as they occur.
This repository holds the mods, board configuration, patches and release workflow.
Each build checks out the upstream release and applies the selected changes; upstream
source is not stored here.

## Start here

- [Flash a device](https://tools.mobmesh.org/flasher) from a Web Serial browser.
- Use the [owner's manual](docs/owners-manual.md) for setup examples, workflows and CLI commands.

## Mods
Select a mod link below for its own README, including behavior and requirements.
| Mod | What it adds |
| --- | --- |
| [hotspot‑ota](mods/hotspot-ota/README.md) | Fully remote firmware updates (off-site), local upload improvements and rollback protection. |
| [sync‑settings](mods/sync-settings/README.md) | Signed region, policy, radio and time campaigns sent across the mesh by trusted publishers. |
| [try‑settings](mods/try-settings/README.md) | Timed setting trials that revert unless the operator confirms the node still works. |
| [power‑guard](mods/power-guard/README.md) | Battery-aware power saving, brownout protection, safe sleep and recovery for supported repeaters. |
| [timing‑safety](mods/timing-safety/README.md) | Timer rollover and post-reboot clock fixes. |
| [shim](mods/shim/README.md) | Shared upstream hooks that connect selected mods to MeshCore. |

The mod set varies by build target. `build-targets.yaml` shows which
mods each board and role receives.

## Flash a device

The [web flasher](https://tools.mobmesh.org/flasher) guides a new installation or an
upgrade over USB. Choose **MeshCore Enhanced+** for this project's builds, **MeshCore
Standard** for stock upstream firmware, or supply your own `.bin` file. Then choose the
board and role, flash, and use post-flash setup to set the node's name, location and
regional settings.

An upgrade preserves the device's identity and settings, including when the new firmware
uses a different partition layout. The flasher needs a browser with Web Serial, such as
Chrome, Brave, Edge or Opera.

## How releases are built

The build workflow checks daily for the latest upstream release tag for each role. It
can also be run manually for a selected upstream ref or variant. For each target that
needs a build, it:

1. Checks out upstream MeshCore and validates the selected mods and board capabilities.
2. Copies mod-owned source, generates integration code and applies the required patches.
3. Builds and stamps the firmware with its board, role and mod identity.
4. Checks the image and, where configured, boots it under emulation.
5. Packages successful app and merged images for publication.

## Verification

The separate repository checks workflow runs **191 automated tests at this revision**
when relevant source or configuration changes. They check patch ownership, board
configuration, generated files and vendored images. Release builds have their own
per-target gates:

- Each patch must apply cleanly to the selected upstream release, and the target must compile.
- The workflow checks that the rollback hook still exists, the app fits its OTA partition,
  and the stamped image reports its own metadata correctly.
- Server-side checks boot the **same stamped app image** that will be published and
  confirm that it reaches the CLI. They exercise OTA commands when hotspot-ota is present,
  and the image's mod stamp is based on markers found in the compiled binary. This boot
  check runs where an emulator is configured; a silent or crashing boot fails the target.

The patch drift canary checks upstream development branches daily so patch conflicts
can be found before a release.

**A failed target is not published.**

The flasher catalogue is generated from board overrides, upstream board information and
the built partition table. It is not maintained by hand. OTA board IDs are permanent:
the board ID registry retains retired assignments, and active boards must match their
`overrides.yaml` entry.

## Release files

Each role uses an upstream version in its release title and tag. For an upstream
`repeater-v1.17.1` release, the published names are:

| Item | Example |
| --- | --- |
| Release title | `Repeater v1.17.1 - mobmesh` |
| Release tag | `repeater-v1.17.1-mobmesh` |
| Heltec V4 repeater app | `heltec_v4_rep_mobmesh-v1.17.1.bin` |
| Heltec V4 repeater merged image | `heltec_v4_rep_mobmesh-v1.17.1-merged.bin` |

Each built target provides an app image for an OTA slot or existing installation and a
merged image for flashing a blank board at offset 0. The merged image includes the app,
bootloader, partition table and OTA data. Beta targets add `_beta` to the asset basename.
