# sync-settings

MeshCore is an evolving platform, and region lists are a relatively new part of
the mesh. These lists help determine which traffic repeaters relay. Keeping them
current helps participants stay connected and keeps the local mesh running smoothly.

Region naming conventions and recommended settings are still evolving, too.
As they change, each repeater operator has to keep up: finding the latest guidance,
updating settings, and sometimes figuring out why a connection that worked yesterday
no longer works today. Across a whole mesh, that becomes a lot of maintenance.

`sync-settings` was designed to make that easier. Repeaters can subscribe to
authorized publishers that send periodic updates over the mesh with the region
lists and selected operating settings appropriate for their area. Instead of every
operator tracking every change, a trusted publisher can help the community stay
aligned.

For most operators, the goal is **set it once and let it stay up to date**.
As updates arrive, the repeater applies them automatically. Less time spent
searching websites for changed settings means more time enjoying the mesh—and a
better-configured community benefits everyone. Updates still depend on reception;
a disconnected repeater must receive a later update to catch up.

Region participation is optional and non-destructive. Sync adds a separate managed
layer, called an overlay, alongside the locally defined region list. Normal MeshCore
region commands continue to work on the local list without changing that separation.
The overlay adds another 32 region slots, allowing up to 64 distinct region codes
across the two lists. Matching deny rules still take precedence.

At any point, turning region sync off removes that managed layer from active use
and leaves the repeater using only its locally defined regions. Nothing in the local
region list is overwritten. Optional policy sync can also keep selected operating
settings aligned, but those changes remain applied after policy sync is turned off.

Think of `sync-settings` as **cruise control for keeping pace with the mesh**:
less routine upkeep, fewer configuration surprises, and an easier way to stay
connected with fellow participants.

## At a glance

- **Regions:** updates replace the sync overlay, never the native list.
- **Policy:** updates selected native operating settings—not radio settings or credentials.
- **Receiving:** requires sync on, a matching channel, and an authorized publisher.
- **Publishing:** works with receiving off; the command specifies its channel.
- **Routing:** overlay matches take priority; a matching deny in either list wins.
- **Turning off:** removes the region overlay from use; applied policy settings remain.
- **Room servers:** overlay lookups are supported; native forwarding behavior stays unchanged.

## Quick setup

**Consumer — subscribe to updates:**

```text
sync.publisher add <full-public-key>
set sync.channel release
sync.region on
sync.policy on
```

Enable regions, policy, or both, depending on the installed build.

**Publisher — prepare and send regions:**

```text
sync.region put us
sync.region put us-al us
sync.region put us-fl us
sync.region save
sync.region publish * release
sync.region publish.status
```

**Publisher — send current policy:**

```text
sync.policy publish * release
```

Before publishing:

- Radio settings must match and the mesh path must be usable.
- The clock must be valid.
- `*` requires native wildcard flooding to be allowed.
- A named route requires a locally flood-enabled region match.

## Updates on the mesh

### Normal publication

```text
Publisher -> Repeater A -> Repeater B -> Consumer C ->  Repeater D -> ...
 capture      relay          relay         relay          relay
 sign                                      subscribed?
 send                                      collect ->
                                           verify -> 
                                           save -> 
                                           activate
```

- Repeaters can relay updates without subscribing or trusting the publisher.
- Only publishers and consumers need firmware with `sync-settings`; all intermediate repeaters can run stock MeshCore.
- Consumers also pass traffic onward when local forwarding rules allow it.
- Relaying still follows local forwarding rules; denied routes can stop a flood.
- The old overlay stays active until the complete replacement is ready.
- Publication acknowledgement means started—not delivered to every consumer.

### Missing data

```text
Round 1:    announcement -> chunk 0 -> [chunk 1 lost] -> chunk 2
Next round: announcement -> chunk 0 -> chunk 1        -> chunk 2
Consumer:   retain collected chunks -> fill gap -> verify -> save -> activate
```

- Repeated rounds resend the same captured update, not later local edits.
- Missing chunks can be recovered while reception remains active.
- Already-applied updates are not repeatedly applied.
- A reboot stops publishing; a saved guard prevents immediate republishing.

### Abort

```text
publish.abort -> stop unsent data -> send two signed abort notices
Consumer still receiving -> discard partial update -> keep existing overlay
Consumer already applied -> keep applied update; no rollback
```

Abort notices can be lost. Unconfirmed transmission completion leaves a warning.

## CLI commands

Available through serial and authenticated remote admin CLI.
`[argument]` means optional; `<argument>` means required.

### Channel and publishers

| Command | Purpose |
| --- | --- |
| `get sync.channel` | Show receive channel. |
| `set sync.channel <channel>` | Save channel. Both sync features must be off and inbound/recovery work idle. |
| `sync.publisher list [offset]` | List trusted keys; zero-based offset, up to two per reply. |
| `sync.publisher add <full-public-key>` | Trust a publisher for regions and policy. |
| `sync.publisher remove <full-public-key>` | Revoke trust and cancel its inbound work; keep replay history. |
| `sync.publisher forget <full-public-key>` | After removal, erase its record and both replay histories. |

- **Public key:** complete 64-character hexadecimal key.
- **Channel:** 1–16 lowercase letters, digits, or hyphens; no leading/trailing hyphen.
- **Examples:** `release`, `early`, `policy1`. Channel labels are not secret keys.
- **Forget:** removes protection against previously seen updates from that key.

### Region list

`sync.region` mirrors MeshCore's native `region` command but targets the sync
overlay records. The native list stays separate and unchanged.

**Native-style commands**

| Command | Purpose |
| --- | --- |
| `sync.region [offset]` | Show RAM entries; follow `next <offset>` for more. Offsets start at zero; `F` means flood-enabled. |
| `sync.region put <name> [<parent>]` | Add/update a flood-enabled entry. Parent must exist; omission means top level. |
| `sync.region def <token> [<token> ...]` | Define a hierarchy using compact MeshCore-style notation. |
| `sync.region allowf <name>` | Allow flooding for an existing entry. |
| `sync.region denyf <name>` | Deny flooding for an existing entry. |
| `sync.region remove <name>` | Remove an entry; remove children first. |
| `sync.region save` | Save RAM edits; warns about incoming overwrites while sync is on. |

**Sync-specific additions**

| Command | Purpose |
| --- | --- |
| `sync.region on` | Enable overlay use and incoming region updates. Requires a channel. |
| `sync.region off` | Use native regions only; cancel inbound reception. |
| `sync.region clear` | Empty the RAM list; `save` makes it permanent. |
| `sync.region reload` | Discard RAM edits and reload the saved list. |

- **Edits are RAM-only until `save`.** Accepted mesh updates save automatically.
- While sync is on, edits affect routing and incoming updates can replace them.
- Limits: 32 public entries, 30-character names, seven hierarchy levels; no required root.
- `put` and `def` enable flooding; use `denyf` afterward if needed. A leading `#` is removed.
- Remote destructive edits may be deferred until after the reply; save may briefly return busy.

**Compact hierarchy example:**

```text
sync.region def us us-al|us us-fl
```

Result: `us` with children `us-al` and `us-fl`.
Each token descends a level; `|us` selects `us` as the next parent. Comma also works.

### Policy reception

| Command | Purpose |
| --- | --- |
| `sync.policy on` | Accept policy updates. Requires a channel. |
| `sync.policy off` | Stop accepting updates; keep already-applied settings. |

Policy publishes the current values of these native settings:

```text
flood.max              flood.max.unscoped     flood.max.advert
advert.interval        flood.advert.interval  path.hash.mode
loop.detect            multi.acks             af
txdelay                agc.reset.interval
```

### Publishing and monitoring

Replace `<dataset>` with `region` or `policy`:

| Command | Purpose |
| --- | --- |
| `sync.<dataset> publish <region\|*> <channel>` | Capture and broadcast repeated updates. |
| `sync.<dataset> publish.reset <region\|*> <channel>` | Recovery publication for an incorrectly far-ahead generation history; eligibility checks still apply. |
| `sync.<dataset> publish.abort` | Abort outbound work/guard; otherwise cancel inbound reception. No rollback or history erasure. |
| `sync.<dataset> publish.status` | Show activity, channel, progress, and relevant warnings/storage state. |
| `sync.<dataset> publish.report <page>` | Read outcomes/warnings; pages start at 1. |
| `get sync.<dataset>.publish.interval` | Show round interval. |
| `set sync.<dataset>.publish.interval <N>h` | Save interval: 3–24 hours. Example: `12h`. |
| `get sync.<dataset>.publish.duration` | Show publication window. |
| `set sync.<dataset>.publish.duration <N>d` | Save duration: 1–4 days. Example: `3d`. |

- Initial schedule: `12h` over `3d` = seven rounds, starting immediately.
- Regions and policy have independent campaigns and schedules.
- Schedule changes affect the next publication.
- Invalid timing input returns the required syntax. No reports returns `Err - no reports`.
- No user-facing restore or rollback command.

**Publish an empty overlay — explicit confirmation required:**

```text
sync.region publish * release -empty
```

This clears subscribed consumers' overlays—not native lists.
`-empty` also works with region `publish.reset`; policy does not use it.

## Hardening and security

- **Authorized updates:** full public-key trust; Ed25519 signs announcements, every chunk, and abort notices.
- **Replay protection:** saved history per publisher and dataset rejects old updates.
- **Integrity:** SHA-256 checks complete updates and paired, versioned storage records.
- **Bounded processing:** queued reception, fixed limits, validation before region replacement, and interrupted-policy recovery.
- **Private keys:** remain behind the signing interface; not handed to the mod.
- **Limits:** public traffic is not confidential; signatures cannot prevent radio flooding or interference.

Trust publishers carefully: authorization permits overlay replacement and changes
to the listed policy settings.
