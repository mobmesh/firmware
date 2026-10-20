# hotspot-ota CLI reference

[↩ Back to hotspot-ota readme](../README.md)

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
| [`set ota.wan.auto <on\|off>`](#get--set-otawanauto) | Turn timed automatic updates on or off |
| [`set ota.wan.auto.interval <N>d`](#get--set-otawanautointerval) | Set days between automatic checks |
| [`set ota.wan.auto.confirm <N>`](#get--set-otawanautoconfirm) | Set checks in a row before installing |
| [`get ota.fw.url`](#get--set-otafwurl) | Read the saved URL |
| [`get ota.status`](#get-otastatus) | Read update or WAN-verification progress |
| [`get ota.ap`](#get-otaap) | Read local upload AP state |
| [`get ota.wan.health`](#get-otawanhealth) | Read verification result and proven status |
| [`get ota.wan.radio`](#get-otawanradio) | Read WiFi, driver, link, and power state |
| [`get ota.wan.pwr`](#get--set-otawanpwr) | Read hotspot-power control state |
| [`get ota.slot`](#get-otaslot) | Inspect both firmware slots and probation |
| [`get ota.wan.auto.status`](#get-otawanautostatus) | Read the automatic update schedule |
| [`ota cancel`](#ota-cancel) | Cancel an update before verification |
| [`ota slot confirm`](#ota-slot-confirm) | Accept firmware on probation now |
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

## get / set ota.wan.auto

```text
get ota.wan.auto
set ota.wan.auto <on|off>
```

| Item | Definition |
| --- | --- |
| `on` / `off` | Run `start ota wan update` on a schedule, or stop. Default `off`. |
| Get response | `> on` or `> off` |
| Set response | `on`: the schedule (below); `off`: `OK`; or `ERR: expected on\|off`, `ERR: WAN not proven; run ota wan verify first` |
| Storage | Persistent across reboots and firmware updates |
| Restrictions | `on` requires `get ota.wan.health` to show `proven=yes` |

Turning it on replies with the schedule:

| Clock | Reply |
| --- | --- |
| Set | `OK - checks every 7d at 14:05 UTC, next 10-27; installs on the 2nd check that finds a new release` |
| Unset | `OK - checks every 7d once the clock is set; installs on the 2nd check that finds a new release` |

Changing the interval or confirm count while on replies the same way. The schedule starts from that moment: the first check runs one interval
later, then every interval after. With the clock unset, the schedule starts once the
clock is set, plus a per-node offset of up to 24 hours. Turning it off and on again
starts a new schedule.

Each check uses the normal WAN update and its checks. It also:

- installs a new image only after `ota.wan.auto.confirm` checks in a row have found it;
- skips a release this node rolled back from;
- skips while the clock is older than the firmware build;
- skips while any sync-settings campaign holds a lock;
- skips while an update, survey, upload AP or rollback probation is active.

A skipped or failed check waits for the next interval. An unchanged image costs one short
WiFi session; a check that only counts a new image reads about 1 KB.

## get / set ota.wan.auto.interval

```text
get ota.wan.auto.interval
set ota.wan.auto.interval <N>d
```

| Item | Definition |
| --- | --- |
| `N` | Days between checks, 4–90; the `d` is required. Default `7d`. |
| Get response | `> <N>d` |
| Set response | `OK` (the schedule when auto is on) or `ERR: expected <4-90>d` |
| Storage | Persistent |

Changing the interval keeps the schedule's start and changes only the spacing.

## get / set ota.wan.auto.confirm

```text
get ota.wan.auto.confirm
set ota.wan.auto.confirm <N>
```

| Item | Definition |
| --- | --- |
| `N` | Checks in a row that must find the same new image before it installs, 2–10. Default `2`. |
| Get response | `> <N>` |
| Set response | `OK` (the schedule when auto is on) or `ERR: expected 2-10` |
| Storage | Persistent; the running count also survives reboots |

A different image restarts the count, including a republish of the same version.

## get ota.wan.auto.status

```text
get ota.wan.auto.status
```

| Response | Meaning |
| --- | --- |
| `> off` | Automatic updates are off |
| `> on; waiting for clock` | On; the schedule starts once the clock is set |
| `> on; checking` | A check is running now; `get ota.status` shows its progress |
| `> on; next <MM-DD HH:MM> UTC` | Scheduled; no check has run yet |
| `> on; next <MM-DD HH:MM> UTC; last <MM-DD> <result>` | Scheduled; the last check's result |
| `> on; next <MM-DD HH:MM> UTC; <version> (<sha>) <n> of <confirm>` | A new image has been counted |

| Result | Meaning |
| --- | --- |
| `current` | The server holds the running image |
| `counted` | A new image was seen and counted |
| `installed` | A new image was installed |
| `failed` | The check could not complete; `get ota.status` shows why |
| `skipped lock` | A sync-settings campaign held a lock |
| `skipped busy` | Another update, survey, upload AP or probation was active |
| `skipped rolled-back` | The server holds a release this node rolled back from |

## get ota.status

```text
get ota.status
```

| Response format | Meaning |
| --- | --- |
| `> <state>` | Current operation state |
| `> downloading <bytes>/<total>` | Download progress with known content length |
| `> downloading <bytes> bytes` | Download progress without known content length |
| `> succeeded: <result>` | Image installed; the node reboots |
| `> done: <result>` | Nothing installed, by design: the image is already running, was counted by `ota.wan.auto`, or rolled back on this node |
| `> failed: <result>` / `> canceled: <result>` | The update did not complete |

Done, failure and cancellation results remain until another operation starts.
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
> Slots: A=<version> (active, <state>) | B=<version> (standby, <state>, <image>)
```

The active and standby descriptions swap when B is running.
A is `ota_0`; B is `ota_1`. Versions include the build SHA, recorded after each
slot boots; a slot without a recorded version shows `v?`.

Example, B running new firmware that is waiting to hear the mesh:

```text
> Slots: A=v1.18.0-35b63b8 (standby, valid, image-ok) | B=v1.18.0-ffa154e (active, pending: mesh 2/3 34m)
```

| Active state | Meaning |
| --- | --- |
| `valid` | Confirmed |
| `pending: boot <seconds>s` | New firmware in its first 90 seconds |
| `pending: mesh <sent>/3 <minutes>m` | Waiting to hear the mesh; `<sent>` adverts sent so far, `<minutes>` left |
| `n/a` | Rollback state unavailable, or flashed over USB |

| Standby state | Meaning |
| --- | --- |
| `valid` | Previously confirmed, and the image still verifies |
| `dirty` | Was confirmed, but has since been partly overwritten, for example by a failed download |
| `new` | Selected but not yet booted |
| `invalid` | Rejected by rollback |
| `aborted` | Did not confirm before a reset |
| `n/a` | No applicable rollback record |

| Image result | Meaning |
| --- | --- |
| `image-ok` | Complete image passed integrity verification |
| `image-invalid` | Image failed verification |
| `image-absent` | Partition does not exist |
| `image-unchecked` | Flash writer is active; verification was skipped |

Only the standby image is verified; reading and hashing it takes longer than other
status queries. Requires rollback-guard support.

### Probation

New firmware must survive 90 seconds. If the node heard the mesh in the 3 days before an
update installed over WiFi or the upload page, it must also hear a valid packet within the
next hour, or it returns to the previous firmware. When nothing is heard on its own, it
sends a flood advert, up to 3, 20 minutes apart; a neighbour relaying it counts. The
listening step is skipped when the battery is below the power-guard resume level, when the
image was already confirmed on this node, and after a USB flash. Radio initialization
failure rolls back at once.

Any reset during probation returns to the previous firmware, so `reboot`, `poweroff` and
`shutdown` are refused until it ends; `ota slot confirm` ends it early.

## ota slot confirm

```text
ota slot confirm
```

**Response:** `OK - firmware confirmed` or `ERR: no firmware on probation`.

**Effect:** Ends probation and keeps the running firmware, skipping any remaining checks.

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

**Join failures:** the same replies appear in OTA and WAN-verify results.

| Reply | Meaning |
| --- | --- |
| `ERR: WiFi not found` | The saved network is not visible; the hotspot may be off or out of range |
| `ERR: WiFi bad password` | The network rejected the saved password, or the handshake never completed |
| `ERR: WiFi refused` | The network refused the association |
| `ERR: WiFi timeout` | No connection and no reason from the network within the join window |
| `ERR: WiFi start failed` | The WiFi driver did not start; retry, or reboot if it repeats |

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

**Effect:** Selects the requested slot and reboots immediately. Rearms 90 seconds of
rollback probation, including for a previously valid image; it never waits to hear the mesh.

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

[↩ Back to hotspot-ota readme](../README.md)
