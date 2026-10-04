# try-settings CLI reference

Commands added by `try-settings`, available over serial and authenticated remote CLI.
Trial behavior and examples are described in the [mod README](../README.md).

`<value>` denotes a required parameter. Durations are integer seconds without a unit suffix.

## Command index

| Command | Purpose |
| --- | --- |
| [`tryset`](#tryset) | Inspect the running trial |
| [`get tryset`](#tryset) | Alias for trial inspection |
| [`tryset <seconds> <key> <value>`](#tryset-setting) | Trial a supported setting |
| [`tryset <seconds> radio <freq>,<bw>,<sf>,<cr>`](#tryset-radio) | Trial radio parameters |
| [`tryset keep`](#tryset-keep) | Retain the active trial's setting |
| [`tryset revert`](#tryset-revert) | Restore the original setting |

## tryset

```text
tryset
get tryset
```

| Response | Meaning |
| --- | --- |
| `(none)` | No live trial |
| `<key> <value> <seconds>s` | Setting under trial and time remaining |
| `radio <freq>,<bw>,<sf>,<cr> (tempradio)` | Live native radio trial; remaining time is not included |

## tryset setting

```text
tryset <seconds> <key> <value>
```

| Parameter | Definition |
| --- | --- |
| `seconds` | Integer duration, 60–86400 seconds |
| `key` | One of the keys below |
| `value` | Same syntax and limits as the native `set <key>` command |

| Valid key | Setting |
| --- | --- |
| `tx` | LoRa transmit power |
| `radio.rxgain` | Radio chip receive gain |
| `radio.fem.rxgain` | External FEM receive gain, where supported |
| `radio.fem.txgain` | External FEM transmit gain, where supported |
| `int.thresh` | Interference threshold |
| `cad` | Channel Activity Detection |
| `agc.reset.interval` | Receiver AGC reset interval |
| `flood.max` | Maximum flood hops |
| `flood.max.unscoped` | Maximum unscoped flood hops |
| `flood.max.advert` | Maximum advert flood hops |
| `repeat` | Message forwarding |
| `dutycycle` | Transmit duty-cycle limit |

**Response:** `OK - tryset <seconds>s (reverts unless kept)` or `Error: ...`.
Native setting-validation errors may also be returned.

**Effect:** Records the original value before applying the trial value. Expiry or
reboot restores it. A failed restore retains the recovery record and retries.
Clock corrections do not change the trial duration.

**Restrictions:** One trial at a time, including radio trials. Unsupported keys or
board capabilities are refused. A rollback record must be saved before the setting changes.

## tryset radio

```text
tryset <seconds> radio <freq>,<bw>,<sf>,<cr>
```

| Parameter | Definition |
| --- | --- |
| `seconds` | 60–86400 seconds; must be a multiple of 60 |
| `freq` | Frequency in MHz |
| `bw` | Bandwidth in kHz |
| `sf` | Spreading factor |
| `cr` | Coding rate |

**Response:** Native `tempradio` response or `Error: ...`.

**Effect:** Uses MeshCore's temporary-radio mechanism. Parameters are validated by
upstream; they are not saved until kept. Expiry or reboot restores saved parameters.

**Restrictions:** Cannot overlap another trial. A fractional-minute duration is refused
with `Error: radio trials take whole minutes -- use <lower> or <upper>`.

## tryset keep

```text
tryset keep
```

| Response | Meaning |
| --- | --- |
| `OK - <key> kept` | Setting trial retained; recovery record cleared |
| Native `set radio` response | Live radio parameters saved |
| `Error: no live trial to keep` | No active trial to confirm |

**Restrictions:** Confirmation must arrive during the active trial period for every key.
After expiry or revert, a new trial is required before keeping a value.

## tryset revert

```text
tryset revert
```

| Response | Meaning |
| --- | --- |
| `OK - <key> reverted` | Original setting restored |
| `OK - radio reverting to saved params` | Native radio return requested |
| `Error: <key> could not be restored -- trial kept, retrying` | Recovery remains pending |
| `Error: no live trial` | Nothing to revert |

**Effect:** Setting trials restore immediately when the native setter succeeds.
Radio trials return to saved parameters through a one-minute native trial; the
radio change normally takes effect within two seconds. The native timer remains
active until that trial expires.
