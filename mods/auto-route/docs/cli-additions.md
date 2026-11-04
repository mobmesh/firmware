# auto-route CLI reference

[↩ Back to auto-route readme](../README.md)

Commands added by `auto-route`, available over serial and authenticated remote CLI.

`<value>` denotes a required parameter.

## Command index

| Command | Purpose |
| --- | --- |
| [`get autoroute`](#autoroute) | Whether resends and detours are on |
| [`set autoroute <on\|off>`](#autoroute) | Turn the mod on or off |
| [`set autoroute.detour <on\|off>`](#autoroute) | Turn detours on or off |
| [`get autoroute.stats`](#autoroutestats) | Counters since boot |
| [`get autoroute.links`](#autoroutelinks) | What has been learned |

## autoroute

```text
get autoroute
set autoroute <on|off>
set autoroute.detour <on|off>
```

**Response:** `> <on|off>, detour <on|off>` for `get`. `OK` for `set`, or `OK - but repeat is off`, `Error: use on or off`, `ERR: could not save`.

**Effect:** saved and applied at once. Off by default, with detours on once it is turned on. Turning it off drops pending resends and turns detours back on, so `set autoroute on` always gives the full mod. With `repeat` off the node relays nothing, so only its own direct sends are protected.

## autoroute.stats

```text
get autoroute.stats
```

**Response:** `> resent=<n> echo=<n> reply=<n> exhausted=<n> detour=<n> noalt=<n> replied=<n> noslot=<n> stale=<n>`

| Field | Meaning |
| --- | --- |
| `resent` | Packets sent again |
| `echo` | Resends stopped by hearing the next hop repeat the packet |
| `reply` | Resends stopped by a hop reply |
| `exhausted` | Packets that used every resend |
| `detour` | Packets sent around a failing hop |
| `noalt` | Times no armed neighbour was available |
| `replied` | Hop replies sent |
| `noslot` | Packets not protected because every slot was in use |
| `stale` | Packets given up on because they sat queued for 10 s without being sent |

## autoroute.links

```text
get autoroute.links
```

**Response:** `>` then entries of `<hop>><neighbour>:<forward>/<reverse>`, with `*` when armed. For example `30a1>40b2:0/4*` says neighbour `40b2` was seen receiving from `30a1` four times. Arrives as several replies when long.

[↩ Back to auto-route readme](../README.md)
