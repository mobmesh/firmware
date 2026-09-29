# RAK3112 (WisBlock RAK3312 core)

Board-specific notes for firmware built for this board. `overrides.yaml` in this same folder holds the machine-readable config; this file holds the hardware notes a human needs.

Not yet in `build-targets.yaml`: nothing builds or releases for this board until a target is added.

## Hardware requirements (for the `hotspot-ota` mod)

- `PIN_HOTSPOT_PWR` is GPIO38 (WisBlock `WB_IO5`), from RAK's RAK3312 pin table; not yet confirmed on hardware.
- `WB_IO1` and `WB_IO2` are left alone: slot A modules commonly use them, and upstream drives `WB_IO2` (GPIO14) as the 3V3_S enable.
- If the switch is a load-switch IC, its control line must be held high for the whole update, not pulsed. A hardware pulldown on the control line is recommended so the rail defaults to off on any reset.

## Build notes

- Upstream builds this board on PlatformIO's `esp32-s3-devkitc-1` (8 MB), although the module carries 16 MB of flash; the default 8 MB table leaves each OTA slot 3.3 MB.
- Upstream ships no `boards/rak3112.json`; `board.json` here is the vendored fallback.
