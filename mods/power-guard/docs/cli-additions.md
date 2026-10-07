# power-guard CLI reference

[↩ Back to power-guard readme](../README.md)

Commands added or extended by `power-guard` on supported ESP32 repeaters, available
over serial and authenticated remote CLI. Battery behavior and board thresholds
are described in the [mod README](../README.md).

`<value>` denotes a required parameter; `[value]` is optional; `on|off` is a choice.
Do not type the brackets. Durations are integer seconds without a unit suffix.

## Command index

| Command | Purpose |
| --- | --- |
| [`powersaving safe.mv <mv>`](#powersaving-safemv) | Save the safe-sleep threshold |
| [`powersaving safe [on\|off]`](#powersaving-safe) | Inspect or enable safe sleep |
| [`powersaving auto [on\|off]`](#powersaving-auto) | Inspect or enable automatic power saving |
| [`poweroff <seconds>`](#poweroff--shutdown) | Sleep for a specified duration |
| [`shutdown <seconds>`](#poweroff--shutdown) | Alias for timed power-off |
| [`stats-core`](#stats-core) | Read battery and system statistics |
| [`stats-radio`](#stats-radio) | Read radio statistics |
| [`stats-packets`](#stats-packets) | Read packet counters |

## powersaving safe.mv

```text
powersaving safe.mv <mv>
```

| Parameter | Definition |
| --- | --- |
| `mv` | Integer gauge millivolts within the board's accepted range, or `0` to disable the safe-sleep rung |

| Response | Meaning |
| --- | --- |
| `OK - safe.mv <mv>mV` | Threshold saved; safe sleep is enabled |
| `OK - safe.mv <mv>mV (safe is off)` | Threshold saved; safe sleep remains off |
| `OK - safe.mv 0 (rung disabled)` | Threshold disabled |
| `ERR: <floor>-<maximum> or 0` | Value is outside the board's allowed range or is malformed |

**Storage:** Persistent across reboots and firmware updates.

**Restrictions:** A nonzero threshold must be at least the board's safe floor and
below its resume threshold. Setting a threshold does not enable safe sleep.

## powersaving safe

```text
powersaving safe
powersaving safe <on|off>
```

| Parameter | Effect |
| --- | --- |
| Omitted | Read state, threshold, and last measured battery value |
| `on` | Enable safe sleep at the saved threshold |
| `off` | Disable safe sleep while retaining the threshold |

| Response | Meaning |
| --- | --- |
| `safe <on\|off>, <threshold>mV, batt <reading>mV` | Current safe-sleep state |
| `OK - safe on, <threshold>mV` | Enabled |
| `OK - safe off (<threshold>mV kept)` | Disabled |
| `ERR: no threshold -- set powersaving safe.mv first` | Cannot enable with a zero threshold |

**Storage:** Enable state and threshold are persistent.

**Scope:** Controls leaving service on low voltage. Does not change the separate
boot-time resume check or automatic power-saving thresholds.

## powersaving auto

```text
powersaving auto
powersaving auto <on|off>
```

| Parameter | Effect |
| --- | --- |
| Omitted | Read enable state, active state, and transition count |
| `on` | Allow the board's voltage ladder to control automatic power saving |
| `off` | Disable automatic control and release any active automatic saving |

**Response:** `auto <on|off>, active <yes|no>, transitions <count>` when reading;
`OK - auto <on|off>` when setting; `ERR: usage: powersaving auto [on|off]` for invalid input.

**Storage:** Runtime-only. Reboot restores the board's configured default.

**Scope:** `on` enables automatic control; `active yes` means voltage has engaged it.
Safe sleep is controlled separately. Voltage thresholds come from board configuration.

## poweroff / shutdown

```text
poweroff <seconds>
shutdown <seconds>
```

| Parameter | Definition |
| --- | --- |
| `seconds` | Sleep duration, 60–86400 seconds with the standard build limits |

**Response:** `OK - deep sleep <seconds>s (<hours>h<minutes>m), then wakes`, or
`ERR: usage: poweroff <secs> (<minimum>-<maximum>)`.

**Effect:** Enters deep sleep, then wakes. Serial invocation sleeps immediately after
printing the reply. Remote invocation waits five seconds so the reply can be transmitted.

**Restrictions:** A duration is required. When hotspot-ota is installed, active OTA
work or rollback probation can refuse sleep. On wake, the board's resume-voltage check
may return the node to sleep until the battery recovers.

## stats-core

```text
stats-core
```

**Response:** Native MeshCore system statistics: battery, uptime, errors, and queue state.

**Extension:** Allows the existing serial command over authenticated remote CLI.
Read-only; response fields follow the upstream firmware version.

## stats-radio

```text
stats-radio
```

**Response:** Native MeshCore radio statistics: noise floor, RSSI/SNR, and airtime.

**Extension:** Allows the existing serial command over authenticated remote CLI.
Read-only; response fields follow the upstream firmware version.

## stats-packets

```text
stats-packets
```

**Response:** Native MeshCore packet counters.

**Extension:** Allows the existing serial command over authenticated remote CLI.
Read-only; response fields follow the upstream firmware version.

[↩ Back to power-guard readme](../README.md)
