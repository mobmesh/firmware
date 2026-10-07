# hotspot-ota CLI reference

Commands added or extended by `hotspot-ota`, available over serial and authenticated
remote CLI on supported ESP32 repeater and room-server builds. Setup instructions
are in the [mod README](../README.md).

`<value>` denotes a required parameter; `A|B` denotes a choice. Do not type the brackets.

## Command index

| Command | Purpose |
| --- | --- |
| [`set ota.wan.wifi <ssid>,<password>`](#set-otawanwifi) | Save WiFi credentials |
| [`set ota.fw.url <url>`](#get--set-otafwurl) | Save the default firmware URL |
| [`set ota.fw.marker <on\|off>`](#set-otafwmarker) | Control the next update's stamp check |
| [`set ota.wan.pwr <on\|off>`](#get--set-otawanpwr) | Switch hotspot power |
| [`get ota.fw.url`](#get--set-otafwurl) | Read the saved URL |
| [`get ota.status`](#get-otastatus) | Read update or WAN-verification progress |
| [`get ota.ap`](#get-otaap) | Read local upload AP state |
| [`get ota.wan.health`](#get-otawanhealth) | Read verification result and proven status |
| [`get ota.wan.radio`](#get-otawanradio) | Read WiFi, driver, link, and power state |
| [`get ota.wan.pwr`](#get--set-otawanpwr) | Read hotspot-power control state |
| [`get ota.slot`](#get-otaslot) | Inspect both firmware slots |
| [`ota cancel`](#ota-cancel) | Cancel an update before verification |
| [`ota wan join`](#ota-wan-join) | Join the saved WiFi network |
| [`ota wan leave`](#ota-wan-leave) | Disconnect WiFi and turn off hotspot power |
| [`ota wan check`](#ota-wan-check) | Check internet access after a manual join |
| [`ota wan survey [offset\|refresh]`](#ota-wan-survey) | Scan or page nearby networks |
| [`ota wan verify`](#ota-wan-verify) | Verify WAN/NTP and restore the starting state |
| [`ota slot boot <A\|B>`](#ota-slot-boot) | Select a valid slot and reboot |
| [`start ota`](#start-ota) | Open the local upload AP |
| [`start ota wan <url>`](#start-ota-wan) | Download and install an application image |
| [`start ota wan update`](#start-ota-wan) | Update from the saved URL |
| [`stop ota`](#stop-ota) | Close the local upload AP |
| [`ver`](#ver) | Read upstream and OTA build versions |

## set ota.wan.wifi

```text
set ota.wan.wifi <ssid>,<password>
```

| Item | Definition |
| --- | --- |
| `ssid` | Network name; stored limit of 31 bytes |
| `password` | Network password; stored limit of 63 bytes |
| Response | `OK` or `ERR: ...` |
| Storage | Persistent across firmware updates |
| Restrictions | Refused during an active OTA/WAN operation |

Longer credentials are truncated. Saving credentials clears proven WAN health.
There is no credential-reading command.

## get / set ota.fw.url

```text
get ota.fw.url
set ota.fw.url <url>
```

| Item | Definition |
| --- | --- |
| `url` | HTTP(S) application `.bin` URL; up to 146 characters |
| Get response | `> <url>` or `> (not set)` |
| Set response | `OK` or `ERR: URL too long (max 146 chars)` |
| Storage | Persistent across firmware updates |

A longer URL is refused without truncation. Use an application image, not a merged
image containing boot files.

## set ota.fw.marker

```text
set ota.fw.marker <on|off>
```

| Value | Effect |
| --- | --- |
| `on` | Require the project stamp and OTA support; skip an already-running build. Default. |
| `off` | Permit an unstamped image, an image without OTA support, or a same-build reflash |

**Response:** `OK` or `ERR: expected on|off`.

**Lifetime:** RAM-only. Consumed when the next update is queued. A refusal before
queuing leaves it available; reboot restores `on`.

**Checks retained:** SHA-256 verification always applies. Board and role matching
still applies when both images have readable metadata.

> [!IMPORTANT]
> Firmware without remote OTA removes remote-update ability. Restoring it requires
> an on-site flash.

## get / set ota.wan.pwr

```text
get ota.wan.pwr
set ota.wan.pwr <on|off>
```

| Item | Definition |
| --- | --- |
| `on` / `off` | Assert or release the board-configured hotspot-power control pin |
| Get response | `> on` or `> off` |
| Set response | `OK` or `ERR: ...` |
| Restrictions | Set is refused during an active OTA/WAN operation |

Reports the control-pin state, not measured supply voltage. Does not join WiFi or
start an update. Manual `on` bypasses the automatic power-guard battery check.
External rail control is validated on Heltec V4; check board configuration before
using another board's pin.

## get ota.status

```text
get ota.status
```

| Response format | Meaning |
| --- | --- |
| `> <state>` | Current operation state |
| `> downloading <bytes>/<total>` | Download progress with known content length |
| `> downloading <bytes> bytes` | Download progress without known content length |
| `> <state>: <result>` | Final result or error |

Failure and cancellation results remain until another operation starts.
`wan-complete` means WAN verification finished; read `get ota.wan.health` for its outcome.

## get ota.ap

```text
get ota.ap
```

| Response | Meaning |
| --- | --- |
| `> down` | Local upload AP is stopped |
| `> up, <seconds>s left` | AP is running with the indicated session time remaining |
| `> up, <seconds>s left, uploading` | An upload is writing |

## get ota.wan.health

```text
get ota.wan.health
```

**Response:** `> checking` during verification; otherwise
`> latest=<result> proven=<yes|no>`.

| Result | Meaning |
| --- | --- |
| `UNKNOWN` | No result for the saved credentials |
| `WAN_0\|NTP_0` | WAN verification failed |
| `WAN_1\|NTP_0` | WAN passed; NTP failed |
| `WAN_1\|NTP_1` | WAN and NTP passed |
| `RESTORE_FAULT` | Cleanup could not restore the starting state |

`proven=yes` persists across transient failures. Saving WiFi credentials clears it.
A proven setup is required by features such as sync.time campaigns that request
WAN/NTP refresh. A restore fault does not create a persistent lock.

## get ota.wan.radio

```text
get ota.wan.radio
```

**Response format:** `> wifi:<state> driver:<state> link:<state> pwr:<state>`.

| Field | Values | Meaning |
| --- | --- | --- |
| `wifi` | `off`, `on` | WiFi mode |
| `driver` | `no`, `yes`, `error` | Driver initialization state; `error` indicates an unexpected query result |
| `link` | `down`, `up` | Station connection state |
| `pwr` | `off`, `on` | Same control-pin state as `get ota.wan.pwr` |

Driver initialization alone does not indicate a network connection.

## get ota.slot

```text
get ota.slot
```

**Response format:**

```text
> Slots: A=<version> (active, <state>) | B=<version> (recorded-<state>, <image>)
```

The active and inactive descriptions swap when B is running.
A is `ota_0`; B is `ota_1`. Versions include the build SHA, recorded after each
slot boots; a slot without a recorded version shows `v?`.

| Active state | Meaning |
| --- | --- |
| `pending` | On rollback probation; confirms after approximately 90 seconds with a working radio |
| `valid` | Confirmed |
| `n/a` | Rollback state unavailable |

| Inactive recorded state | Meaning |
| --- | --- |
| `recorded-valid` | Previously accepted |
| `new` | Selected but not yet booted |
| `invalid` | Rejected by rollback |
| `aborted` | Update did not finish |
| `n/a` | No applicable rollback record |

| Image result | Meaning |
| --- | --- |
| `image-ok` | Complete image passed integrity verification |
| `image-invalid` | Image failed verification |
| `image-absent` | Partition does not exist |
| `image-unchecked` | Flash writer is active; verification was skipped |

Recorded validity does not prove the image is intact. Only the inactive image is
verified; reading and hashing it takes longer than other status queries.
A reset during probation causes rollback; radio initialization failure triggers
immediate rollback. Requires rollback-guard support.

## ota cancel

```text
ota cancel
```

**Response:** `OK - cancel requested` or `ERR: OTA is not cancellable`.

**Restrictions:** Accepted while queued, joining, checking WAN, opening the URL,
or downloading. Refused during verification, commit, or WAN verification.

## ota wan join

```text
ota wan join
```

**Response:** `OK - joined` or `ERR: ...`.

**Effect:** Powers the hotspot and joins the saved WiFi network using one join attempt.
Does not check WAN access or download firmware. A failed join shuts WiFi and power down.

**Restrictions:** Requires saved credentials. Refused during an OTA/WAN operation,
while the local upload AP is up, or when power-guard reports a low battery.

## ota wan leave

```text
ota wan leave
```

**Response:** `OK - disconnected`, `ERR: OTA active`, or
`ERR: WiFi shutdown failed; reboot to clear`.

**Effect:** Disconnects the station, stops the WiFi driver, and turns off hotspot power.
On boards with a WPA2-session limit, reaching the limit causes a reboot after
WiFi is off and the reply has been sent.

## ota wan check

```text
ota wan check
```

**Response:** `WAN OK` or `WAN ERR`; `ERR: OTA active` during an OTA/WAN operation.

**Prerequisite:** Manually joined WiFi. Checks internet reachability without joining
or changing hotspot power.

## ota wan survey

```text
ota wan survey
ota wan survey <offset>
ota wan survey refresh
```

| Parameter | Meaning |
| --- | --- |
| `offset` | Result index, 0–255; use the suggested `next N` |
| `refresh` | Discard saved results and start a new scan |

| Response | Meaning |
| --- | --- |
| `OK - scanning` | Scan is running; another call reads its result |
| Network list, optionally ending in `next N` | Saved scan results; each page starts with a newline, and a lock marks protected networks |
| `No networks found` | Scan completed without results |
| `ERR: ...` | Invalid parameter, scan failure, or operation conflict |

**Results:** Matching SSID names are combined within each open/secured category; the strongest signal is shown. Ties retain the first scan record. Row numbers retain scan indexes and can have gaps; use `next N` for paging.

**Lifetime:** One scan retained for two minutes. An expired result starts a new scan.

**Restrictions:** Refused while WiFi is in use or an OTA/WAN operation is active.
Never joins or powers the hotspot. Sleep is inhibited during the scan and released
when it finishes or fails; cached results do not keep the node awake. WiFi is switched
off when scanning finishes.

## ota wan verify

```text
ota wan verify
```

**Response:** `OK - WAN verification queued` or `ERR: ...`.

**Effect:** Powers and joins as needed, checks WAN twice, and attempts NTP twice.
Restores WiFi and hotspot power to their starting state. A reboot cancels verification.

**Result:** `get ota.status` reports `wan-complete`; `get ota.wan.health` reports
success, failure, and the persistent proven flag.

**Restrictions:** Requires saved credentials. Refused while another operation or
local upload AP is active, or when power-guard reports a low battery.

## ota slot boot

```text
ota slot boot <A|B>
```

**Response:** `OK - rebooting` or `ERR: ...`.

**Effect:** Selects the requested slot and reboots immediately. Rearms approximately
90 seconds of rollback probation, including for a previously valid image.

**Restrictions:** Refused if the slot is already active, absent, or has no valid image,
or if an OTA/WAN operation or upload is writing. Requires rollback-guard support.

## start ota

```text
start ota
```

**Effect:** Opens the local access point and its unauthenticated firmware-upload page.
A successful upload reboots into the new image.

**Limits:** Session ends after 20 minutes unless writing. An upload stalled for
10 minutes is aborted and the session closes. Failed uploads leave the page available
for retry until the session deadline. Teardown restores the prior WiFi mode.

**Restrictions:** Refused while the station is connected, another OTA operation is
active, or the firmware is on probation or its rollback state is unavailable.

## start ota wan

```text
start ota wan <url>
start ota wan update
```

| Parameter | Meaning |
| --- | --- |
| `url` | HTTP(S) application `.bin` URL; up to 146 characters |
| `update` | Use the URL saved with `set ota.fw.url` |

**Response:** `OK - OTA queued` or `ERR: ...`.

**Effect:** Joins WiFi if needed, downloads and verifies the image, installs it in the
inactive slot, and reboots on success. Failure leaves the current firmware running.
Progress is available through `get ota.status`.

| Image check | Behavior |
| --- | --- |
| Project stamp | Scans file bytes 208–287 for `MOBMESH` plus its NUL and the preceding compact record |
| OTA support | Requires the hotspot-ota mod bit |
| Board and role | Compares numeric IDs when both stamps are readable |
| Same build | Matching upstream version and repository SHA stops the update |
| Integrity | Always verifies the appended SHA-256 |

`set ota.fw.marker off` changes the stamp checks as documented above. The stamp and
SHA-256 do not authenticate the publisher.

**Restrictions:** Requires saved WiFi credentials and a URL. Refused while the local
upload AP or another operation is active, during probation or unavailable rollback
state, or when power-guard reports a low battery.

## stop ota

```text
stop ota
```

**Response:** `OK - OTA AP stopped` or `ERR: ...`.

**Effect:** Closes the local upload AP and server, restoring the prior WiFi mode.

**Restrictions:** Refused if no AP is running or an upload is writing.

## ver

```text
ver
```

**Response format:**

```text
<upstream-version> (<upstream-build-date>) + ota (<OTA-build-date> - <commit>)
```
