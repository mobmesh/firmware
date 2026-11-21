# rx-duty-cycle CLI reference

[↩ Back to rx-duty-cycle readme](../README.md)

Commands added by `rx-duty-cycle`, available over serial and authenticated remote CLI.

`<value>` denotes a required parameter. `[value]` is optional.

## Command index

| Command | Purpose |
| --- | --- |
| [`get radio.rxdc`](#get-radiorxdc) | Read the duty-cycle mode and what the radio is doing now |
| [`set radio.rxdc <auto\|on\|off>`](#set-radiorxdc) | Choose when the radio duty-cycles |
| [`get radio.fem.rxdc`](#get-radiofemrxdc) | Read whether the LNA is bypassed while the radio sleeps |
| [`set radio.fem.rxdc <on\|off>`](#set-radiofemrxdc) | Bypass the LNA while the radio sleeps, during CPU light sleep |

## get radio.rxdc

```text
get radio.rxdc
```

| Response | Meaning |
| --- | --- |
| `> <mode>, armed, rx <us>us sleep <us>us, arms <n>, fails <n>, calib <s>s ago` | Duty-cycling; `calibrating` or `paused for a reading` in place of `armed` while the noise floor is measured |
| `> auto, idle (power saving off)` | Mode is auto; power saving is not running |
| `> <mode>, continuous (no safe timing at these radio settings)` | The current SF and bandwidth leave no safe listen and sleep pair |
| `> <mode>, continuous (arming failed), err <code>` | Three arms in a row failed; continuous until the mode or radio settings change |
| `> off` | Mode is off |

## set radio.rxdc

```text
set radio.rxdc auto
set radio.rxdc on
set radio.rxdc off
```

| Parameter | Effect |
| --- | --- |
| `auto` | Duty-cycle the radio whenever power saving runs (default) |
| `on` | Duty-cycle the radio whether or not power saving runs |
| `off` | Keep continuous receive, with power saving on or off |

| Response | Meaning |
| --- | --- |
| `OK - radio.rxdc <mode>` | Mode set and saved |
| `ERR: radio.rxdc <mode>, not saved` | Mode set until reboot; the settings file could not be written |
| `ERR: usage: set radio.rxdc auto\|on\|off` | Unknown mode |

Only the first word of the value is read, so the full `get radio.rxdc` line can be given back as a value (as `tryset` does when it restores).

## get radio.fem.rxdc

```text
get radio.fem.rxdc
```

| Response | Meaning |
| --- | --- |
| `> on, <n> gated sleeps` | Enabled and acting; `<n>` CPU light sleeps have gated the LNA since boot |
| `> on, inactive (<reason>), <n> gated sleeps` | Enabled but not acting now: `LNA off`, `rxdc not armed`, `no switchable LNA` or `RTC memory in use` |
| `> off` | Disabled |
| `ERR: radio.fem.rxdc not supported on this board` | The board has no KCT8103L FEM on an ESP32-S3 |

## set radio.fem.rxdc

```text
set radio.fem.rxdc on
set radio.fem.rxdc off
```

| Parameter | Effect |
| --- | --- |
| `on` | While the CPU is in light sleep and the radio is duty-cycling with the LNA on, bypass the LNA whenever the radio sleeps (default; KCT8103L FEM boards) |
| `off` | Leave the LNA as set through every radio sleep |

| Response | Meaning |
| --- | --- |
| `OK - radio.fem.rxdc <on\|off>` | Set and saved |
| `ERR: radio.fem.rxdc <on\|off>, not saved` | Set until reboot; the settings file could not be written |
| `ERR: usage: set radio.fem.rxdc on\|off` | Unknown value |

**Storage:** both settings persist across reboots and firmware updates.

[↩ Back to rx-duty-cycle readme](../README.md)
