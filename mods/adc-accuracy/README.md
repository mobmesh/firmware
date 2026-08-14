# adc-accuracy - Calibrated Battery Voltage Reading on Heltec V4

`HeltecV4Board::getBattMilliVolts()` reads the battery ADC at 10-bit resolution
with `analogRead()` and a hardcoded `3.3V / 1024` conversion. This throws away
accuracy two independent ways: 10-bit instead of the ESP32-S3's available
12-bit, and no correction for the per-chip ADC nonlinearity that
`analogReadMilliVolts()` already accounts for internally. Measured effect:
readings quantise in steps of roughly 17 mV.

This mod switches the same function to `analogReadResolution(12)` and
`analogReadMilliVolts()` -- the same approach upstream's own base-class
`ESP32Board::getBattMilliVolts()` already uses for every other ESP32 board.
`HeltecV4Board` just never adopted it, since its override exists only to apply
the board's own resistor-divider multiplier on top.

## Scope

One function, one board. No CLI surface, no runtime flag, nothing to hook --
this patches `variants/heltec_v4/HeltecV4Board.cpp` directly and unconditionally.
Doesn't touch xiao_c3 or any other board.

## Relationship to `ADC_MULTIPLIER`

Separate from, and independent of, `variants/heltec_v4/overrides.yaml`'s
`ADC_MULTIPLIER: 4.9` build flag. That corrects a wrong constant (upstream's
default doesn't match the schematic's actual divider); this corrects the read
path itself. Both were needed; neither depends on the other.

## Enabling It

Add `adc-accuracy` to a target's `mods:` list in `build-targets.yaml`:

```yaml
    mods: [hotspot-ota, timing-safety, adc-accuracy]
```

**Contributes no suffix.** This is a correctness fix, not a feature -- enabling
it does not change the released asset's filename, unlike every other mod here.

## Why Not Fold Into batt-saver

Battery voltage is read and reported independent of `batt-saver` -- the plain
`battery` CLI command and telemetry channel 1 both use it, on any target,
whether or not powersaving-by-battery is enabled. Tying the accuracy fix to
`batt-saver`'s lifecycle would withhold it from every target that doesn't
adopt that mod.
