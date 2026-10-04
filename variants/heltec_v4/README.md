# Heltec V4

Board-specific notes for firmware built for this board. `overrides.yaml` in this same folder holds the machine-readable config (GPIO pins, timing, partition scheme); this file holds the hardware notes a human needs that don't belong in code or generated output.

## Hardware requirements (for the `hotspot-ota` mod)

- Heltec V4 hardware (GPIO47 is confirmed clean for this on the V4.3.1 revision; GPIO48 is a documented fallback if 47 is unavailable), with an external switch controlling power to a WiFi hotspot device.
- If the switch is a load-switch IC (e.g. TI's TPS22995, rated up to 3.8A continuous), note it is not a latch — its `ON` control line must be held continuously high for the duration of an update, not pulsed. A hardware pulldown on the control line is recommended so the rail defaults to off on any reset, independent of firmware state.

These requirements only apply if this board is built with the `hotspot-ota` mod -- see `build-targets.yaml` at the repo root for which mods each board's build includes.
