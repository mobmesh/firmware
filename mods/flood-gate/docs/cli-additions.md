# flood-gate CLI reference

[↩ Back to flood-gate readme](../README.md)

Commands added by `flood-gate`, available over serial and authenticated remote CLI.

`<value>` denotes a required parameter. `[value]` is optional.

## Command index

| Command | Purpose |
| --- | --- |
| [`sinkhole list [from]`](#sinkhole-list) | List entries by slot |
| [`sinkhole show <slot>`](#sinkhole-show) | One entry and its counters |
| [`sinkhole add <hash\|*> [hours] [chan <channel>] [depth <n\|any>] [minbytes <2\|3>]`](#sinkhole-add) | Stop forwarding matching channel floods |
| [`sinkhole watch <hash\|*> [hours] [chan <channel>] [depth <n\|any>] [minbytes <2\|3>]`](#sinkhole-watch) | Count matching channel floods without dropping them |
| [`sinkhole remove <hash\|*> [chan <channel>]`](#sinkhole-remove) | Lift entries |
| [`sinkhole clear`](#sinkhole-clear) | Lift every entry |
| [`sinkhole stats`](#sinkhole-stats) | Counters since boot |

## sinkhole list

```text
sinkhole
sinkhole list [from]
```

After a blank first line, one entry per line from slot `from` (default 1): `<slot>. <key> <rule>`.
A slot number stays with its entry until it is removed or expires. When the reply cannot hold every
entry it ends with `next <slot>`; pass that slot as `from`.

```
3. a1b2 watch
4. c3 drop
5. * drop
```

| Response | Meaning |
| --- | --- |
| `(none)` | No entries |
| `(none past that)` | No entries from that slot on |

## sinkhole show

```text
sinkhole show <slot>
```

After a blank first line, the rule, then its counters:

```
3. key:a1b2 rule:watch chan:public depth:3 left:23h
match:41 hop:1x30/2x9/3x2/+x0 width:1x38/2x3/3x0 last:4m
```

| Field | Meaning |
| --- | --- |
| `key` | Repeater hash; with `minbytes`, only the bytes a narrower flood carries. `*` for any repeater |
| `rule` | `drop` or `watch` |
| `chan` | `any`, or the channel as typed |
| `depth` | Hops checked from the origin; absent for `*` |
| `minbytes` | Shown when set |
| `left` | Time until the entry expires |
| `match` | Channel floods matched |
| `hop` | Matches by the position of the key in the path: 1st, 2nd, 3rd, 4th or later. Absent for `*` |
| `width` | Matches by path hash width: 1, 2, 3 bytes |
| `last` | Time since the last match, `-` for none |

Counts of 10,000 or more show as `12k`, `3M`, `4G`. Counters are kept in RAM: they restart on reboot
and when the entry is re-added.

## sinkhole add

```text
sinkhole add <hash|*> [hours] [chan <channel>] [depth <n|any>] [minbytes <2|3>]
```

Only channel floods (messages and datagrams) are matched; adverts, direct messages, requests and
logins always pass. The hash comes first; the other parameters can follow in any order.

| Parameter | Definition |
| --- | --- |
| `hash` | Repeater hash, 2-6 hex digits in whole bytes |
| `*` | Any repeater; needs `minbytes`, and refuses `depth` |
| `hours` | 1-168, default 24. Every entry expires |
| `chan <channel>` | One channel: `public`, `#name`, or `0xNN` for a private channel's hash; default every channel |
| `depth <n\|any>` | Hops checked from the origin, 1-63 or any; default 3, the first three repeaters |
| `minbytes <2\|3>` | Path hash width in bytes; floods at least this wide pass, narrower ones drop |

Entries match at the packet's path hash width, using as many bytes as both the entry and the packet
carry. Re-adding the same hash and channel replaces the entry in its slot and restarts its timer;
this also turns a `watch` entry into an enforcing one. Up to 16 entries.

| Response | Meaning |
| --- | --- |
| `Error: hash is 2-6 hex digits, whole bytes, or *` | Bad hash |
| `Error: chan is public, #name or 0xNN` | Missing or unknown channel |
| `Error: * needs minbytes` | `*` would otherwise match every channel flood |
| `Error: depth needs a hash` | `*` has no path position |
| `Error: sinkhole full (16)` | No free entry |

## sinkhole watch

```text
sinkhole watch <hash|*> [hours] [chan <channel>] [depth <n|any>] [minbytes <2|3>]
```

As `sinkhole add`, but matching floods are forwarded and only counted. Re-adding the same hash and
channel with `sinkhole add` starts dropping.

## sinkhole remove

```text
sinkhole remove <hash|*> [chan <channel>]
```

Without a channel, every entry for that hash is removed.

## sinkhole clear

```text
sinkhole clear
```

## sinkhole stats

```text
sinkhole stats
```

| Response | Meaning |
| --- | --- |
| `drops 12 (minbytes 3), watch matches 40, entries 2/16` | Floods dropped since boot, of which by width; matches on watch entries; entries in use |
