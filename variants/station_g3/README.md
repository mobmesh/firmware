# B&Q Station G3

Board-specific notes for firmware built for this board. `overrides.yaml` in this same folder holds the machine-readable config; this file holds the hardware notes a human needs.

Not yet in `build-targets.yaml`: nothing builds or releases for this board until a target is added.

## Board id

- The id is `station_g3`; `upstream_variant: station_g3_esp32` names upstream's directory. The full name does not fit the image stamp's 24-byte `board/role` field.

## Hardware requirements (for the `hotspot-ota` mod)

- `PIN_HOTSPOT_PWR` is GPIO8 on the IO extension socket, confirmed on hardware: it follows `set ota.wan.pwr` and stays low through boot.
- GPIO9 and GPIO10 are this board's PA level and LNA controls; do not use them.
- If the switch is a load-switch IC, its control line must be held high for the whole update, not pulsed. A hardware pulldown on the control line is recommended so the rail defaults to off on any reset.

## Build notes

- 16 MB flash, but upstream sets no partition table, leaving the stock 1.25 MB OTA slots; this build does not fit them. `partitions_station_g3.csv` is the stock `default_16MB.csv`, giving both slots 6.25 MB. Moving a stock board onto it needs a full erase.
- Upstream's board file is `boards/station-g3-esp32.json`, which matches neither directory name; `board.json` here is a copy of it.
- `LORA_TX_POWER` is 7: the SX1262 drives an external PA. Do not copy heltec_v4's `set tx 20`.

## Serial

- USB serial is TinyUSB (`ARDUINO_USB_MODE=0`): the host must assert DTR to see output, and QEMU cannot reach the CLI.
- esptool cannot auto-reset it into download mode; open the port at 1200 baud and close it, then use `--after watchdog-reset` to return to the app.
