# hotspot-ota - Remote OTA Firmware Updates for MeshCore

Adds a WiFi-based over-the-air update path to MeshCore, along with automatic rollback protection if an update doesn't work correctly.

MeshCore already has a built-in `start ota` command. This starts a WiFi access point on the device and lets someone nearby upload a `.bin` firmware file through a web page.

This mod adds another way to update the device. Instead of creating its own WiFi network, the device powers on the external power rail for its hotspot, if one is connected, and joins an existing WiFi network. It checks that the network has internet access, downloads the firmware from a URL, verifies it, and then installs it.

This means you don't need to bring a laptop or phone to the location of the device just to perform an update.

The commands are also regular MeshCore CLI commands, so they can be sent remotely over the LoRa mesh. You don't have to be connected directly to the device.

The mod also adds rollback protection. After an update, the new firmware is tested before it is considered good. If the new firmware fails during startup, the device automatically goes back to the previous working firmware.

## Patches

| File                                    | Purpose                                            |
| --------------------------------------- | -------------------------------------------------- |
| `patches/0001_hotspot-fetch-ota.patch`  | Adds the hotspot-based OTA update feature          |
| `patches/0002_ota-rollback-guard.patch` | Adds automatic rollback protection after an update |

The second patch depends on the first one. This dependency is defined in `0002.meta.yaml`, so the rollback patch cannot be applied on its own.

The patches don't contain board-specific settings such as the GPIO pin used for the power switch or WiFi and HTTP timing values.

Those settings come from each board's `variants/<board>/overrides.yaml` file and are passed into the build as `-D` flags. See the root README for more information about how board configuration works.

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
| `set ota.fw.marker <on\|off>`                   | Controls the firmware authenticity check. It is `on` by default. Setting it to `off` temporarily disables the check for the next `start ota wan` command. The setting is stored only in RAM and is turned back on after a reboot. The SHA-256 check is still always enforced. |
| `ota wan join` / `ota wan leave`                | Connects to the configured WiFi network without downloading firmware, or disconnects and turns off the WAN power.                                                                                                                                                             |
| `ota wan check`                                 | Checks whether the device can reach the internet after joining the WiFi network.                                                                                                                                                                                              |
| `get ota.wan.pwr` / `set ota.wan.pwr <on\|off>` | Reads or directly controls the WAN power switch. This is mainly useful for diagnostics and recovery.                                                                                                                                                                          |
| `get ota.slot`                                  | Shows the version and state of both OTA slots. The version includes the short build commit hash, which helps tell apart different builds with the same version number. A slot that has never booted will show `v?`.                                                           |
| `ota slot boot <A\|B>`                          | Changes the bootloader configuration to use the other OTA slot and reboots into it. It refuses to switch if the selected slot is already active or doesn't contain a valid image. Rollback testing is also started again for the selected slot.                               |

For example:

```text
set ota.wan.wifi MyHotspot,hunter2
start ota wan https://example.com/firmware/heltec_v4_repeater-v1.16.0.bin
```

If a file named `<url>.sha256` exists next to the firmware file, it is downloaded automatically and used to verify the firmware. You don't need to manually set the checksum in that case.

For complete details about these commands, see `docs/cli-additions.md`. The standard MeshCore CLI commands are documented in the upstream `docs/cli_commands.md`.

### Short Commands for Remote Updates

When updating a device remotely over LoRa, it's useful to keep the commands as short as possible.

You can save the firmware URL once:

```text
set ota.fw.url https://github.com/mobmesh/firmware/raw/refs/heads/main/pages/flasher/heltec_v4/repeater/firmware.bin
```

After that, future updates can use:

```text
start ota wan update
```

This is much shorter than sending the full URL every time.

### OTA Update Timing

The `start ota wan` command does not respond immediately.

Unlike most CLI commands, there is no initial acknowledgment or progress message. The device first connects to WiFi, checks the connection, downloads the firmware, verifies it, and flashes it before sending a response.

The whole process can take up to around two minutes.

After the update finishes, the device reboots using the new firmware and starts the automatic rollback protection process.

## Web-Based Flasher

The shared web-based flasher in this project supports two OTA-slot-aware flashing options for boards using this mod.

### New Device

Use this option for a blank board or a board that has been bricked.

It completely erases the chip and installs the bootloader, partition table, and firmware from scratch.

The firmware is written to both OTA slots. This means you can use `ota slot boot <A|B>` immediately without having to flash the second slot separately.

### Update Existing Device

Use this option when the board is already running MeshCore.

It writes the firmware to either OTA slot A or B without erasing anything else on the device.

This is the only way to select a specific OTA slot from outside the device's own CLI.

The flasher does not change which slot the device will boot from. To switch slots from the device itself, use:

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

## Why This Exists

Remote MeshCore nodes can be difficult or impossible to access physically.

A normal firmware update can leave a remote node unusable if the new firmware has a problem. If the radio fails during startup, the node may stop responding to commands sent over the mesh.

The hotspot OTA feature makes it possible to download and install firmware remotely using an existing WiFi connection.

The rollback protection adds another layer of safety. If the new firmware doesn't start correctly, the node can automatically return to the last known working firmware.

Together, these features make it much safer to manage MeshCore nodes that are installed in remote or hard-to-reach locations.
