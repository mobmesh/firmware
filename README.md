# Firmware - by GulfCoastMesh Mobile

Custom firmware for MeshCore running on ESP32 boards.

This project layers its changes onto a fresh copy of the upstream MeshCore source instead of maintaining a separate long-term fork. Each build starts with the latest upstream release: each mod's own source files are copied in, then a small set of patches edits the upstream files that need to call them. That keeps the project current without slowly drifting out of sync with MeshCore.

The MeshCore source code itself is not stored in this repository. Instead, this repo contains the patches, board-specific configuration, and the GitHub Actions workflow that puts everything together and publishes the builds.

## Available Mods

Mods add features or changes to the standard MeshCore firmware. Each mod can ship owned source, integration declarations, upstream patches, board configuration, and documentation.

| Mod           | Description                                                                                                                                                                                                                   | Main Features                                                                                                                                                                                         |
| ------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| [`hotspot-ota`](https://github.com/mobmesh/firmware/tree/main/mods/hotspot-ota) | Adds remote firmware updates over WiFi ( or cellular hotspot) and automatic rollback protection. A device can connect to an existing WiFi network, download a firmware image, verify it, and install it without needing to be onsite with the node. | Remote OTA updates, power control of external cell modems, firmware SHA-256 verification, firmware authenticity checks, OTA slot management, automatic rollback / recovery after failed updates, automatic clock sync via NTP whenever WiFi is joined, remote updates through MeshCore CLI commands, downloads that run in the background so the node keeps repeating, and an update in progress that can be cancelled |
| [`timing-safety`](https://github.com/mobmesh/firmware/tree/main/mods/timing-safety) | Small fixes for how the firmware tracks time. Keeps timers working correctly on devices that run for many weeks, and stops "time since last heard from" numbers from showing garbage right after a reboot. | Long-uptime timer fix, safer elapsed-time math across reboots |
| [`try-settings`](https://github.com/mobmesh/firmware/tree/main/mods/try-settings) | Lets you try a setting before you commit to it. Some settings can cause loss of communication with a node the moment they take effect -- a radio.rxgain change, a tx value change, a radio.fem.txgain, etc -- and once it is out of reach you cannot put it back. tryset applies the change, starts a clock, and puts the old value back on its own unless you confirm the node is still reachable with a tryset keep command. If it goes quiet instead, the node recovers by itself. | `tryset <secs> <key> <value>` with automatic revert, `tryset keep` to commit and `tryset revert` to undo early, an allowlist of the settings worth trialling (gain, power, site RF, and the ones that risk stranding a node), radio parameters proxied to MeshCore's own `tempradio`, the rollback value written to flash before the setting moves, and a trial that ends rather than resumes after an unexpected reboot |
| [`sync-settings`](https://github.com/mobmesh/firmware/tree/main/mods/sync-settings) | Keeps repeaters current with the local mesh without each operator chasing changes. Subscribe to a publisher you trust, and region lists plus a handful of operating settings arrive over the mesh and apply themselves. Your own region list is never touched -- synced regions live in a separate layer you can switch off any time. | Signed region and policy updates over LoRa, a separate region overlay with 32 extra slots, trusted-publisher list, replay protection, lost chunks recovered on later rounds, publish and abort from the CLI |
| [`shim`](https://github.com/mobmesh/firmware/tree/main/mods/shim) | The plumbing the other mods plug into. On its own it does nothing -- the firmware behaves exactly like upstream. It's the one place that touches upstream's code, so the feature mods don't have to. | Hook points for startup, the main loop, power saving and the CLI, generated at build time from each mod's declarations |
| [`power-guard`](https://github.com/mobmesh/firmware/tree/main/mods/power-guard) | Keeps a bad situation from becoming an unrecoverable one, and puts the battery under its own management. Brownouts happen -- a flat pack, a cold morning, a cloudy week. Left alone, a node that browns out reboots straight into a loop that burns whatever charge is left and ends in a trip up the tower. This hibernates before it gets there, retries on a widening schedule, and comes back by itself once the battery does. Beyond the standard `powersaving on` / `off` it adds `powersaving auto`, which saves power only when the battery says to, and `powersaving safe`, the brownout failsafe. It also stops a mistyped `poweroff` from ending a node permanently. | Hibernation before the bootloop threshold, automatic recovery, power saving that engages only when it's needed, thresholds set over serial or the mesh and kept across reboots, `poweroff` requires a wake time and is refused over the mesh |

More information about each mod can be found in its own README under `mods/<name>/`.

### Web-Based Flasher
⚡️[our web-based flasher](https://tools.mobmesh.org/flasher)

This isn't a generic MeshCore flasher. It's custom built for this project's releases and offers features not found in others.

The  sets up a MeshCore device right from your browser. 
Plug in over USB, answer a few questions, and it handles the rest.
It needs Chrome, Brave, Edge or Opera (anything with Web Serial). No other software.

It walks you through it step by step:

- **New or upgrade?** Upgrading keeps the device's identity and settings, even when the
  new firmware uses a different partition layout.
- **Which firmware?** MeshCore Enhanced+ is this project's build, used when your board is
  supported. MeshCore Standard is stock upstream. You can also upload your own `.bin`.
- **Which device and role?** Pick your hardware, then repeater, room server or client.
  It always offers the latest build for that combination.
- **Post-flash setup:** Once the device reboots, the flasher can apply your group's
  regional settings, name and location, and everything else you need to get started on the mesh.


## A Look Under the Hood

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

**The flasher configures itself.** After a successful build, `pages/flasher/data/auto_boards.json` is regenerated from the board overrides, upstream's board information, and the actual `partitions.bin` the build produced. Nothing about it is hand-maintained.

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


## About

Custom firmware for MeshCore by Mobmesh a member of GulfCoastMesh
