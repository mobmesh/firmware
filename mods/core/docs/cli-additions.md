# core CLI reference

[↩ Back to core readme](../README.md)

Commands answered by `core` on every MobMesh build, over serial and authenticated remote CLI. Core answers after every other mod, so a mod that handles one of these first takes precedence.

## Command index

| Command | Purpose |
| --- | --- |
| [`ver`](#ver) | Upstream version and build, plus the MobMesh build |
| [`poweroff`](#poweroff--shutdown) | Refused unless a mod provides a power-off that wakes |
| [`shutdown`](#poweroff--shutdown) | Same as `poweroff` |
| [`stats-core`](#stats-core) | Read battery and system statistics |
| [`stats-radio`](#stats-radio) | Read radio statistics |
| [`stats-packets`](#stats-packets) | Read packet counters |
| [`ping`](#ping) | Is a repeater reachable: time, hops and the weakest link SNR, named by its hop |
| [`trace`](#trace) | The same probe with each link's SNR both ways |
| [`get tz`](#tz) | Local time offset used in reports |
| [`set tz <offset>`](#tz) | Set the local time offset |

## ver

```text
ver
```

**Response format:**

```text
<upstream-version> (<upstream-build-date>) + MobMesh (<build-date> - <commit>)
```

## poweroff / shutdown

```
poweroff
shutdown
```

Upstream's power-off never wakes, so a remote node would stay off until someone visits it. Without a mod that provides a timed power-off, such as power-guard, both commands are refused:

| Response | Meaning |
| --- | --- |
| `ERR: poweroff not available in this build` | No mod in this build can power off and wake again |

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

[↩ Back to core readme](../README.md)

## ping

```text
ping <hash>
ping <hash> via <hash>,<hash>,...
ping last
```

| Parameter | Definition |
| --- | --- |
| `<hash>` | A repeater's path hash as `neighbors` shows it, at the node's hash width (4 hex digits at 2 bytes) |
| `via` | The repeaters between this node and the destination, in order, up to 5 (6 hops in all) |
| `last` | Reprint the most recent `ping` or `trace` result with its age; nothing is sent |

**Response:** one line, a few seconds after the command (a late reply over the remote CLI):

```
Reply 4ea1: time=1334ms hops=2 weakest(c5fb)=+10.50
```

`time` is the round trip measured here, `hops` the hops to the destination one way, `weakest(<hash>)` the lowest link SNR on the path in either direction, in dB, named by the hop at the far end of that link (`trace` shows both ends of it). No reply: `Request timed out.`, after a wait that grows with the hop count and the radio's airtime.

Over the remote CLI nothing is sent until the result is in, so no reply crosses the mesh while the trace does; a not-in-neighbours note arrives as the first line of the result.

**Effect:** sends one MeshCore TRACE out along the path and back the same way; stock repeaters forward it with no login. Repeaters not in the neighbour table are tried anyway, with a note: `c5fb not in neighbours, trying direct`.

| Response | Meaning |
| --- | --- |
| `ERR: <text> is not a hash, use <n> hex digits` | Not a full-width hex hash |
| `ERR: <hash> is the destination, leave it out of via` | The destination repeated in the path |
| `ERR: path too long (max 6 hops)` | More than 5 `via` hops |
| `ERR: ping already running` | One probe at a time |
| `ERR: no ping result yet` | `ping last` before any probe |

## trace

```text
trace <hash>
trace <hash> via <hash>,<hash>,...
trace last
```

Parameters, effect and errors as [`ping`](#ping).

**Response:** a header line, then one numbered line per link, numbered as `tracert` numbers hops. Each link shows the way out, then the way back; the SNR in brackets is how well the node at the arrowhead heard the signal, in dB. `me` is this repeater.

```
Trace to 4ea1 over 2 hops, 1334ms:
1  me→c5fb(+11.50) c5fb→me(+11.50)
2  c5fb→4ea1(+12.25) 4ea1→c5fb(+11.75)
```

A long trace that needs more than one reply ends the last with `Summary: time=<ms>ms hops=<n> weakest(<hash>)=<dB>`. No reply: `Trace to 4ea1 over 2 hops: no reply in 7s`.

## tz

```text
get tz
set tz <offset>
get diag.tz
set diag.tz <offset>
```

| Parameter | Definition |
| --- | --- |
| `offset` | Whole hours from UTC, -12 to +14, with or without `h` (`-5h`, `-5`, `+9`) |

**Response:** `> <hours>` for `get`; `OK` or `Error, must be from -12 to +14` for `set`; `ERR: could not save` when the file cannot be written.

**Storage:** persistent, in core's own file, stored as upstream's companion stores `tz_offset` (whole hours). A fixed offset with no daylight-saving rules: update it when the clocks change. `diag.tz` is the same setting under the name diag-report used; a node upgraded from diag-report keeps the offset it had there.

**Effect:** reports print local times when the offset is not 0, and UTC times when it is 0.

