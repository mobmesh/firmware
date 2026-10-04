# Xiao ESP32-S3 + Wio-SX1262 kit

Board-specific notes for firmware built for this board. `overrides.yaml` in this same folder holds the machine-readable config; this file holds the hardware notes a human needs.

Not yet in `build-targets.yaml`: nothing builds or releases for this board until a target is added.

## Hardware requirements (for the `hotspot-ota` mod)

- `PIN_HOTSPOT_PWR` is GPIO2 (pad D1). The Wio-SX1262 uses GPIO7-9 and GPIO38-42, and GPIO3 (D2) is a strapping pin; not yet confirmed on hardware.
- Upstream's plain `xiao_s3` variant (radio wired to the D pins) is a different board and is not covered here.
- If the switch is a load-switch IC, its control line must be held high for the whole update, not pulsed. A hardware pulldown on the control line is recommended so the rail defaults to off on any reset.

## Build notes

- Upstream builds this board on PlatformIO's `seeed_xiao_esp32s3` and ships no `boards/xiao_s3_wio.json`; `board.json` here is the vendored fallback.
