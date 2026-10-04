# sync-settings CLI reference

Commands added or extended by `sync-settings`, available over serial and authenticated
remote CLI. A campaign command exists only when that campaign type is included in
the build. Setup and campaign behavior remain in the [mod README](../README.md) and
[region](region.md), [policy](policy.md), [radio](radio.md), and [time](time.md) guides.

`<value>` denotes a required parameter; `[value]` is optional; `A|B` denotes a choice.
Do not type the brackets. Time settings require the unit suffix shown in their syntax.

## Command index

| Command family | Purpose |
| --- | --- |
| [`get / set sync.channel`](#get--set-syncchannel) | Read or save the receive channel |
| [`get / set repeat.gate`](#get--set-repeatgate) | Read or set region gating for room-server repeats |
| [`get / set publication timing`](#get--set-publication-timing) | Read or save region and policy publication timing |
| [`get / set sync.radio.schedule`](#get--set-syncradioschedule) | Read or save radio campaign timing |
| [`get / set sync.time settings`](#get--set-synctime-settings) | Read or save time campaign timing and tolerance |
| [`sync.publisher list`](#syncpublisher-list) | List authorized publisher keys |
| [`sync.publisher add / remove / forget`](#syncpublisher-add--remove--forget) | Manage publisher trust |
| [`sync.region`](#syncregion) | Read the working region overlay |
| [`sync.region put`](#syncregion-put) | Add or update an overlay entry |
| [`sync.region def`](#syncregion-def) | Define an overlay using compact hierarchy notation |
| [`sync.region allowf / denyf / remove`](#syncregion-allowf--denyf--remove) | Change overlay forwarding or remove an entry |
| [`sync.region save / clear / reload`](#syncregion-save--clear--reload) | Manage overlay persistence |
| [`sync.policy`](#syncpolicy) | Read the policy values captured for publication |
| [`sync subscriptions`](#sync-subscriptions) | Enable or disable campaign reception |
| [`sync.region / sync.policy publish`](#syncregion--syncpolicy-publish) | Publish a captured dataset |
| [`sync.region / sync.policy publish.reset`](#syncregion--syncpolicy-publishreset) | Publish exceptional generation recovery |
| [`sync.region / sync.policy publish.abort`](#syncregion--syncpolicy-publishabort) | Cancel outbound or incomplete inbound work |
| [`sync.region / sync.policy publish.status`](#syncregion--syncpolicy-publishstatus) | Read dataset campaign activity |
| [`sync.region / sync.policy publish.status.err`](#syncregion--syncpolicy-publishstatuserr) | Read a current status error |
| [`sync.region / sync.policy publish.report`](#syncregion--syncpolicy-publishreport) | Read campaign outcomes and warnings |
| [`sync.radio publish.arm / publish.go`](#syncradio-publisharm--publishgo) | Validate and launch a radio migration |
| [`sync.radio publish.abort`](#syncradio-publishabort) | Cancel a pending radio migration |
| [`sync.radio publish.status`](#syncradio-publishstatus) | Read migration state |
| [`sync.radio publish.report`](#syncradio-publishreport) | Read the latest radio migration result |
| [`sync.time publish`](#synctime-publish) | Start a signed time-sample schedule |
| [`sync.time publish.abort`](#synctime-publishabort) | Stop local time publication |
| [`sync.time publish.status`](#synctime-publishstatus) | Read time schedule and last received result |

## get / set sync.channel

```text
get sync.channel
set sync.channel <channel>
```

| Item | Definition |
| --- | --- |
| `channel` | 1–16 letters, digits, `-`, or `_`; normalized to lowercase |
| Get response | `> <channel>` or `> unset` |
| Set response | `OK` or `Err - ...` |
| Storage | Persistent |

**Restrictions:** All included campaign subscriptions must be off. The channel is a
stream selector, not an authentication credential.

## get / set repeat.gate

```text
get repeat.gate
set repeat.gate <on|off>
```

**Response:** `> on` or `> off` when reading; `OK - repeat.gate is now ON`,
`OK - repeat.gate is now OFF`, or `Err - ...` when setting.

**Effect:** On room servers, `on` requires region eligibility for forwarding.
Repeaters already apply region filtering and store the value for policy compatibility.
The default is off; the setting is persistent.

**Extension:** Native `set repeat on` also enables `repeat.gate` on a room server.

## get / set publication timing

```text
get sync.<dataset>.publish.interval
set sync.<dataset>.publish.interval <N>h
get sync.<dataset>.publish.duration
set sync.<dataset>.publish.duration <N>d
```

| Parameter | Definition |
| --- | --- |
| `dataset` | `region` or `policy` |
| `N` for interval | 3–24 hours; suffix `h` is required |
| `N` for duration | 1–4 days; suffix `d` is required |

**Response:** `> <N>h` or `> <N>d` when reading; `OK` or `Err - ...` when setting.

**Storage:** Persistent. Changes configure the next publication; they do not rewrite
an active campaign's timing.

## get / set sync.radio.schedule

```text
get sync.radio.schedule
set sync.radio.schedule <campaign>,<test-interval>,<test-window>,<duration>,<confirm-interval>,<confirm-window>
```

All six values are integer minutes with an `m` suffix.

| Field | Production limits | Default |
| --- | --- | --- |
| `campaign` | Greater than 5 minutes and less than duration | `10m` |
| `test-interval` | `0m` with `test-window=0m`, or greater than 30 minutes | `0m` |
| `test-window` | `0m` with tests disabled; otherwise positive, less than 25 minutes and test interval | `0m` |
| `duration` | Positive, at most 1440 minutes; enough time for a complete test if enabled | `360m` |
| `confirm-interval` | Positive and less than confirmation window | `5m` |
| `confirm-window` | 15–35791 minutes | `45m` |

**Response:** `> <campaign>m,<test-interval>m,<test-window>m,<duration>m,<confirm-interval>m,<confirm-window>m`
when reading; `OK` or `Err - ...` when setting.

**Restrictions:** A test interval plus test window plus one minute must fit before
cutover. Refused while campaign work locks radio changes. Saving a schedule clears
the RAM-only publish arm. Settings are persistent.

## get / set sync.time settings

```text
get sync.time.tolerance
set sync.time.tolerance <N>m
get sync.time.publish.interval
set sync.time.publish.interval <N>h|<N>d
get sync.time.publish.duration
set sync.time.publish.duration <N>d
get sync.time.ntp.interval
set sync.time.ntp.interval <N>h|<N>d|0
```

| Setting | Accepted values | Meaning |
| --- | --- | --- |
| `tolerance` | 1–1440 minutes, suffix `m` | Maximum tolerated receiver-clock offset in outgoing samples |
| `publish.interval` | 1 hour–30 days, suffix `h` or `d` | Time between samples |
| `publish.duration` | 1–365 days, suffix `d` | Schedule duration |
| `ntp.interval` | 1 hour–365 days, suffix `h` or `d`, or `0` | Hotspot clock refresh cadence; `0` refreshes every sample |

**Response:** `> <value>` with its unit when reading; unset tolerance returns
`> unset`, and every-sample NTP returns `> 0`. Set returns `OK` or `Err - ...`.

**Restrictions:** Publication interval must not exceed duration. Tolerance must be
set before publishing. Settings are persistent; NTP interval changes take effect
on the next sample.

## sync.publisher list

```text
sync.publisher list [offset]
```

| Parameter | Definition |
| --- | --- |
| `offset` | Zero-based record offset; omitted means zero |

**Response:** Up to two full authorized public keys per reply, separated by spaces,
or `empty` / `Err - ...`. Increase the offset by the number of keys returned to page.
Removed records are retained internally but are not listed.

## sync.publisher add / remove / forget

```text
sync.publisher add <full-public-key>
sync.publisher remove <full-public-key>
sync.publisher forget <full-public-key>
```

| Parameter | Definition |
| --- | --- |
| `full-public-key` | Exactly 64 hexadecimal characters; prefixes are not accepted |

| Action | Effect |
| --- | --- |
| `add` | Authorize a publisher for all included campaign types |
| `remove` | Revoke authority and cancel inbound work; retain replay history |
| `forget` | After removal, erase its record and replay history |

**Response:** `OK` or `Err - ...`.

**Storage:** Persistent, with capacity for 16 records including retained removed keys.

**Restrictions:** Forget requires prior removal and can be refused while recovery or
replay settlement is pending. It removes protection against previously seen generations.

## sync.region

```text
sync.region [offset]
```

**Parameter:** Zero-based offset; omitted means zero.

**Response:** Indented region tree with `F` for flood-enabled entries and a
`next <offset>` line when more entries remain; `empty` or `Err - ...` otherwise.

**Scope:** Reads the RAM overlay, separate from the native MeshCore region list.

## sync.region put

```text
sync.region put <name> [<parent>]
```

| Parameter | Definition |
| --- | --- |
| `name` | Region name, at most 30 bytes; must satisfy native region-name rules |
| `parent` | Existing parent name; omission selects top level |

**Response:** `OK` or `Err - ...`.

**Effect:** Adds or updates a flood-enabled entry in RAM. Requires `sync.region save`
for persistence. Invalid hierarchy or exhausted capacity is refused.

## sync.region def

```text
sync.region def <token> [<token> ...]
```

**Parameter:** MeshCore-style region hierarchy tokens. Each token descends one level; a
`|parent` or `,parent` suffix selects the next parent. For example,
`us us-al|us us-fl` defines `us` with children `us-al` and `us-fl`.

**Response:** `OK` or `Err - ...`.

**Effect:** Defines the working overlay in RAM. Requires `sync.region save` for persistence.
Invalid names or hierarchy and exhausted capacity are refused.

## sync.region allowf / denyf / remove

```text
sync.region allowf <name>
sync.region denyf <name>
sync.region remove <name>
```

| Action | Effect |
| --- | --- |
| `allowf` | Enable flooding for an existing entry |
| `denyf` | Disable flooding for an existing entry |
| `remove` | Remove an entry; children must be removed first |

**Response:** `OK`, `OK - accepted; save required` for deferred remote destructive
edits, or `Err - ...`.

**Storage:** RAM only until saved. Destructive remote edits take effect after the
reply is queued. Further edits can be refused while an earlier edit is pending.

## sync.region save / clear / reload

```text
sync.region save
sync.region clear
sync.region reload
```

| Action | Effect | Successful response |
| --- | --- | --- |
| `save` | Persist RAM edits | `OK` or `OK - campaigns may overwrite` |
| `clear` | Empty RAM overlay; save separately to persist | `OK - save required`, or `OK - accepted; save required` remotely |
| `reload` | Discard RAM edits and load saved overlay | `OK` or `OK - accepted`; may include a degraded-storage indication |

**Errors:** `Err - ...`, including pending edits, receipt settlement, and storage failures.
Incoming campaigns can replace a saved overlay when region sync is on.

## sync.policy

```text
sync.policy
```

**Response format:**

```text
flood:<all>/<unscoped>/<advert> adv:<local>m/<flood>h path:<mode> loop:<value> ack:<value> gate:<value> af:<factor> tx:<factor> agc:<seconds>s
```

**Effect:** Read-only view of selected native policy values and `repeat.gate`.
Storage or unavailable native-policy errors return `Err - ...`.

## sync subscriptions

```text
sync.region <on|off>
sync.policy <on|off>
sync.radio <on|off>
sync.time <on|off>
```

**Response:** `OK` or `Err - ...`. Region/policy off can return
`OK - off until reboot; storage failed` when the runtime change could not be persisted.

| Type | Prerequisite for `on` | Effect of `off` |
| --- | --- | --- |
| Region | Saved channel | Stop reception and overlay routing; retain saved overlay |
| Policy | Saved channel | Stop reception; retain applied policy |
| Radio | Saved channel and trusted publisher | Stop reception and cancel pending received migration before commit |
| Time | Saved channel | Stop accepting samples; retain clock and local publish schedule |

**Storage:** Enable states are persistent when saved successfully.

**Restrictions:** Radio off refuses during a local publication or durable commit;
use `sync.radio publish.abort` for a local migration. Trust is still required for
accepting signed campaigns even where `on` does not require a populated trust list.

## sync.region / sync.policy publish

```text
sync.region publish <region|*> <channel> [-empty] [-raw]
sync.policy publish <region|*> <channel> [-raw]
```

| Parameter | Definition |
| --- | --- |
| `region` | Defined, flood-enabled scope |
| `*` | Unscoped publication |
| `channel` | Target receive channel, 1–16 letters, digits, `-`, or `_` |
| `-empty` | Region only: explicitly allow an empty overlay |
| `-raw` | Force uncompressed data for this campaign |

**Response:** `OK - ...` describing the captured format, size, and frames, or `Err - ...`.

**Effect:** Signs and broadcasts a snapshot in repeated rounds. Later local edits do
not change it. Region captures the working overlay; policy captures selected native settings.

**Restrictions:** Requires valid routing, identity, storage, and clock. Campaign
locks or pending recovery can refuse publication. Publishing does not require the
local receive subscription to be on.

## sync.region / sync.policy publish.reset

```text
sync.region publish.reset <region|*> <channel> [-empty] [-raw]
sync.policy publish.reset <region|*> <channel> [-raw]
```

**Parameters / response:** Same as the corresponding `publish` command.

**Effect:** Publishes generation-history recovery when a normal campaign is blocked
by incorrectly advanced history. This is an exceptional recovery operation.

## sync.region / sync.policy publish.abort

```text
sync.region publish.abort
sync.policy publish.abort
```

**Response:** `OK - aborting` for outbound work, `OK` for inbound cancellation,
or `Err - ...` such as `Err - no campaign`.

**Effect:** Stops local outbound work first; otherwise cancels incomplete reception.
An already-applied dataset remains in place. Replacing it requires a new campaign.

## sync.region / sync.policy publish.status

```text
sync.region publish.status
sync.policy publish.status
```

**Response format:** Four lines, or `fault storage`.

```text
sync:<on|off> step:<activity>
gen:<generation|-> tout:<minutes|-> lock:<minutes|->
regn:<scope|-> chnl:<channel|->
rpts:<count> ERR:<count>
```

Region status also includes `layr:<state>`. Activity includes `publishing`, `aborting`,
`receiving`, `staged`, `quiet`, `idle`, and policy-only `recovering`.
Publication includes `rnd:<done>/<total>`; reception includes `chnk:<received>/<total>`.
`rpts` counts reports; `ERR` counts current errors.

## sync.region / sync.policy publish.status.err

```text
sync.region publish.status.err [page]
sync.policy publish.status.err [page]
```

**Parameter:** One-based page; omission selects the first error.

**Response:** `<page>/<count> [<token>] <detail>`, `empty`, `fault storage`,
`Err - page`, or `Err - page range`.

**Scope:** Current faults, separate from completed-campaign reports.

## sync.region / sync.policy publish.report

```text
sync.region publish.report [page]
sync.policy publish.report [page]
```

**Parameter:** One-based page; omission selects the first report.

**Response format:** `page:<page>/<count> gen:<generation> result:<outcome>`;
abort warnings use `kind:abort` with deadline fields. Hop tallies may be appended.
No reports returns `empty`; invalid pages or storage return `Err - ...`.

**Scope:** Local outcomes and warnings, not acknowledgements from every receiver.

## sync.radio publish.arm / publish.go

```text
sync.radio publish.arm <region|*> <channel> <freq>,<bw>,<sf>,<cr> [@UTC]
sync.radio publish.go <region|*> <channel> <freq>,<bw>,<sf>,<cr> [@UTC]
```

| Parameter | Definition |
| --- | --- |
| `region` / `*` | Defined flood-enabled scope, or unscoped |
| `channel` | Target subscription channel |
| `freq`, `bw` | Frequency in MHz and bandwidth in kHz |
| `sf`, `cr` | Native spreading factor and coding rate; CR `0` preserves each node's coding rate |
| `@UTC` | Optional cutover time, `@YYYY-MM-DDTHH:MMZ`; otherwise launch time plus schedule duration |

| Action | Successful response | Effect |
| --- | --- | --- |
| `publish.arm` | `OK - armed <scope> <radio\|cr keep> in <minutes>m`, or `... T <epoch>` | Validate and retain a RAM-only arm |
| `publish.go` | `OK - migration <id> T <epoch>` | Launch the exact armed plan |

**Restrictions:** Arm expires after six hours or reboot. Go must match the armed
route, channel, radio values, timing mode, and schedule. Valid clock, identity, and
native radio state are required. Active campaign work can lock migration.

## sync.radio publish.abort

```text
sync.radio publish.abort
```

**Response:** `OK` or `Err - ...`, including no campaign or a commit that is too late to cancel.

**Effect:** Cancels local publication or received migration before durable commit,
returning to the original radio configuration. Does not undo a committed migration.

## sync.radio publish.status

```text
sync.radio publish.status
```

**Response format:**

```text
sync:<on|off> step:<phase and radio side>
cut:<epoch|->
chnl:<channel|->
```

Phases distinguish publisher/receiver, R1/R2, testing, cutover, confirmation, commit,
and fallback. A validated but unlaunched plan shows `publisher armed`.
Unavailable storage returns `fault storage`.

## sync.radio publish.report

```text
sync.radio publish.report [page]
```

**Parameter:** Page 1; omitted means the first page.

**Response:** `1/1 migration <id> <result>`, `empty`, or `Err - ...`.

| Result | Meaning |
| --- | --- |
| `committed` | New radio configuration accepted |
| `identical` | Target configuration was already in effect |
| `cr-only` | Coding-rate-only result |
| `fallback` | Returned to original configuration |
| `aborted` | Migration cancelled |
| `fault` | Migration failed |

## sync.time publish

```text
sync.time publish <region|*> <channel>
```

**Parameters:** Defined flood-enabled region or `*`, and target receive channel.

**Response:** `OK - every <interval> until <epoch>` or `Err - ...`.

**Effect:** Starts a persistent publication schedule using saved tolerance, interval,
and duration. Each publication sends one signed time sample.

**Restrictions:** Requires set tolerance, sane clock, valid schedule, identity, and
route. An active schedule must be aborted before replacing it. WAN/NTP refresh
requires hotspot-ota and a proven WiFi setup; see [time campaigns](time.md).

## sync.time publish.abort

```text
sync.time publish.abort
```

**Response:** `OK`, `Err - no schedule`, or `Err - storage`.

**Effect:** Stops this node's future samples. Already accepted receiver-clock
corrections remain in place. Does not change the receive subscription.

## sync.time publish.status

```text
sync.time publish.status
```

**Response format:**

```text
sync:<on|off> step:<activity>
next:<epoch|-> ends:<epoch|->
chnl:<channel|->
last:<result|->
```

Activity includes `idle`, `waiting-clock`, `verifying`, `sending`, and `retry`.
The received result can include an offset in seconds. Storage failure returns
`fault storage`. Time campaigns have no separate `publish.report` command.
