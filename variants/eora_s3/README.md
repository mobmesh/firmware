# Ebyte EoRa-S3

Board-specific notes for firmware built for this board. `overrides.yaml` in this same folder holds the machine-readable config; this file holds the hardware notes a human needs.

Not yet in `build-targets.yaml`: nothing builds or releases for this board until a target is added.

## Board id

- The id is `eora_s3`; `upstream_variant: ebyte_eora_s3` names upstream's directory. The full name does not fit the image stamp's 24-byte `board/role` field.

## Hardware requirements (for the `hotspot-ota` mod)

- `PIN_HOTSPOT_PWR` is GPIO38 (header pin 13 in Ebyte's EoRa-S3-900TB pin table), a plain GPIO; not yet confirmed on hardware.
- If the switch is a load-switch IC, its control line must be held high for the whole update, not pulsed. A hardware pulldown on the control line is recommended so the rail defaults to off on any reset.

## Build notes

- 4 MB flash: the stock scheme leaves 1.25 MB OTA slots, too small for this build. `partitions_eora_s3.csv` is the xiao_c3 table, giving both slots 0x1D0000.
- Upstream's board file is `boards/ebyte_eora-s3.json`, which matches neither directory name; `board.json` here is a copy of it.
