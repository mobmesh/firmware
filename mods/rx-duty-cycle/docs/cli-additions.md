# rx-duty-cycle CLI reference

[↩ Back to rx-duty-cycle readme](../README.md)

Commands added by `rx-duty-cycle`, available over serial and authenticated remote CLI.

`<value>` denotes a required parameter. `[value]` is optional.

## Command index

| Command | Purpose |
| --- | --- |
| [`powersaving rxdc [auto\|off]`](#powersaving-rxdc) | Show or set whether the radio duty-cycles while power saving runs |

## powersaving rxdc

```text
powersaving rxdc
powersaving rxdc auto
powersaving rxdc off
```

| Parameter | Effect |
| --- | --- |
| Omitted | Read the mode and what the radio is doing now |
| `auto` | Duty-cycle the radio whenever power saving runs (default) |
| `off` | Keep continuous receive, with power saving on or off |

| Response | Meaning |
| --- | --- |
| `rxdc auto, armed, rx <us>us sleep <us>us, arms <n>, fails <n>, calib <s>s ago` | Duty-cycling; `calibrating` or `paused for a reading` in place of `armed` while the noise floor is measured |
| `rxdc auto, idle (power saving off)` | Mode is auto; power saving is not running |
| `rxdc auto, continuous (no safe timing at these radio settings)` | The current SF and bandwidth leave no safe listen and sleep pair |
| `rxdc auto, continuous (arming failed), err <code>` | Three arms in a row failed; continuous until the mode or radio settings change |
| `rxdc off` | Mode is off |
| `OK - rxdc auto` / `OK - rxdc off` | Mode set and saved |
| `ERR: rxdc <mode>, not saved` | Mode set until reboot; the settings file could not be written |
| `ERR: usage: powersaving rxdc [auto\|off]` | Unknown argument |

**Storage:** Persistent across reboots and firmware updates.

[↩ Back to rx-duty-cycle readme](../README.md)
