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

## ver

```text
ver
```

**Response format:**

```text
<upstream-version> (<upstream-build-date>) + MobMesh (<build-date> - <commit>)
```

## poweroff / shutdown

```text
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

```text
Reply 4ea1: time=1334ms hops=2 weakest(c5fb)=+10.50
```

`time` is the round trip measured here, `hops` the hops to the destination one way, `weakest(<hash>)` the lowest link SNR on the path in either direction, in dB, named by the hop it sits on (`trace` lists that hop's two directions). No reply: `Request timed out.`, after a wait that grows with the hop count and the radio's airtime.

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

**Response:** a header line, then one numbered line per hop, as `tracert` lists them. `out` is how well that hop heard the one before it, `back` how well the one before heard it on the way back, in dB.

```text
Trace to 4ea1 over 2 hops, 1334ms:
  1  c5fb  out +11.50  back +11.50
  2  4ea1  out +12.25  back +11.75
```

A long trace that needs two replies ends the second with `Summary: time=<ms>ms hops=<n> weakest(<hash>)=<dB>`. No reply: `Trace to 4ea1 over 2 hops: no reply in 7s`.

