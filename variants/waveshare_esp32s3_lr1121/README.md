# Waveshare ESP32-S3-LR1121

Board-specific notes for firmware built for this board. `overrides.yaml` in this same folder holds the machine-readable config; this file holds the hardware notes a human needs.

The HF version: ESP32-S3FH4R2 (4 MB flash, 2 MB PSRAM, native USB) with an LR1121 covering 850-930 MHz and 2.4 GHz. Upstream added it in v1.18.0.

## Hardware requirements (for the `hotspot-ota` mod)

- `PIN_HOTSPOT_PWR` is GPIO1. The LR1121 uses GPIO38-42, 45 and 46; GPIO0, 3, 45 and 46 are strapping pins; GPIO19 and 20 are USB. Not yet confirmed on hardware.
- No battery sense, so `power-guard` is not built for this board.
- If the switch is a load-switch IC, its control line must be held high for the whole update, not pulsed. A hardware pulldown on the control line is recommended so the rail defaults to off on any reset.

## Build notes

- Upstream builds on `boards/esp32-s3-zero.json` with `min_spiffs.csv`: two 1.875 MB app slots and a 128 KB filesystem, the same layout as xiao_c3.
- The QEMU boot check needs the LR1121 model in espressif-qemu v9.2.2-6 or newer.
