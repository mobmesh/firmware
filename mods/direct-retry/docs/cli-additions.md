# direct-retry CLI reference

[↩ Back to direct-retry readme](../README.md)

Commands added by `direct-retry`, available over serial and authenticated remote CLI.

`<value>` denotes a required parameter.

## Command index

| Command | Purpose |
| --- | --- |
| [`get direct.retry`](#directretry) | Whether resends are on |
| [`set direct.retry <on\|off>`](#directretry) | Turn the mod on or off |
| [`get direct.retry.stats`](#directretrystats) | Counters since boot |
| [`get direct.retry.hops`](#directretryhops) | Resends per next hop since boot |

## direct.retry

```text
get direct.retry
set direct.retry <on|off>
```

**Response:** `> <on|off>` for `get`. `OK` for `set`, or `OK - but repeat is off`, `Error: use on or off`, `ERR: could not save`.

**Effect:** saved and applied at once. Off by default. Turning it off drops pending resends. With `repeat` off the node relays nothing, so only its own direct sends are protected.

## direct.retry.stats

```text
get direct.retry.stats
```

**Response:** `> resent=<n> echo=<n> reply=<n> exhausted=<n> replied=<n> noslot=<n> stale=<n> retried=<n> won=<n>`

| Field | Meaning |
| --- | --- |
| `resent` | Packets sent again |
| `echo` | Resends stopped by hearing the next hop repeat the packet |
| `reply` | Resends stopped by a hop reply |
| `exhausted` | Packets that used every resend |
| `replied` | Hop replies sent |
| `noslot` | Packets not protected because every slot was in use |
| `stale` | Packets given up on because they sat queued for 10 s without being sent |
| `retried` | Packets that needed at least one resend |
| `won` | Of those, packets then confirmed by an echo or a hop reply |

## direct.retry.hops

```text
get direct.retry.hops
```

**Response:** `> <hop>:<retried>/<won> ...`, one entry per next hop with at least one resend, up to 8 hops; `> none` before any resend. `<hop>` is the next hop's path hash at the mesh's width (`a6b2`, or `a6` on 1-byte meshes).

**Effect:** none; read only. A packet's next hop is the first hash left in its path; packets on their last hop are not attributed. With all 8 entries in use, a new hop replaces the one with the fewest resends.

[↩ Back to direct-retry readme](../README.md)
