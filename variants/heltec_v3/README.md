# Heltec WiFi LoRa 32 V3

Board-specific notes for firmware built for this board. `overrides.yaml` in this same folder holds the machine-readable config; this file holds the hardware notes a human needs.

Not yet in `build-targets.yaml`: nothing builds or releases for this board until a target is added.

## Hardware requirements (for the `hotspot-ota` mod)

- `PIN_HOTSPOT_PWR` is GPIO7 (header J3, pin 18), chosen from Heltec's V3.2 pin map; not yet confirmed on hardware.
- GPIO47, GPIO48 and GPIO26 are avoided: upstream assigns them to an optional GPS (`PIN_GPS_RX`, `PIN_GPS_TX`, `PIN_GPS_EN`), which the V4 does not.
- If the switch is a load-switch IC, its control line must be held high for the whole update, not pulsed. A hardware pulldown on the control line is recommended so the rail defaults to off on any reset.

## Build notes

- The CLI is on UART0 behind the CP2102 USB bridge, not the ESP32-S3's own USB, so `qemu.console` is `uart0`.
- Upstream builds this board on PlatformIO's `esp32-s3-devkitc-1` and ships no `boards/heltec_v3.json`; `board.json` here is the vendored fallback.
