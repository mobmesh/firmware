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
