# MeshCore Hotspot OTA

Adds a hotspot-based over-the-air update path to [MeshCore](https://github.com/meshcore-dev/MeshCore) firmware for the Heltec V4, and automatically tracks upstream MeshCore releases.

MeshCore's built-in `start ota` command turns the device into a self-hosted WiFi access point and waits for someone to upload a `.bin` file through a web page in local proximity to the device. This project adds a second path: the device instead powers on its hotspot's external power rail (if wired), joins an existing WiFi hotspot as a client, confirms internet connectivity, downloads the firmware from a supplied URL, verifies it, and flashes itself — no laptop or phone required at the update site. These are ordinary CLI commands, so they can be triggered remotely over the mesh via LoRa, not just from a directly-connected console. A rollback guard confirms every update actually works before committing to it, automatically reverting to the previous firmware if it doesn't.

This repository does **not** contain a fork of MeshCore's source. It contains a series of patches and a GitHub Actions workflow that applies them to a fresh copy of upstream MeshCore on every build, so it stays in sync with upstream automatically rather than drifting out of date.

## Contents

| File | Purpose |
|---|---|
| `patches/0001-hotspot-fetch-ota.patch` | Adds the hotspot-fetch OTA feature to MeshCore |
| `patches/0002-ota-rollback-guard.patch` | Adds automatic post-update rollback protection. **Depends on 0001** and cannot be applied alone |
| `.github/workflows/build-release.yml` | Builds firmware for each supported variant against the latest matching upstream release and publishes it here |
| `docs/flasher/` | Browser-based USB flasher (GitHub Pages), see "Web-based flasher" below |

Patches are applied in numeric order. If a patch fails to apply against the current upstream release, the build fails and an issue is opened here identifying which patch broke — it does not attempt to auto-resolve the conflict.

## Supported variants

| Variant | Upstream tag tracked | Release asset |
|---|---|---|
| Repeater | `repeater-v*` | `heltec_v4_rep_ota-vX.Y.Z.bin` |
| Room Server | `room-server-v*` | `heltec_v4_room_ota-vX.Y.Z.bin` |

Each variant is tracked and released independently, since MeshCore versions them on separate tag sequences. Adding another variant (a different board, or another role on an existing board) is a matter of extending the workflow's build matrix and, if needed, the patches — see the workflow file for the current matrix definition.

## How it works

1. A scheduled workflow run checks upstream MeshCore for the newest release tag matching each variant (`repeater-v*`, `room-server-v*`).
2. For any variant that hasn't been built yet at its current upstream tag, the workflow clones upstream at that release, applies every patch in `patches/` in numeric order, and compiles firmware for that variant's PlatformIO environment.
3. If a patch no longer applies cleanly (upstream changed a file it touches), the build fails and an issue is opened here automatically, naming the specific patch that failed.
4. On a successful build, the compiled `.bin` and a matching `.sha256` checksum file are published as a release here, with upstream's own release notes for that tag included in the release body.

Builds also run on demand via the Actions tab (**Run workflow**), optionally targeting a specific upstream tag/branch or limiting the run to a single variant.

## Releases

Each release is named after the variant and the upstream MeshCore release it was built from, e.g. **"Repeater v1.16.0 - ota_mod"** or **"Room v1.16.0 - ota_mod"**, and contains:

- `<asset-basename>-vX.Y.Z.bin` — the firmware image (see the variant table above for exact asset names)
- `<asset-basename>-vX.Y.Z.bin.sha256` — its SHA-256 checksum

The release body includes upstream's own release notes for the exact tag the build was made from.

Flash the `.bin` the same way you would an official MeshCore firmware release for that variant, or use the web-based flasher below.

## Web-based flasher

[**Open the flasher**](https://mobmesh.github.io/meshcore-hotspot-ota/flasher/) — flashes a Heltec V4 straight from your browser over USB (Chrome, Edge, or Opera; requires Web Serial). No PlatformIO or other dev tools needed. It always uses the most recently built firmware for whichever variant you pick.

Two flows:

- **New device** — for a blank board, or one that's bricked. Fully erases the chip and writes bootloader, partition table, and firmware from scratch — into both OTA slots, so `ota slot boot <A|B>` works right away instead of requiring a separate flash into the other slot first.
- **Update existing device** — for a board already running MeshCore. Writes firmware into a chosen OTA slot (A or B) without erasing anything else. This is the only way to target a specific slot from outside the device's own CLI; it doesn't change which slot the device boots from — use `ota slot boot <A|B>` on-device for that.

This isn't MeshCore's own flasher — it's built for this repo's own releases, plus the OTA slot targeting above that MeshCore's flasher doesn't do.

## CLI commands added

These are available on any device running firmware built from these patches, in addition to all standard MeshCore CLI commands.

| Command | Description |
|---|---|
| `set ota.wan.wifi <ssid>,<password>` | Set the WiFi credentials used for future updates. Persists across firmware updates — set once. |
| `set ota.fw.sha256 <hex>` | Manually specify the expected SHA-256 of the next firmware download. Takes precedence over an automatically-fetched checksum. RAM-only — cleared on every boot. |
| `set ota.fw.sha256 clear` | Clear a manually-set checksum so an automatically-fetched one can be used again. |
| `start ota wan <url>` | Join the configured WiFi network, download the firmware at `<url>`, verify it, confirm it's actually a build of this project (refuses otherwise, even if the checksum matches), and flash it. |
| `set ota.fw.url <url>` | Persist a default firmware URL. Overwrite-only, no `clear`. |
| `start ota wan update` | Same as `start ota wan <url>`, using the persisted `ota.fw.url` instead of a URL on the command line. Errors with `ota.fw.url not configured` if none is set. Exists to keep remote admin updates short over LoRa — a full firmware URL can be well over 100 characters, this is 21. |
| `set ota.fw.marker <on\|off>` | Default `on` (marker/authenticity check enforced). One-time, RAM-only `off` bypasses that check above for the next `start ota wan` — never persisted, always back to `on` after a reboot. Never bypasses the sha256 check. Use with care: if the download turns out not to be a build of this project, this node loses remote OTA capability until it's reflashed locally (USB or on-site). |
| `ota wan join` / `ota wan leave` | Pre-flight: join the configured WiFi network only (no download), or disconnect and drop WAN power. |
| `ota wan check` | Pre-flight: check WAN reachability once `ota wan join` has joined. |
| `get ota.wan.pwr` / `set ota.wan.pwr <on\|off>` | Diagnostic/recovery command to read or directly force the WAN power switch, independent of `start ota wan`. |
| `get ota.slot` | Both OTA slots' version and state, e.g. `Slots: A=v1.16.0-0f11a30 (active, valid) \| B=v? (n/a)`. Version includes the short build commit hash (distinguishes two slots sharing the same version number but from different builds) and is `v?` for a slot that's never actually booted (self-reported into SPIFFS on first boot, since the compiled-in image header doesn't carry it). Active slot's state is `pending`/`valid`/`n/a`; the other's is `valid`/`invalid`/`aborted`/`new`/`n/a`. |
| `ota slot boot <A\|B>` | Point the bootloader at the other OTA slot and reboot into it immediately, without reflashing. Refuses if that slot is already active or has no valid image. Re-arms rollback probation for that slot even if it was previously `valid` -- expect `get ota.slot` to briefly show `pending` right after. |

Example:

```
set ota.wan.wifi MyHotspot,hunter2
start ota wan https://example.com/firmware/heltec_v4_repeater-v1.16.0.bin
```

If a file named `<url>.sha256` exists alongside the firmware, it's fetched automatically and used to verify the download — no manual checksum needed. Full parameter and usage details for these commands, in the same format as upstream's own CLI reference, are in [`docs/cli-additions.md`](docs/cli-additions.md). See [`docs/cli_commands.md`](https://github.com/meshcore-dev/MeshCore/blob/main/docs/cli_commands.md) in upstream MeshCore for the complete standard CLI reference.

For remote admin updates over LoRa, where every character sent counts, set `ota.fw.url` once to this project's own published firmware asset and use the short form after that:

```
set ota.fw.url https://github.com/mobmesh/meshcore-hotspot-ota/raw/refs/heads/main/docs/flasher/heltec_v4/repeater/firmware.bin
start ota wan update
```

**`start ota wan` does not reply until it finishes.** Unlike most CLI commands, there is no immediate acknowledgment and no progress update — the device is joining WiFi, downloading, verifying, and flashing before it sends anything back, which can take up to about two minutes. The device will reboot and mount the new firmware image and begin automatic rollback protection testing. 

## Automatic rollback protection

This is added behavior, not something stock MeshCore does.

**Upstream MeshCore's default:** if a bad update boots and `radio_init()` fails, stock MeshCore just calls `halt()` — the device sits there, unresponsive, with no automatic path back to the last known-good firmware.

**What this patch changes:** it defers that confirmation instead of letting it happen automatically. A newly-updated image (via either `start ota` or `start ota wan`) is held on probation — not yet confirmed — until it's run stably for about 90 seconds with a working radio. If `radio_init()` fails while still on probation, that's treated as evidence the update itself is bad, and the device immediately rolls back and reboots into the previous working firmware instead of halting. Radio failures unrelated to an update still get a capped number of retry-reboots before permanently halting, rather than looping forever. No additional hardware is required — this relies entirely on ESP-IDF's app-rollback feature, which is already compiled into MeshCore's upstream toolchain.

**Why:** MeshCore's default behavior of just halting on a radio initialization failure is a real risk especially for a node that's physically remote and can't be walked over to and re-flashed. Without this patch, any bad firmware update can brick a node, leaving it non-responsive to any radio commands. With this patch, the device can self-recover.

## Requirements

- Heltec V4 hardware (GPIO47 is confirmed clean for this on the V4.3.1 revision; GPIO48 is a documented fallback if 47 is unavailable), with an external switch controlling power to a WiFi hotspot device.
- If the switch is a load-switch IC (e.g. TI's TPS22995, rated up to 3.8A continuous), note it is not a latch — its `ON` control line must be held continuously high for the duration of an update, not pulsed. A hardware pulldown on the control line is recommended so the rail defaults to off on any reset, independent of firmware state.
