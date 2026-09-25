# sync-settings

MeshCore is an evolving platform, which means change is the only inevitable constant.
One day it's a new region code, the next it's a different radio setting or frequency.
For a novice user, this can be a daunting task to keep up with.

`sync-settings` was designed to make that easier. Repeaters can subscribe to
authorized publishers that send periodic updates over the mesh with the region
lists and selected operating settings appropriate for their area. Instead of every
operator tracking every change, one or more [trusted publishers](#trusted-publishers) can help the community stay
aligned.

For most operators, the goal is simple: **set it once and let the repeater keep itself
up to date**, applying each update as it arrives. Less time spent
searching websites for changed settings means more time enjoying the mesh—and a
better-configured community benefits everyone. Updates still depend on reception;
a disconnected repeater must receive a later update to catch up.

Think of `sync-settings` as **cruise control for keeping pace with changes to the mesh**:
less routine upkeep, fewer configuration surprises, and an easier way to stay
connected with fellow participants.

## At a glance

| Campaign | With sync | Without sync |
|---|---|---|
| [**Region**](docs/region.md) | A managed region list overlay | Region changes are maintained locally. |
| [**Policy**](docs/policy.md) | The mesh's recommended settings | Policy settings are maintained locally. |
| [**Radio**](docs/radio.md) | Change when the mesh changes | Radio changes require local coordination. |

- **Subscribing:** trust a publisher, choose a channel, turn on each campaign type wanted.
- **Publishing:** needs no subscription; each publish command names its own channel.
- **Relaying:** stock MeshCore repeaters carry campaigns without the mod.

## Campaigns

Every update travels as a **campaign**: a publisher captures a dataset once, signs it,
and floods it across the mesh in repeated rounds over a set period. Region, policy and
radio are three kinds of campaign carried by the same machinery.

### How a campaign moves

```text
Publisher -> Repeater A -> Repeater B -> Subscriber C ->  Repeater D -> ...
 capture      relay          relay        relay            relay
 sign                                     subscribed?
 send                                     collect ->
                                          verify ->
                                          save ->
                                          activate
```

- Each round is an announcement followed by one or more chunks.
- Only publishers and subscribers need `sync-settings`; every relay can run stock MeshCore.
- Relaying follows local forwarding rules; a denied route can stop a flood.
- Subscribers also pass traffic onward when local rules allow it.
- A campaign is scoped to a region or to `*`, the wildcard.
- Publication acknowledgement means started—not delivered to every subscriber.

### Repeated rounds and missing data

```text
Round 1:    announcement -> chunk 0 -> [chunk 1 lost] -> chunk 2
Next round: announcement -> chunk 0 -> chunk 1        -> chunk 2
Subscriber: retain collected chunks -> fill gap -> verify -> save -> activate
```

- Every round resends the same captured update, not later local edits.
- Missing chunks can be recovered from a later round while reception remains active.
- A lost announcement costs that whole round.
- An already-applied update is not applied again.
- A reboot stops publishing; a saved guard prevents immediate republishing.

### Running campaigns together

- Region and policy campaigns can run at the same time, independently of each other.
- A radio campaign runs alone: while it is active, no region or policy campaign can start
  or be accepted.

### Campaign Feedback and Reports

Every campaign type has the same two monitoring commands. Both show **this repeater's own
view**: what it is doing as a publisher, a subscriber, or neither.

| Command | Shows |
|---|---|
| `sync.<type> publish.status` | What is happening now, in one line. |
| `sync.<type> publish.report [page]` | How the most recent campaign ended. |

Replace `<type>` with `region`, `policy` or `radio`.

**Region and policy status** shows the first of these that applies:

| Status | Role |
|---|---|
| `aborting notices n/2` | publishing |
| `publishing gen … round n/m` | publishing |
| `receiving <publisher> gen … chunks n/m` or `staged` | receiving |
| `recovering` (policy only) | receiving |
| `quiet gen …` — the guard after publishing ends | publishing |
| `idle` | neither |

**Region and policy reports** hold the latest outcome from either role; a newer event
replaces an older one. An outstanding abort warning appears as an extra first page.

- Publishing: `publication signing failed`, or an abort warning.
- Receiving: `applied`, `compressed payload could not be decoded`,
  `application failed; prior policy restored` or `application failed; campaign released`.

**Radio** keeps one migration record, so status and report read the same on the publishing
and receiving repeater. The report shows `committed`, `identical`, `cr-only`, `fallback`,
`aborted` or `fault`.

**Hop tally.** On region and policy receiving outcomes only, the report adds how many
hops each frame took. The publisher never learns how far its frames travelled.

```text
sync.region publish.report 1
  -> 1/1 gen 42 applied; replay settled hops 0x1,1x3
```

- Each term is `<hops>x<frames>`: here the announcement arrived direct and three chunks
  came through one relay.
- Hop counts absent from the line had no frames; the token is omitted when nothing arrived.
- Duplicate copies are dropped before the mod sees them, so the tally shows the shortest
  route that arrived first.
- A campaign that never lands leaves nothing to tally: this diagnoses a working path, not
  a broken one.

## Subscribing

A repeater accepts a campaign only when all three hold:

1. the campaign type is turned on;
2. the campaign's channel matches the repeater's channel; and
3. the publisher's full public key is in the trusted list.

```text
sync.publisher add <full-public-key>
set sync.channel release
sync.region on
sync.policy on
sync.radio on
```

Turn on any combination of campaign types the installed build supports.

### Trusted Publishers

- Trust is by complete 64-character hexadecimal public key.
- One trusted list covers region, policy and radio campaigns.
- `remove` revokes trust and cancels that publisher's inbound work, but keeps its replay
  history.
- `forget`, after `remove`, erases the record and its replay history. Previously seen
  updates from that key would then be accepted again.

### Subscription Channels

A channel lets one publisher run separate streams—for example `release` for the whole
mesh and `early` for a few test repeaters. A subscriber hears only its own channel.

- 1–16 letters, digits, `-` or `_`; uppercase becomes lowercase.
- Examples: `release`, `early`, `policy1`.
- Channel labels are not secret keys.
- Changing the channel requires every sync feature off and no inbound work in progress.

### Commands

| Command | What it does |
|---|---|
| `get sync.channel` | Show receive channel. |
| `set sync.channel <channel>` | Save channel. |
| `sync.publisher list [offset]` | List trusted keys; zero-based offset, up to two per reply. |
| `sync.publisher add <full-public-key>` | Trust a publisher. |
| `sync.publisher remove <full-public-key>` | Revoke trust and cancel its inbound work; keep replay history. |
| `sync.publisher forget <full-public-key>` | After removal, erase its record and replay history. |

## Region and policy publishing

Shared by [region](docs/region.md) and [policy](docs/policy.md) campaigns
([radio campaigns](docs/radio.md) are special and use their own commands).
Replace `<dataset>` with `region` or `policy`.

- Initial schedule: every `12h` over `3d`, seven rounds, starting immediately.
- Region and policy keep independent schedules; a change affects the next publication.
- There is no restore or rollback command.

**Abort:**

```text
publish.abort -> stop unsent data -> send two signed abort notices
Repeater still receiving -> discard partial update -> keep existing state
Repeater already applied  -> keep applied update; no rollback
```

Abort notices can be lost; an unconfirmed transmission leaves a warning.

| Command | What it does |
|---|---|
| `sync.<dataset> publish <region\|*> <channel> [-raw]` | Capture and broadcast repeated updates. The reply names the format, size and frames per round. |
| `sync.<dataset> publish.reset <region\|*> <channel> [-raw]` | Recovery publication for a generation history that is incorrectly far ahead. |
| `sync.<dataset> publish.abort` | Abort outbound work; otherwise cancel inbound reception. No rollback. |
| `sync.<dataset> publish.status` | Show activity, channel, progress, warnings and storage state. |
| `sync.<dataset> publish.report [page]` | Read outcomes and warnings; pages start at 1. |
| `get sync.<dataset>.publish.interval` | Show round interval. |
| `set sync.<dataset>.publish.interval <N>h` | Save interval: 3–24 hours. |
| `get sync.<dataset>.publish.duration` | Show publication window. |
| `set sync.<dataset>.publish.duration <N>d` | Save duration: 1–4 days. |

## Hardening and security

- **Authorized updates:** full public-key trust; Ed25519 signs announcements, every chunk,
  confirmations and abort notices.
- **Replay protection:** saved history per publisher and dataset rejects old updates.
- **Integrity:** SHA-256 checks complete updates and paired, versioned storage records.
- **Bounded processing:** queued reception, fixed limits, validation before replacement, and
  boot recovery of interrupted work.
- **Compressed payloads are checked, not trusted:** decompression happens only after every
  chunk's signature passes, is bounded on input and output, and the integrity check covers
  the decompressed table.
- **Private keys:** remain behind the signing interface; not handed to the mod.
