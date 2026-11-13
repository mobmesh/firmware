# core CLI reference

[↩ Back to core readme](../README.md)

Commands answered by `core` on every MobMesh build, over serial and authenticated remote CLI. Core answers after every other mod, so a mod that handles one of these first takes precedence.

## Command index

| Command | Purpose |
| --- | --- |
| [`ver`](#ver) | Upstream version and build, plus the MobMesh build |
| [`poweroff`](#poweroff--shutdown) | Refused unless a mod provides a power-off that wakes |
| [`shutdown`](#poweroff--shutdown) | Same as `poweroff` |

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

[↩ Back to core readme](../README.md)
