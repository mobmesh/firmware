# hotspot-ota - Remote OTA Firmware Updates for MeshCore

Adds a WiFi-based over-the-air update path to MeshCore, along with automatic rollback protection if an update doesn't work correctly.

MeshCore already has a built-in `start ota` command. This starts a WiFi access point on the device and lets someone nearby upload a `.bin` firmware file through a web page.

This mod adds another way to update the device. Instead of creating its own WiFi network, the device powers on the external power rail for its hotspot, if one is connected, and joins an existing WiFi network. It checks that the network has internet access, downloads the firmware from a URL, verifies it, and then installs it.

This means you don't need to bring a laptop or phone to the location of the device just to perform an update.

The commands are also regular MeshCore CLI commands, so they can be sent remotely over the LoRa mesh. You don't have to be connected directly to the device.

The mod also adds rollback protection. After an update, the new firmware is tested before it is considered good. If the new firmware fails during startup, the device automatically goes back to the previous working firmware.

## What ships

| Where | What |
| ----- | ---- |
| `files/src/helpers/esp32/HotspotOTA.{cpp,h}` | hotspot join, fetch, verify, flash, NTP clock set |
| `files/src/helpers/esp32/HotspotOtaBoard.cpp` | `modBoardStartOtaFromUrl()`, reached from the CLI hook |
| `files/src/helpers/esp32/HotspotOtaIntegration.{cpp,h}` | radio policy, loop polling, and CLI handler |
| `files/src/helpers/esp32/RollbackGuard.{cpp,h}` | rollback protection after an update |

`files/` is copied into the upstream clone before composition. `mod.yaml` declares the
integration phases, and the shim generator wires them into its aggregate sources. This mod
has no patch and touches no upstream or shim-owned source file.

The patches don't contain board-specific settings such as the GPIO pin used for the power switch or WiFi and HTTP timing values.

Those settings come from each board's `variants/<board>/overrides.yaml` file and are passed into the build as `-D` flags. See the root README for more information about how board configuration works.

## Hardware Requirements

This mod expects an external switch on a GPIO pin controlling power to a WiFi hotspot or cellular modem. The firmware raises that pin before joining WiFi and drops it again when it's finished, so the hotspot only draws power during an update.

The pin is held high for the whole update rather than pulsed. If the switch is a load-switch IC, its enable line needs to stay asserted the entire time, and a hardware pulldown on that line is recommended so the rail defaults to off after any reset regardless of what the firmware is doing.

**Which GPIO to use is per-board.** See `variants/<board>/README.md` for the confirmed pin, any documented fallback, and other wiring notes for that board. The machine-readable value lives beside it in `variants/<board>/overrides.yaml`.

Rollback protection and clock sync need no extra hardware. Only the hotspot power control does.

## CLI Commands

These commands are available on devices built with these patches. They can be used alongside the standard MeshCore CLI commands.

| Command                                         | Description                                                                                                                                                                                                                                                                   |
| ----------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `set ota.wan.wifi <ssid>,<password>`            | Saves the WiFi network and password that will be used for future updates. The settings survive firmware updates, so you only need to set them once.                                                                                                                           |
| `set ota.fw.sha256 <hex>`                       | Sets the expected SHA-256 checksum for the next firmware download. This takes priority over a checksum downloaded automatically. The value is kept in RAM and is cleared after every reboot.                                                                                  |
| `set ota.fw.sha256 clear`                       | Clears a manually configured checksum so the automatically downloaded checksum can be used again.                                                                                                                                                                             |
| `start ota wan <url>`                           | Connects to the configured WiFi network, downloads the firmware from `<url>`, verifies it, checks that it is actually a build from this project, and flashes it.                                                                                                              |
| `set ota.fw.url <url>`                          | Saves a default firmware URL. This setting can only be overwritten and cannot be cleared.                                                                                                                                                                                     |
| `start ota wan update`                          | Same as `start ota wan <url>`, but uses the saved `ota.fw.url`. If no URL has been configured, the command returns `ota.fw.url not configured`. This shorter command is useful for remote updates over LoRa, where every character matters.                                   |
| `get ota.status`                                | Reports the current OTA service state, download progress, or the final failure/cancellation result.                                                                                                                                                                           |
| `ota cancel`                                    | Requests cancellation while the service is queued, joining, checking connectivity, opening the URL, or downloading.                                                                                                                                                           |
| `set ota.fw.marker <on\|off>`                   | Controls the firmware authenticity check. It is `on` by default. Setting it to `off` temporarily disables the check for the next `start ota wan` command. The setting is stored only in RAM and is turned back on after a reboot. The SHA-256 check is still always enforced. |
| `ota wan join` / `ota wan leave`                | Connects to the configured WiFi network without downloading firmware, or disconnects and turns off the WAN power.                                                                                                                                                             |
| `ota wan check`                                 | Checks whether the device can reach the internet after joining the WiFi network.                                                                                                                                                                                              |
| `get ota.wan.pwr` / `set ota.wan.pwr <on\|off>` | Reads or directly controls the WAN power switch. This is mainly useful for diagnostics and recovery.                                                                                                                                                                          |
| `get ota.slot`                                  | Shows the version and state of both OTA slots. For the inactive slot it reports two separate facts: what the bootloader records (`recorded-valid`, `aborted`, ...) and whether the image actually verifies (`image-ok`, `image-invalid`). Only `image-ok` means a rollback target exists. |
| `ota slot boot <A\|B>`                          | Changes the bootloader configuration to use the other OTA slot and reboots into it. It refuses to switch if the selected slot is already active or doesn't contain a valid image. Rollback testing is also started again for the selected slot.                               |

For example:

```text
set ota.wan.wifi MyHotspot,hunter2
start ota wan https://example.com/firmware/heltec_v4_repeater-v1.16.0.bin
```

The firmware is verified against a SHA-256 the image carries in its own final 32 bytes, so nothing is fetched but the image. You only need to set a checksum manually when the firmware comes from a source you do not trust to serve it honestly.

For complete details about these commands, see `docs/cli-additions.md`. The standard MeshCore CLI commands are documented in the upstream `docs/cli_commands.md`.

### Short Commands for Remote Updates

When updating a device remotely over LoRa, it's useful to keep the commands as short as possible.

You can save the firmware URL once:

```text
set ota.fw.url https://tools.mobmesh.org/flasher/heltec_v4/repeater/firmware.bin
```

That address serves the same file as the copy on GitHub, but it is shorter to type
and it does not redirect on the way. If a device has trouble fetching it, the
GitHub address still works:

```text
set ota.fw.url https://github.com/mobmesh/firmware/raw/refs/heads/main/pages/flasher/heltec_v4/repeater/firmware.bin
```

After that, future updates can use:

```text
start ota wan update
```

This is much shorter than sending the full URL every time.

### OTA Update Timing

The `start ota wan` command queues the update and responds immediately with `OK - OTA queued`.
The OTA service then connects to WiFi, downloads, verifies, and flashes in a dedicated task while
the normal command loop remains available. Use `get ota.status` to inspect its current state or
download byte count. Use `ota cancel` to stop it before verification begins.

The whole process can take up to around two minutes.

After a successful update, the main loop reboots into the new firmware and starts automatic
rollback protection. A failure or cancellation leaves the current firmware running and preserves
the terminal result in `get ota.status` for inspection.

## [Web-Based Flasher](https://tools.mobmesh.org/flasher)

Some boards need a repartitioned flash layout to make room for this mod's expanded feature set. The flasher will let the user know if this is the case.

### Full Flash (Reset)

Use this option for a blank board or a board that has been bricked.

### Update Existing Device

Use this option when available to preserve any settings that have already been configured on the device.

Slot switching is still a device-side operation, separate from flashing. The flasher does not change which slot the device boots from. After a USB flash, slot A is automatically set as the primary slot, leaving slot B available for the first OTA update. To switch slots from the device itself, use:

```text
ota slot boot <A|B>
```

## Automatic Rollback Protection

Automatic rollback protection is added by this mod. It is not part of the standard MeshCore behavior.

Normally, if a bad firmware update boots and `radio_init()` fails, MeshCore calls `halt()`. The device then becomes unresponsive and there is no automatic way to return to the previous firmware.

This mod changes that behavior.

After a firmware update using either `start ota` or `start ota wan`, the new firmware is placed into a probation period. It is not immediately marked as confirmed.

The device needs to run for about 90 seconds with a working radio before the new firmware is considered stable.

If `radio_init()` fails while the new firmware is still on probation, the device assumes the update is bad. It automatically rolls back to the previous firmware and reboots.

This is especially useful for nodes that are installed somewhere remote. If a bad firmware update causes the radio to stop working, you may not be able to reach the device to re-flash it.

With rollback protection enabled, the device can recover on its own instead of remaining stuck on the broken firmware.

Radio failures that are unrelated to a recent update are handled differently. The device will retry the reboot a limited number of times and then halt instead of getting stuck in an endless reboot loop.

No extra hardware is required for rollback protection. It uses the ESP-IDF app rollback feature that is already available in the MeshCore upstream toolchain.

## Automatic Clock Sync

Neither of the boards this mod currently targets has a battery-backed clock chip, so their sense of time resets to a fixed placeholder date every time they reboot (see the `timing-safety` mod for more on why that happens and what else it affects).

This mod fixes that for free, as a side effect of something it's already doing. Every time the device joins WiFi for OTA purposes -- whether from `ota wan join`, `start ota wan`, or `start ota wan update` -- it also asks an NTP server (`us.pool.ntp.org`, falling back to the global `pool.ntp.org` if that doesn't answer) what time it is and sets the device's clock from the answer. No extra command, no admin step, and no separate WiFi connection just for this -- it rides along with a connection the device was already making.

This happens automatically and can't be turned off from the CLI. If the NTP request fails or times out, the clock is simply left as it was; nothing else about the OTA flow is affected either way.

## Why This Exists

Remote MeshCore nodes can be difficult or impossible to access physically.

A normal firmware update can leave a remote node unusable if the new firmware has a problem. If the radio fails during startup, the node may stop responding to commands sent over the mesh.

The hotspot OTA feature makes it possible to download and install firmware remotely using an existing WiFi connection.

The rollback protection adds another layer of safety. If the new firmware doesn't start correctly, the node can automatically return to the last known working firmware.

Together, these features make it much safer to manage MeshCore nodes that are installed in remote or hard-to-reach locations.
