# Xiao ESP32-C3

Board-specific notes for firmware built for this board. `overrides.yaml` in this same folder holds the machine-readable config (GPIO pins, timing, partition scheme); this file holds the hardware notes a human needs that don't belong in code or generated output.

## Hardware requirements (for the `hotspot-ota` mod)

- `PIN_HOTSPOT_PWR` in `overrides.yaml` is currently a **placeholder** (carried over from heltec_v4's GPIO47), not yet confirmed against this board's real pinout. Do not treat this as validated wiring -- confirm the actual hotspot power-switch GPIO on real Xiao C3 hardware before relying on this in production, and update `overrides.yaml` once known.
- Custom partition table (`partitions_xiao_c3.csv`) is required -- the stock 4MB scheme's default app partition is too small to fit `hotspot-ota`'s added code.

These requirements only apply if this board is built with the `hotspot-ota` mod -- see `build-targets.yaml` at the repo root for which mods each board's build includes.

## No automated boot-loop check -- already attempted, not just unscoped

`heltec_v4` gets an automated QEMU boot-loop regression check (`qemu-boot-check.yml`) on every release, using `qemu-system-xtensa`. RISC-V (ESP32-C3, this board's chip) needs a different QEMU machine target entirely, and this was already tried: a RISC-V-capable QEMU build was put together and couldn't even boot a known-good upstream MeshCore binary. This isn't a "someone should scope this" backlog item -- the available tooling was tried and found not to work as a testing platform. Manual hardware testing is the only verification path for this board until/unless the RISC-V QEMU tooling situation actually changes.
