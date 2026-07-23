# MeshCore Hotspot OTA

Adds a self-hosted, WiFi-hotspot-based over-the-air update path to [MeshCore](https://github.com/meshcore-dev/MeshCore) firmware for the Heltec V4, and automatically tracks upstream releases.

MeshCore's built-in `start ota` command turns the device into a WiFi access point and waits for someone to upload a `.bin` file through a web page. This project adds a second path: the device instead joins an existing WiFi hotspot as a client, downloads the firmware from a URL, verifies it, and flashes itself — no laptop or phone required at the update site.

This repository does **not** contain a fork of MeshCore's source. It contains a patch and a GitHub Actions workflow that applies the patch to a fresh copy of upstream MeshCore on every build, so it stays in sync with upstream automatically rather than drifting out of date.

## Contents

| File | Purpose |
|---|---|
| `patches/0001-hotspot-fetch-ota.patch` | Unified diff adding the hotspot-fetch OTA feature to MeshCore |
| `.github/workflows/build-release.yml` | Builds firmware against the latest upstream release and publishes it here |

## How it works

1. A scheduled workflow run checks upstream MeshCore for the newest `repeater-v*` release.
2. If it hasn't been built yet, the workflow clones upstream at that release, applies the patch, and compiles firmware for the Heltec V4 repeater target.
3. If the patch no longer applies cleanly (upstream changed a file it touches), the build fails and an issue is opened here automatically — it does not attempt to auto-resolve the conflict.
4. On a successful build, the compiled `.bin` and a matching `.sha256` checksum file are published as release assets in this repository.

Builds also run on demand via the Actions tab (**Run workflow**), optionally targeting a specific upstream tag or branch.

## Releases

Each release is named after the upstream MeshCore release it was built from and contains:

- `heltec_v4_rep_ota_mod-vX.Y.Z.bin` — the firmware image
- `heltec_v4_rep_ota_mod-vX.Y.Z.bin.sha256` — its SHA-256 checksum

Flash the `.bin` the same way you would an official MeshCore repeater firmware release.

## CLI commands added

These are available on any device running firmware built from this patch, in addition to all standard MeshCore CLI commands.

| Command | Description |
|---|---|
| `set ota.wifi <ssid>,<password>` | Set the WiFi hotspot credentials used for future updates. Persists across firmware updates — set once. |
| `set ota.sha256 <hex>` | Manually specify the expected SHA-256 of the next firmware download. Takes precedence over an automatically-fetched checksum. |
| `set ota.sha256 clear` | Clear a manually-set checksum so an automatically-fetched one can be used again. |
| `start ota url <url>` | Join the configured WiFi hotspot, download the firmware at `<url>`, verify it, and flash it. |
| `get ota.pwr` / `set ota.pwr <on\|off>` | Diagnostic/recovery command to read or directly force the hotspot power switch, independent of `start ota url`. |

Example:

```
set ota.wifi MyHotspot,hunter2
start ota url https://example.com/firmware/heltec_v4_repeater-v1.16.0.bin
```

If a file named `<url>.sha256` exists alongside the firmware, it's fetched automatically and used to verify the download — no manual checksum needed. Full parameter and usage details for these commands, in the same format as upstream's own CLI reference, are in [`docs/cli-additions.md`](docs/cli-additions.md). See [`docs/cli_commands.md`](https://github.com/meshcore-dev/MeshCore/blob/main/docs/cli_commands.md) in upstream MeshCore for the complete standard CLI reference.

**`start ota url` does not reply until it finishes.** Unlike most CLI commands, there is no immediate acknowledgment and no progress update — the device is joining WiFi, downloading, verifying, and flashing before it sends anything back, which can take up to about two minutes. A single final reply arrives when it succeeds or fails; silence in between is expected, not a sign the command was lost.

## Requirements

- Heltec V4 hardware (GPIO47 is confirmed clean for this on the V4.3.1 revision; GPIO48 is a documented fallback if 47 is unavailable), with an external switch controlling power to a WiFi hotspot device.
- If the switch is a load-switch IC (e.g. TI's TPS22995, rated up to 3.8A continuous), note it is not a latch — its `ON` control line must be held continuously high for the duration of an update, not pulsed. A hardware pulldown on the control line is recommended so the rail defaults to off on any reset, independent of firmware state.
- Firmware built with the `WITH_HOTSPOT_OTA` build flag, which the patch enables on the `heltec_v4_repeater` environment.

## Security notes

- Firmware integrity is verified by SHA-256 checksum, not by validating the download server's TLS certificate.
- A manually-set checksum (`set ota.sha256`) is the source of truth once set and is not overridden by an automatically-fetched one — clear it before pointing at a different firmware image.
- Downloads over `https://` are supported, though certificate validation is not enforced by default.

## Building locally

```
git clone https://github.com/meshcore-dev/MeshCore.git
cd MeshCore
git apply /path/to/0001-hotspot-fetch-ota.patch
pio run -e heltec_v4_repeater
```

## Disclaimer

This project is not affiliated with or endorsed by the MeshCore project. It distributes a patch against MeshCore's source and compiled binaries built from that patch. See [meshcore-dev/MeshCore](https://github.com/meshcore-dev/MeshCore) for the upstream project.

## License

MeshCore is released under the [MIT License](https://github.com/meshcore-dev/MeshCore/blob/main/license.txt). The patch and workflow files in this repository are released under the MIT License as well.
