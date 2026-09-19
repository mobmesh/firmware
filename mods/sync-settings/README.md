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
Publisher -> Repeater A -> Repeater B -> Subscriber C ->  Repeater D -> ...
 capture      relay          relay        relay            relay
 sign                                     subscribed?
 send                                     collect ->
                                          verify -> 
                                          save -> 
                                          activate
```

- Repeaters can relay updates without subscribing or trusting the publisher.
- Only publishers and subscribers need firmware with `sync-settings`; all intermediate repeaters can run stock MeshCore.
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

## CLI Commands

Available through serial and authenticated remote admin CLI.
`[argument]` means optional; `<argument>` means required. In the publishing rows, replace
`<dataset>` with `region` or `policy`.

<table>
<thead><tr><th align="left">Command</th><th align="left">What it does</th></tr></thead>
<tbody>
<tr><th colspan="2" align="left">📡 Channel and publishers</th></tr>
<tr><td><code>get sync.channel</code></td><td>Show receive channel.</td></tr>
<tr><td><code>set sync.channel &lt;channel&gt;</code></td><td>Save channel: 1–16 letters, digits, <code>-</code> or <code>_</code>; uppercase becomes lowercase. Both sync features must be off and inbound/recovery work idle.</td></tr>
<tr><td><code>sync.publisher list [offset]</code></td><td>List trusted keys; zero-based offset, up to two per reply.</td></tr>
<tr><td><code>sync.publisher add &lt;full-public-key&gt;</code></td><td>Trust a publisher for regions and policy.</td></tr>
<tr><td><code>sync.publisher remove &lt;full-public-key&gt;</code></td><td>Revoke trust and cancel its inbound work; keep replay history.</td></tr>
<tr><td><code>sync.publisher forget &lt;full-public-key&gt;</code></td><td>After removal, erase its record and both replay histories.</td></tr>
<tr><th colspan="2" align="left">🗺️ Region list: native-style</th></tr>
<tr><td><code>sync.region [offset]</code></td><td>Show RAM entries; follow <code>next &lt;offset&gt;</code> for more. Offsets start at zero; <code>F</code> means flood-enabled.</td></tr>
<tr><td><code>sync.region put &lt;name&gt; [&lt;parent&gt;]</code></td><td>Add/update a flood-enabled entry. Parent must exist; omission means top level.</td></tr>
<tr><td><code>sync.region def &lt;token&gt; [&lt;token&gt; ...]</code></td><td>Define a hierarchy using compact MeshCore-style notation.</td></tr>
<tr><td><code>sync.region allowf &lt;name&gt;</code></td><td>Allow flooding for an existing entry.</td></tr>
<tr><td><code>sync.region denyf &lt;name&gt;</code></td><td>Deny flooding for an existing entry.</td></tr>
<tr><td><code>sync.region remove &lt;name&gt;</code></td><td>Remove an entry; remove children first.</td></tr>
<tr><td><code>sync.region save</code></td><td>Save RAM edits; warns about incoming overwrites while sync is on.</td></tr>
<tr><th colspan="2" align="left">🔄 Region list: sync-specific</th></tr>
<tr><td><code>sync.region on</code></td><td>Enable overlay use and incoming region updates. Requires a channel.</td></tr>
<tr><td><code>sync.region off</code></td><td>Use native regions only; cancel inbound reception.</td></tr>
<tr><td><code>sync.region clear</code></td><td>Empty the RAM list; <code>save</code> makes it permanent.</td></tr>
<tr><td><code>sync.region reload</code></td><td>Discard RAM edits and reload the saved list.</td></tr>
<tr><th colspan="2" align="left">📥 Policy reception</th></tr>
<tr><td><code>sync.policy on</code></td><td>Accept policy updates. Requires a channel.</td></tr>
<tr><td><code>sync.policy off</code></td><td>Stop accepting updates; keep already-applied settings.</td></tr>
<tr><td><code>get repeat.gate</code></td><td>Show whether flood packets with no permitted region are dropped.</td></tr>
<tr><td><code>set repeat.gate &lt;on|off&gt;</code></td><td>Drop flood packets whose region scope is unknown or flood-denied. Carried by policy campaigns. Only a room server acts on it; a repeater always gates and stores the value so a shared profile applies cleanly on both.</td></tr>
<tr><th colspan="2" align="left">📤 Publishing and monitoring</th></tr>
<tr><td><code>sync.&lt;dataset&gt; publish &lt;region|*&gt; &lt;channel&gt; [-raw]</code></td><td>Capture and broadcast repeated updates. The reply names the payload format, its size and the frames per round.</td></tr>
<tr><td><code>sync.&lt;dataset&gt; publish.reset &lt;region|*&gt; &lt;channel&gt; [-raw]</code></td><td>Recovery publication for an incorrectly far-ahead generation history; eligibility checks still apply.</td></tr>
<tr><td><code>sync.&lt;dataset&gt; publish.abort</code></td><td>Abort outbound work/guard; otherwise cancel inbound reception. No rollback or history erasure.</td></tr>
<tr><td><code>sync.&lt;dataset&gt; publish.status</code></td><td>Show activity, channel, progress, and relevant warnings/storage state.</td></tr>
<tr><td><code>sync.&lt;dataset&gt; publish.report [page]</code></td><td>Read outcomes/warnings; pages start at 1, and omitting the page shows the first.</td></tr>
<tr><td><code>get sync.&lt;dataset&gt;.publish.interval</code></td><td>Show round interval.</td></tr>
<tr><td><code>set sync.&lt;dataset&gt;.publish.interval &lt;N&gt;h</code></td><td>Save interval: 3–24 hours. Example: <code>12h</code>.</td></tr>
<tr><td><code>get sync.&lt;dataset&gt;.publish.duration</code></td><td>Show publication window.</td></tr>
<tr><td><code>set sync.&lt;dataset&gt;.publish.duration &lt;N&gt;d</code></td><td>Save duration: 1–4 days. Example: <code>3d</code>.</td></tr>
</tbody>
</table>

### Channel and publishers

- **Public key:** complete 64-character hexadecimal key.
- **Channel:** 1–16 lowercase letters, digits, or hyphens; no leading/trailing hyphen.
- **Examples:** `release`, `early`, `policy1`. Channel labels are not secret keys.
- **Forget:** removes protection against previously seen updates from that key.

### Region list

`sync.region` mirrors MeshCore's native `region` command but targets the sync
overlay records. The native list stays separate and unchanged.

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

Policy publishes the current values of these native settings:

```text
flood.max              flood.max.unscoped     flood.max.advert
advert.interval        flood.advert.interval  path.hash.mode
loop.detect            multi.acks             af
txdelay                agc.reset.interval
```

### Publishing and monitoring

- Initial schedule: `12h` over `3d` = seven rounds, starting immediately.
- Regions and policy have independent campaigns and schedules.
- Schedule changes affect the next publication.
- Invalid timing input returns the required syntax. No reports returns `empty`.
- No user-facing restore or rollback command.

**Publish an empty overlay — explicit confirmation required:**

```text
sync.region publish * release -empty
```

This clears subscribed consumers' overlays—not native lists.
`-empty` also works with region `publish.reset`; policy does not use it.

**Compressed region tables:**

A region table is usually repetitive—shared prefixes, similar names—so the publisher
compresses it opportunistically to save on air-time. A full 32-entry table goes
out in 5 frames per round instead of 13. Compression is decided per campaign and the reply
says which was used:

```text
sync.region publish * release
  -> OK - deflated 280 B, 5 frames per round
```

- Compression is only chosen when it saves a **chunk**, not merely bytes. A table that
  already fits one chunk, or one that does not shrink enough to drop a chunk, is sent as-is.
- A publisher needs PSRAM to hold the compressor. Boards without it publish uncompressed,
  automatically, with no configuration. The Heltec v4 for example can support compression.
- Consumers need no setting. All sync-setting clients support decompression. If a board fails
  decompression for some reason it will say so in its `publish.report`. 

**Send uncompressed:**

```text
sync.region publish * release -raw
```

`-raw` forces the uncompressed format for that campaign. Note: A compressed campaign can always be 
retried as a plain one in the field.

## How far the campaign travelled

A receiver records how many hops each frame of a campaign took and tallies them on the
report once the campaign ends:

```text
sync.region publish.report 1
  -> 1/1 gen 42 applied; replay settled hops 0x1,1x3
```

Each term is `<hops>x<frames>`: here the manifest arrived direct and three chunks came
through one relay. Hop counts absent from the line had no frames. The token is omitted
entirely when nothing was received, so `0x5` genuinely means five direct frames.

Two caveats. Duplicate copies of a frame are dropped before the mod sees them, so what is
recorded is the shortest route that happened to arrive first -- a node in direct range of
the publisher reads all zeroes no matter how busy the relays around it are. And a campaign
that never lands leaves no frames to tally, so this diagnoses a working path, not a broken
one.


## Hardening and security

- **Authorized updates:** full public-key trust; Ed25519 signs announcements, every chunk, and abort notices.
- **Replay protection:** saved history per publisher and dataset rejects old updates.
- **Integrity:** SHA-256 checks complete updates and paired, versioned storage records.
- **Bounded processing:** queued reception, fixed limits, validation before region replacement, and interrupted-policy recovery.
- **Compressed payloads are checked, not trusted:** decompression happens only after every chunk's signature passes, is bounded on input and output, and the SHA-256 integrity check covers the decompressed table, never the bytes on air.
- **Private keys:** remain behind the signing interface; not handed to the mod.
- **Limits:** public traffic is not confidential; signatures cannot prevent radio flooding or interference.

Trust publishers carefully: authorization permits overlay replacement and changes
to the listed policy settings.
