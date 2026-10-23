# Grumpy Node

Seeed XIAO ESP32-C3 with an Ebyte E22P-915M30S radio and a MAX17048 fuel gauge.

## Board files

`vendor/` is copied daily by `upstream-sync` from [Cloud-121/MeshCore](https://github.com/Cloud-121/MeshCore) (MIT, `vendor/LICENSE`); `SOURCE.json` records the commit. Do not edit `vendor/` by hand.

## Hardware notes

- `LORA_TX_POWER=20` drives a 30 dBm amplifier. Do not raise it.
- `PIN_HOTSPOT_PWR` is preliminarily D0 (GPIO2), active low: LOW powers the hotspot, HIGH (and the strapping pull-up at reset) keeps it off. Unconfirmed on the PCB.
- Beta until checked on a real board.
