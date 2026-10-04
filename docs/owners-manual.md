# Owner's Manual and Quick Reference

**For:** MeshCore operators who are new to MeshCore Enhanced+  
**Document date:** 4 October 2026

MeshCore Enhanced+ is MeshCore with added safety protections and features for remote and regionally
coordinated mesh nodes. It is built against new upstream MeshCore releases as they occur.
Independent **mods** add remote firmware updates, battery protection, reversible settings,
and signed settings distribution. The native radio, routing, identity, CLI, repeater, and
room-server behavior remain the foundation.

---

## Table of contents

1. [How the project is layered](#1-how-the-project-is-layered)
2. [`hotspot-ota`: remote updates and rollback](#2-hotspot-ota-remote-updates-and-rollback)
3. [`power-guard`: battery survival](#3-power-guard-battery-survival)
4. [`try-settings`: reversible configuration](#4-try-settings-reversible-configuration)
5. [`timing-safety`: clock and timer fixes](#5-timing-safety-clock-and-timer-fixes)
6. [`sync-settings`: fleet settings over the mesh](#6-sync-settings-fleet-settings-over-the-mesh)
   - [Publishers and subscribers](#publishers-and-subscribers)
   - [How a campaign works](#how-a-campaign-works)
   - [Region campaigns](#region-campaigns)
   - [Policy campaigns](#policy-campaigns)
   - [Radio campaigns](#radio-campaigns)
   - [Time campaigns](#time-campaigns)
   - [Status, reports, and aborts](#status-reports-and-aborts)
7. [`shim`: the integration layer](#7-shim-the-integration-layer)
8. [Browser tools and initial flashing](#8-browser-tools-and-initial-flashing)
9. [Complete command and syntax reference](#9-complete-command-and-syntax-reference)

---

## 1. How the project is layered

Each build begins with a clean copy of the selected upstream MeshCore release. The project
then copies in mod-owned source, generates the connections between those mods and MeshCore,
and applies two small maintained patches where upstream code itself must change.

| Layer | Responsibility |
|---|---|
| **Upstream MeshCore** | Radio, packet routing, identity, native settings, CLI, repeater and room-server applications |
| **Shim** | Shared hook points and generated dispatch between MeshCore and installed mods |
| **Feature mods** | OTA, power management, reversible settings, synchronization, and timing fixes |
| **Board configuration** | Hardware facts such as GPIOs, battery thresholds, partitions, and supported capabilities |
| **Build and release checks** | Composition tests, strict patch application, compatibility builds, identity stamping, and emulated boot |

The repository checks run 191 automated tests at this revision. Separately, each release
target must pass patch application, compilation, partition-size and metadata checks. Where
emulation is configured, the exact app image slated for release must boot and answer its CLI.

### Installed mods

| Mod | What it adds | Where it ships |
|---|---|---|
| [hotspot‑ota](#2-hotspot-ota-remote-updates-and-rollback) | Remote Wi-Fi/cellular firmware updates, OTA slot tools, WAN/NTP verification, and rollback protection | All targets |
| [power‑guard](#3-power-guard-battery-survival) | Battery-aware power saving, brownout avoidance, deep-sleep rail control, and bounded power-off | Heltec V4 repeater |
| [try‑settings](#4-try-settings-reversible-configuration) | Timed trials for settings that may strand a remote node | All targets |
| [timing‑safety](#5-timing-safety-clock-and-timer-fixes) | Long-uptime timer correction and safe elapsed-time reporting after reboot | All targets |
| [sync‑settings](#6-sync-settings-fleet-settings-over-the-mesh) | Signed region, policy, radio, and time campaigns | All targets |
| [`shim`](#7-shim-the-integration-layer) | Generated integration and shared safety interlocks | All targets |

The [complete command reference](#9-complete-command-and-syntax-reference) is at the end of
this manual. The command tables inside each feature section focus on common tasks and
examples.

---

## 2. [<u>hotspot-ota</u>↗](../mods/hotspot-ota/README.md): remote updates and rollback

`hotspot-ota` lets a repeater or room server update itself without a person at the site. It
can power an attached cellular hotspot, join configured Wi-Fi, verify WAN access, download
the correct firmware, write the inactive OTA slot, and reboot. It also hardens MeshCore's
local `start ota` upload mode and protects a newly installed image with a 90-second probation
period.

### Common tasks

| Scenario | Commands | Result |
|---|---|---|
| Configure Wi-Fi | `set ota.wan.wifi <ssid>,<password>` | Saves credentials independently of normal node preferences |
| Find nearby Wi-Fi | `ota wan survey` | Scans once; use the suggested `next` offset to page through results |
| Prove WAN and NTP | `ota wan verify` then `get ota.wan.health` | Tests the path and restores the Wi-Fi/GPIO state it found |
| Save the normal firmware URL | `set ota.fw.url <url>` | Keeps future remote update commands short |
| Start a remote update | `start ota wan update` | Queues the saved URL for background download and installation |
| Watch progress | `get ota.status` | Shows join, check, download, verify, completion, or failure |
| Cancel safely | `ota cancel` | Stops work before verification or partition commit begins |
| Inspect fallback | `get ota.slot` | Shows both OTA slots and whether the inactive image is actually valid |
| Use local upload mode | `start ota`, `get ota.ap`, `stop ota` | Starts, inspects, or ends the local Wi-Fi upload page |

The [full OTA and WAN syntax](#9-complete-command-and-syntax-reference) is included in the
final command table.

### Example: configure Wi-Fi

If the network name is unknown, run `ota wan survey`; follow a `next N` result with
`ota wan survey N`, or use `ota wan survey refresh` to scan again.

One-time setup:

```text
set ota.wan.wifi MyHotspot,my-password
ota wan verify
```

`ota wan verify` is non-blocking. Watch it finish:

```text
get ota.status
get ota.wan.health
```

A successful health record resembles:

```text
latest=WAN_1|NTP_1 proven=yes
```

### Example: start a remote firmware update

The MobMesh web flasher automatically saves the correct firmware URL for supported repeater
images. After a manual flash, set it once before using the short update command:

```text
set ota.fw.url <url>
```

Start the update:

```text
start ota wan update
get ota.status
```

After reboot, inspect both slots:

```text
get ota.slot
```

The active image reports `pending` during probation and later `valid`. Do not start another
update while the new image is still pending.

### Example: diagnose the WAN path without updating

```text
ota wan join
ota wan check
ota wan leave
get ota.wan.radio
```

The final command shows whether the Wi-Fi driver, link, and hotspot power are off after leaving.

Or use the combined verification routine:

```text
ota wan verify
```

Use `ota wan verify` when the goal is an unattended health check that also attempts NTP and
proves the original Wi-Fi and hotspot-power states were restored.

### What protects the node

- The image identity is checked early, before paying to download the complete image.
- An image for another board or role is refused.
- An image without `hotspot-ota` is refused because installing it would remove remote WAN
  updating.
- The complete image's appended SHA-256 must match; this check has no override.
- Destructive commands are refused while flash is being written.
- A new image remains on probation until the radio and firmware have stayed healthy.
- A probation failure returns to the previous slot automatically.
- The hotspot rail is forced off during startup so a reset cannot leave the modem powered.

`set ota.fw.marker off` is a one-attempt recovery override for installing an unstamped image.
It never bypasses SHA-256, and it may deliberately remove the ability to update remotely.

### Hardware note

The Heltec V4 hotspot-power output is validated on GPIO47. The Xiao ESP32-C3 can use Wi-Fi
already in range, but its external hotspot-power GPIO remains unverified. Do not connect or
rely on that rail-control output until its board configuration has been measured and updated.

---

## 3. [<u>power-guard</u>↗](../mods/power-guard/README.md): battery survival

`power-guard` keeps a weak battery from becoming an unrecoverable boot loop. It can engage
normal power saving as voltage falls, leave service before the pack reaches the measured
boot floor, deep-sleep with the radio rails off, and wake periodically until the battery has
recovered. It currently ships only on the Heltec V4 repeater because that is the target whose
battery divider and thresholds have been measured.

Automatic power saving and safe sleep have separate controls: `powersaving auto` manages
normal savings as voltage falls and rises; `powersaving safe` controls when the repeater
leaves service to protect the battery.

### Common tasks

| Scenario | Commands | Result |
|---|---|---|
| Inspect automatic power saving | `powersaving auto` | Shows whether automatic saving is enabled and active |
| Enable automatic saving | `powersaving auto on` | Lets the measured battery ladder control normal power saving |
| Inspect brownout protection | `powersaving safe` | Shows the failsafe state, threshold, and latest reading |
| Enable the failsafe | `powersaving safe on` | Allows the node to leave service before a boot loop |
| Set the safe threshold | `powersaving safe.mv <mv>` | Saves a gauge-millivolt threshold; `0` disables it |
| Sleep temporarily | `poweroff <seconds>` | Sleeps for 60–86400 seconds and then wakes |
| Read remote diagnostics | `stats-core`, `stats-radio`, `stats-packets` | Makes native statistics available through authenticated remote CLI |

See the [full power command syntax](#9-complete-command-and-syntax-reference) at the end.

### What happens during a low-battery event

1. Several agreeing measurements are required; one transmit dip does not trigger a state
   change.
2. Normal power saving engages at the board's automatic threshold.
3. At the safe threshold, the node leaves service and deep-sleeps.
4. Each wake measures the battery before starting the LoRa radio.
5. If the pack is still weak, the node sleeps again on a widening schedule.
6. At the measured resume threshold, normal operation returns.

The Heltec V4 build also turns off and latches the LoRa front-end rail during deep sleep.
Measured sleep current is about 14 µA rather than roughly 4 mA with that rail left on.

### Safety boundaries

- Voltage values are board facts, not portable defaults.
- An unmeasured board gets zero thresholds, which disables automatic action.
- `poweroff` always requires a wake duration. Builds without power-guard refuse upstream's
  permanent form.
- While the battery is below the hotspot resume level, OTA joins, verification, and updates
  are refused so the modem load cannot cause the brownout being avoided.

---

## 4. [<u>try-settings</u>↗](../mods/try-settings/README.md): reversible configuration

`try-settings` is for changes that may sever remote contact or need to be judged at the
installation site. It snapshots the current value, applies a replacement for a bounded time,
and restores the old value unless the operator confirms the trial while the node is still
reachable on the new setting.

### Common tasks

| Scenario | Commands | Result |
|---|---|---|
| Try a setting for five minutes | `tryset 300 tx 14` | Applies the value and starts a revert timer |
| Inspect the trial | `tryset` | Shows the key, trial state, and time remaining |
| Keep the value | `tryset keep` | Persists the active trial |
| Revert now | `tryset revert` | Restores the saved value immediately |
| Try radio parameters | `tryset 300 radio 910.525,62.5,7,6` | Uses MeshCore's native temporary-radio mechanism |

The [complete `tryset` syntax and key list](#9-complete-command-and-syntax-reference) appear
in the final reference.

### Example workflow

```text
tryset 300 flood.max 4
tryset
```

If the node still answers from the far side:

```text
tryset keep
```

If the change behaves badly:

```text
tryset revert
```

Doing nothing is also safe: the timer restores the previous value.

### Why `keep` has a deadline

`tryset keep` is accepted only while the trial is active. Reaching the node on the trial
setting is evidence that the setting did not strand it. A confirmation after automatic
reversion would prove nothing.

The rollback record is written before the setting changes. A reboot ends and reverts a trial
rather than attempting to resume a countdown whose context may no longer be trustworthy.

---

## 5. [<u>timing-safety</u>↗](../mods/timing-safety/README.md): clock and timer fixes

`timing-safety` has no operator commands. It carries two small upstream corrections that are
important on unattended hardware: deadline comparisons continue working when the 32-bit
millisecond counter wraps after about 49 days, and elapsed “last heard” output no longer
turns into years of nonsense when a reboot resets the wall clock.

The mod works automatically whenever the firmware runs. NTP from `hotspot-ota` and signed
time campaigns from `sync-settings` provide the actual clock-setting mechanisms described in
their own sections.

---

## 6. [<u>sync-settings</u>↗](../mods/sync-settings/README.md): fleet settings over the mesh

`sync-settings` distributes selected configuration from trusted publishers to participating
repeaters and room servers. It uses ordinary MeshCore flood routing, so intermediate relays
can run stock firmware. The mod supports four campaign types: region, policy, radio, and
time.

| Campaign | What a subscriber receives |
|---|---|
| [Region](#region-campaigns) | A managed region overlay; the native region list stays intact. |
| [Policy](#policy-campaigns) | Selected operating settings for its area. |
| [Radio](#radio-campaigns) | A coordinated move to new LoRa settings, with fallback if confirmation fails. |
| [Time](#time-campaigns) | Signed clock samples that correct a materially wrong clock. |

### Publishers and subscribers

A **publisher** captures or defines an update, signs it with the node's MeshCore identity,
and transmits it in repeated rounds. Publishing does not require the publisher to subscribe
to that campaign type.

A **subscriber**—also called a receiver—accepts a campaign only when:

1. that campaign type is on;
2. the campaign's channel matches the subscriber's configured channel; and
3. the signing public key is in the subscriber's trusted publisher list.

Receiver setup:

```text
sync.publisher add <full-public-key>
set sync.channel release
sync.region on
sync.policy on
sync.radio on
sync.time on
```

Enable only the campaign types wanted. The full public key can be read on the publisher with
the native `get public.key` command or copied through the browser configurator.

| Concept | Purpose |
|---|---|
| Trusted publisher key | Defines who has authority to change enabled sync types |
| Channel | Selects a stream such as `release` or `early`; it is not a password |
| Per-publisher generation | Rejects old campaigns without forcing independent publishers to coordinate |
| Campaign lock | Keeps an incomplete dataset bound to one publisher and one signed update |

Trust is shared across region, policy, radio, and time. Removing a publisher revokes future
authority but retains replay history. `forget` is a separate action that erases that history.
The [full trust and channel command reference](#9-complete-command-and-syntax-reference) is
at the end.

### How a campaign works

Region, policy, and radio publication use repeated rounds. The dataset or migration plan is
captured once; every round sends the same signed campaign.
Policy and radio campaigns, and small region overlays, need two LoRa messages per round:
an announcement and one data chunk. Time uses a single signed message per sample.

| Step | Publisher | Subscriber |
|---:|---|---|
| 1 | Captures data and signs an announcement | Checks type, channel, publisher, generation, scope, and signature |
| 2 | Sends signed chunks | Retains valid chunks for the locked campaign |
| 3 | Repeats the same round later | Fills any gaps from later rounds |
| 4 | Finishes the bounded schedule | Verifies the complete digest, saves safely, then activates |

Larger region overlays use more chunks; later rounds can fill gaps left by lost frames.

Security and resilience are layered:

- announcements, chunks, confirmations, aborts, and time samples are signed;
- replay history is stored per publisher and dataset;
- complete datasets carry a SHA-256 digest;
- storage records are versioned, paired, and validated before use;
- decompression is bounded and happens only after frame authentication;
- incomplete data never replaces a working region overlay; and
- standard MeshCore relays can forward campaigns without acting on them.

#### What these protections mean in practice

| Scenario | Protection and result |
|---|---|
| Someone replays an old valid campaign | Stored generation history rejects it. |
| Someone creates a campaign with an unapproved key | The subscriber ignores untrusted publishers. |
| Someone pretends to be an approved publisher | The signature fails without the publisher's private key. |
| A valid publisher uses a different channel | The subscriber rejects the channel mismatch. |
| A packet changes or is damaged in transit | Signature and integrity checks discard it. |
| Chunks from different campaigns become mixed | The campaign lock rejects unrelated chunks. |
| One or more chunks are lost | Later rounds fill gaps; incomplete data is never applied. |
| A complete transfer contains the wrong bytes | The complete SHA-256 digest must match. |
| Compressed input is malformed or expands too far | Bounded decompression rejects it safely. |
| A forged abort or radio confirmation is transmitted | Signed control messages reject it. |
| Power fails while persistent state is being updated | Startup selects a complete, validated storage record. |
| A standard MeshCore repeater forwards campaign traffic | It relays packets without gaining authority or applying them. |

These protections establish authenticity and safe application, not secrecy. Campaign traffic
can cross a public mesh, and the channel name is visible. A trusted publisher is deliberately
being granted authority over every enabled campaign type, so its private key must be protected.

Publication means the publisher started the campaign. It does not prove that every possible
subscriber received it. Status and reports describe the local node's view only.

### [<u>Region campaigns</u>↗](../mods/sync-settings/docs/region.md)

#### What region sync does

Region campaigns carry a separate 32-entry region **overlay**. The native MeshCore list
remains untouched and keeps its own 32 entries.

| Native region list | Synchronized overlay |
|---|---|
| Managed with native `region` commands | Managed with `sync.region` commands |
| Locally controlled | Publisher-managed while sync is enabled |
| Never overwritten by sync | Atomically replaced by an accepted campaign |
| Up to 32 entries | Up to 32 additional entries |

Overlay matches take priority, while an exact flood deny in either table wins. Turning
`sync.region off` immediately removes the overlay from routing but does not erase it.

#### Subscriber example

```text
sync.publisher add <full-public-key>
set sync.channel release
sync.region on
sync.region publish.status
```

After a campaign arrives:

```text
sync.region
sync.region publish.report 1
```

#### Publisher example

Build and save the publishing overlay:

```text
sync.region def us us-al|us us-fl
sync.region save
```

Publish unscoped on channel `release`:

```text
sync.region publish * release
sync.region publish.status
```

A named scope can be used instead of `*` when the publisher has a matching flood-enabled
native or overlay region:

```text
sync.region publish us-al release
```

Region payloads compress automatically when doing so saves at least one whole LoRa frame.
`-raw` forces one campaign to remain uncompressed for diagnosis.

Publishing an intentionally empty overlay requires explicit confirmation:

```text
sync.region publish * release -empty
```

Without `-empty`, a cleared working list is refused rather than becoming a mesh-wide erase.

#### Local overlay management

```text
sync.region put us
sync.region put us-al us
sync.region denyf us-al
sync.region save
```

Changes are live in RAM immediately and persistent only after `save`. `reload` discards
unsaved edits. A future accepted campaign replaces local overlay edits while sync remains on.

See the [complete region command reference](#9-complete-command-and-syntax-reference) for
listing, hierarchy definition, flood flags, removal, clear, reload, publication, schedules,
status, reports, and generation recovery.

### [<u>Policy campaigns</u>↗](../mods/sync-settings/docs/policy.md)

#### What policy sync does

Policy campaigns carry selected native operating settings that a mesh may want consistent:

```text
flood.max              flood.max.unscoped     flood.max.advert
advert.interval        flood.advert.interval  path.hash.mode
loop.detect            multi.acks             af
txdelay                agc.reset.interval     repeat.gate
```

Credentials and radio settings are not included. Once a policy is applied, its native
settings remain even if `sync.policy` is later turned off.

`repeat.gate` is the one added policy setting. Upstream room servers normally forward floods
without consulting regions. With `repeat.gate on`, a room server uses its region view as a
forwarding gate. It remains off by default to preserve upstream behavior.

#### Subscriber example

```text
sync.publisher add <full-public-key>
set sync.channel release
sync.policy on
sync.policy publish.status
```

After application:

```text
sync.policy publish.report 1
```

#### Publisher example

Set and verify the desired native values on the publisher, then capture them:

```text
sync.policy publish * release
sync.policy publish.status
```

The default schedule sends one round every 12 hours for 3 days. Region and policy schedules
are independent and configurable for the next campaign:

```text
set sync.policy.publish.interval 12h
set sync.policy.publish.duration 3d
```

An interrupted multi-setting apply is recovered at boot so a receiver is not left with an
unexplained mixture. See the [full policy syntax](#9-complete-command-and-syntax-reference)
for schedules, raw transmission, reports, abort, and exceptional generation reset.

### [<u>Radio campaigns</u>↗](../mods/sync-settings/docs/radio.md)

#### What radio sync does

A radio campaign coordinates movement from current settings **R1** to new settings **R2**.
A frequency, bandwidth, or spreading-factor change separates R2 nodes from R1 immediately,
so the migration is announced repeatedly, can include temporary rehearsals, and is not saved
until the publisher proves the new path by transmitting a matching signed confirmation on R2.

```text
R1 plans -> optional R2 test windows -> common cutover to temporary R2
         -> signed R2 confirmations -> save R2 or fall back to R1
```

Receivers do not need an accurate wall clock. The signed publisher time establishes the
schedule and each receiver advances it with a monotonic timer. Later campaign copies can
improve that estimate only by moving the publisher's apparent time forward; a slower route
cannot postpone cutover.

#### Subscriber example

```text
sync.publisher add <full-public-key>
set sync.channel release
sync.radio on
sync.radio publish.status
```

A receiver stages the complete plan while remaining on R1. At cutover it moves temporarily
to R2. If no matching confirmation arrives during the confirmation window, it returns to R1
without saving R2.

#### Publisher example

Review the six-value schedule:

```text
get sync.radio.schedule
```

Default:

```text
10m,0m,0m,360m,5m,45m
```

The fields are campaign interval, test interval, test window, campaign duration,
confirmation interval, and confirmation window.

Arm the exact migration:

```text
sync.radio publish.arm * release 915,125,8,0
```

Launch by repeating it exactly:

```text
sync.radio publish.go * release 915,125,8,0
```

Coding rate `0` preserves each receiver's current CR. The arm is RAM-only and expires after
six hours. Frequency, bandwidth, and spreading-factor bounds match native MeshCore.

Optional absolute UTC cutover:

```text
sync.radio publish.arm * release 915,125,8,0 @2026-10-01T03:00Z
sync.radio publish.go  * release 915,125,8,0 @2026-10-01T03:00Z
```

Radio owns the campaign lock while active. Region and policy campaigns cannot start or be
accepted, and time samples wait, because moving the transport underneath another update
would strand it.

Cancellation is signed and repeated. Before cutover it is sent on R1; during confirmation it
is sent on R2. Once durable saving begins, cancellation is too late. The [full radio command
reference](#9-complete-command-and-syntax-reference) includes schedule, status, reports, and
abort.

### [<u>Time campaigns</u>↗](../mods/sync-settings/docs/time.md)

#### What time sync does

Time campaigns repair clocks that are materially wrong. They are intentionally coarse: a
few seconds of LoRa flood delay are acceptable, while a clock wrong by tens of minutes,
months, or years is not.

Each sample is one signed frame containing the publisher's time and **action tolerance**.
The receiver changes its clock only when its difference exceeds that publisher-selected
tolerance in either direction.

| Receiver difference with a 20-minute tolerance | Action |
|---|---|
| 19 minutes slow or fast | Leave the clock unchanged |
| 21 minutes slow | Set it forward |
| 21 minutes fast | Set it backward |

There are no chunks and no time payload to store or roll back. Replay protection is based on
the publisher's campaign generation, not on whether the published epoch is numerically larger.

#### Subscriber example

```text
sync.publisher add <full-public-key>
set sync.channel release
sync.time on
sync.time publish.status
```

Every trusted publisher is authorized to correct the clock while time sync is on.

#### Publisher example

Create a weekly heartbeat lasting six months:

```text
set sync.time.tolerance 20m
set sync.time.publish.interval 7d
set sync.time.publish.duration 180d
sync.time publish * release
```

One fresh sample is sent immediately and then at each interval. The schedule survives reboot;
missed intervals collapse into one fresh sample rather than a burst.

If `ota wan verify` has previously proven a hotspot path, set an independent NTP refresh
cadence:

```text
set sync.time.ntp.interval 7d
```

The publisher refreshes its own clock before the next qualifying sample, then signs the time
it trusts. Without a proven hotspot it uses the local clock, and after a reset it waits until
that clock is believable.

See the [full time command reference](#9-complete-command-and-syntax-reference) for tolerance,
sample duration, NTP cadence, status, and local schedule abort.

### Status, reports, and aborts

| Campaign | Current state | Last outcome | Abort behavior |
|---|---|---|---|
| Region | `sync.region publish.status` | `sync.region publish.report 1` | Stops outbound work or incomplete reception; never rolls back an applied overlay |
| Policy | `sync.policy publish.status` | `sync.policy publish.report 1` | Stops outbound work or incomplete reception; never rolls back applied policy |
| Radio | `sync.radio publish.status` | `sync.radio publish.report 1` | Cancels a pending migration before durable commit |
| Time | `sync.time publish.status` | Included in status | Stops future samples on this publisher only |

Status shows this node's current work. For example, a region publisher that has finished
one of seven rounds may report:

```text
sync.region publish.status
  -> sync:on layr:on step:publishing rnd:1/7
gen:42 tout:- lock:-
regn:* chnl:release
rpts:0 ERR:0
```

A policy subscriber receiving one of three chunks may report:

```text
sync.policy publish.status
  -> sync:on step:receiving chnk:1/3
gen:42 tout:11m lock:-
regn:* chnl:release
rpts:0 ERR:0
```

`gen` is the campaign generation, `tout` the receiving timeout, `lock` the publication
guard, and `rpts` the number of available reports or warnings. `ERR` counts current errors.
Unlike status, a report records how the latest campaign ended:

```text
sync.region publish.report 1
  -> 1/1 gen 42 applied; replay settled hops 0x1,1x3
```

The hop tally `0x1,1x3` means one frame arrived directly and three arrived through one
relay. It describes this receiver's successful path, not a mesh census.

For region and policy, abort sends two signed notices. A subscriber that already applied the
campaign keeps it. A public flood cannot guarantee that every receiver heard an undo request,
so abort means **stop unfinished work**, not **reverse history**.

---

## 7. [<u>shim</u>↗](../mods/shim/README.md): the integration layer

`shim` is the shared boundary between upstream MeshCore and the feature mods. It provides
generated startup, loop, CLI, packet, region, radio, and safety hook dispatch so each feature
does not maintain its own patch against upstream. Its ordering also lets safety checks act
before a lower-priority command handler—for example, refusing `poweroff` while an OTA write
is active.

Shim has no user configuration. With no feature mod claiming a command, normal MeshCore
dispatch continues. Build-time validation rejects incompatible hook ownership instead of
allowing an invalid mod combination to reach a device.

---

## 8. Browser tools and initial flashing

The browser tools at `https://tools.mobmesh.org/` flash both Enhanced+ and stock MeshCore.
Connect the device with a data-capable USB cable, select the board and role, and follow the
prompts in Chrome, Edge, or another Chromium-based browser.

The flasher can:

- enter flash mode automatically on supported boards;
- preserve identity and settings during an upgrade;
- rebuild SPIFFS safely when the partition layout changes;
- apply the selected radio and regional setup after flashing; and
- choose native or synchronized region commands according to the feature bits in the image
  actually flashed.

After first boot, confirm normal MeshCore radio and routing behavior before enabling sync or
attempting a remote WAN update.

---

## 9. Complete command and syntax reference

Parameters in angle brackets are required. Values in square brackets are optional.
Alternatives separated by `|` mean choose one value; do not type the brackets themselves.

### MeshCore commands

These are native MeshCore commands whose behavior or availability is extended by this
project.

| Command / syntax | Purpose and important limits |
|---|---|
| `ver` | Show running firmware version information. |
| `get public.key` | Show the public key subscribers must authorize. Never distribute the private key. |
| `start ota` | Start the native local upload AP through the hardened handler. |
| `poweroff <seconds>` / `shutdown <seconds>` | Deep sleep for 60–86400 seconds, then wake. |
| `stats-core` | Show native core and battery statistics; also allowed over authenticated remote CLI. |
| `stats-radio` | Show native radio statistics; also allowed remotely. |
| `stats-packets` | Show native packet counters; also allowed remotely. |

### hotspot-ota commands

| Command / syntax | Purpose and important limits |
|---|---|
| `set ota.wan.wifi <ssid>,<password>` | Save WAN Wi-Fi credentials; changing them clears proven WAN health. |
| `get ota.fw.url` | Show the saved default firmware URL. |
| `set ota.fw.url <url>` | Save the URL used by `start ota wan update`. |
| `start ota wan <url>` | Queue a WAN firmware update from the supplied URL. |
| `start ota wan update` | Queue an update using the saved URL. |
| `get ota.status` | Show background OTA or WAN-verification progress and result. |
| `ota cancel` | Cancel before verification or commit begins. |
| `set ota.fw.marker <on\|off>` | One-attempt, RAM-only identity-check override; SHA-256 remains mandatory. |
| `get ota.slot` | Inspect active/inactive slots, probation, recorded state, and inactive image validity. |
| `ota slot boot <A\|B>` | Reboot immediately into a verified image in the selected slot. |
| `get ota.ap` | Show local upload AP state and remaining lifetime. |
| `stop ota` | End local upload AP mode; refused during an active write. |
| `ota wan join` | Join configured Wi-Fi without downloading. |
| `ota wan check` | Check WAN reachability while joined. |
| `ota wan survey` | Scan nearby Wi-Fi networks once; show names and whether each is open. |
| `ota wan survey <offset>` | Show the saved results starting at the suggested `next` offset. |
| `ota wan survey refresh` | Discard saved results and start a fresh scan. |
| `ota wan verify` | Queue WAN and NTP checks, then restore initial Wi-Fi/GPIO state. |
| `ota wan leave` | Disconnect Wi-Fi and drop hotspot power. |
| `get ota.wan.health` | Show latest `WAN_n\|NTP_n`, restore state, and durable `proven` flag. |
| `get ota.wan.radio` | Show ESP32 Wi-Fi, driver, link, and hotspot-power states. |
| `get ota.wan.pwr` | Show hotspot power state; external rail control is currently validated only on Heltec V4. |
| `set ota.wan.pwr <on\|off>` | Drive hotspot power directly; refused during active OTA service. Do not use an unverified board pin. |

### power-guard commands

These commands are available on the Heltec V4 repeater.

| Command / syntax | Purpose and important limits |
|---|---|
| `powersaving auto` | Show automatic power-saving state and transitions. |
| `powersaving auto <on\|off>` | Enable or disable automatic power saving. |
| `powersaving safe` | Show brownout failsafe state, threshold, and reading. |
| `powersaving safe <on\|off>` | Enable or disable the failsafe without changing its threshold. |
| `powersaving safe.mv <mv>` | Set safe threshold in gauge millivolts; `0` disables it. |

### try-settings commands

| Command / syntax | Purpose and important limits |
|---|---|
| `tryset <seconds> <key> <value>` | Trial an allowlisted setting for 60–86400 seconds. |
| `tryset <seconds> radio <freq>,<bw>,<sf>,<cr>` | Trial radio parameters through native `tempradio`. |
| `tryset` | Show the running trial and time remaining. |
| `tryset keep` | Persist the active trial; refused after it expires. |
| `tryset revert` | Restore the previous value immediately. |

### timing-safety

`timing-safety` has no operator commands.

### sync-settings commands

#### Shared configuration and publisher trust

| Command / syntax | Purpose and important limits |
|---|---|
| `get sync.channel` | Show the subscriber's receive channel. |
| `set sync.channel <channel>` | Save 1–16 letters, digits, `-`, or `_`; all sync types must be off. |
| `sync.publisher list [offset]` | List complete trusted/retained keys; zero-based offset, up to two per reply. |
| `sync.publisher add <full-public-key>` | Authorize a 64-character hexadecimal public key for all enabled campaign types. |
| `sync.publisher remove <full-public-key>` | Revoke trust and cancel its inbound work; retain replay history. |
| `sync.publisher forget <full-public-key>` | After removal, erase the record and replay history. |

#### Region campaigns and overlay

| Command / syntax | Purpose and important limits |
|---|---|
| `sync.region [offset]` | Show the RAM overlay as an indented tree; use the final `next` offset for another page. |
| `sync.region put <name> [<parent>]` | Add or update a flood-enabled entry; parent must already exist. |
| `sync.region def <token> [<token> ...]` | Load compact MeshCore-style hierarchy notation into RAM. |
| `sync.region allowf <name>` | Flood-enable an existing overlay entry. |
| `sync.region denyf <name>` | Flood-deny an existing overlay entry. |
| `sync.region remove <name>` | Remove an entry; children must be removed first. |
| `sync.region save` | Persist RAM edits; incoming campaigns may later replace them. |
| `sync.region clear` | Empty RAM only; follow with `save` to persist. |
| `sync.region reload` | Discard RAM edits and reload the saved overlay. |
| `sync.region <on\|off>` | Enable or disable campaign acceptance and overlay participation. A subscriber needs a channel to enable it. |
| `sync.region publish <region\|*> <channel> [-empty] [-raw]` | Publish the captured overlay; `-empty` confirms a clear and `-raw` disables compression. |
| `sync.region publish.reset <region\|*> <channel> [-empty] [-raw]` | Publish exceptional generation-history recovery. |

#### Policy campaigns

| Command / syntax | Purpose and important limits |
|---|---|
| `sync.policy <on\|off>` | Enable or disable future policy acceptance; applied settings remain. |
| `get repeat.gate` | Show whether room-server repeats use region eligibility. |
| `set repeat.gate <on\|off>` | Gate room-server forwarding by regions; off preserves upstream behavior. |
| `sync.policy publish <region\|*> <channel> [-raw]` | Publish the current selected native policy values. |
| `sync.policy publish.reset <region\|*> <channel> [-raw]` | Publish exceptional generation-history recovery. |

#### Shared region and policy campaign controls

| Command / syntax | Purpose and important limits |
|---|---|
| `get sync.<region\|policy>.publish.interval` | Show the publication round interval. |
| `set sync.<region\|policy>.publish.interval <N>h` | Save a 3–24 hour interval for the next campaign. |
| `get sync.<region\|policy>.publish.duration` | Show publication duration. |
| `set sync.<region\|policy>.publish.duration <N>d` | Save a 1–4 day duration for the next campaign. |
| `sync.<region\|policy> publish.abort` | Abort outbound work or cancel inbound reception; no rollback. |
| `sync.<region\|policy> publish.status` | Show local publishing, receiving, quiet, recovery, or idle state. |
| `sync.<region\|policy> publish.report [page]` | Show local outcome and warnings; pages start at 1. |

#### Radio campaigns

| Command / syntax | Purpose and important limits |
|---|---|
| `sync.radio <on\|off>` | Enable or disable radio plans; enable requires a channel and trusted publisher. |
| `get sync.radio.schedule` | Show six radio campaign timing values. |
| `set sync.radio.schedule <campaign>,<test-interval>,<test-window>,<duration>,<confirm-interval>,<confirm-window>` | Save the six minute-valued fields. |
| `sync.radio publish.arm <region\|*> <channel> <freq>,<bw>,<sf>,<cr> [@UTC]` | Validate and preview; the RAM-only arm expires after six hours. |
| `sync.radio publish.go <region\|*> <channel> <freq>,<bw>,<sf>,<cr> [@UTC]` | Launch only when it exactly matches the arm. `cr=0` preserves local CR. |
| `sync.radio publish.abort` | Cancel a pending publish or received migration before durable commit. |
| `sync.radio publish.status` | Show phase, radio side, channel, and cutover. |
| `sync.radio publish.report [page]` | Show `committed`, `identical`, `cr-only`, `fallback`, `aborted`, or `fault`. |

#### Time campaigns

| Command / syntax | Purpose and important limits |
|---|---|
| `sync.time <on\|off>` | Enable or disable trusted time samples. A subscriber needs a channel to enable it. |
| `get sync.time.tolerance` | Show the action tolerance placed in samples. |
| `set sync.time.tolerance <N>m` | Save the required tolerance, 1–1440 minutes. |
| `get sync.time.publish.interval` | Show the sample interval. |
| `set sync.time.publish.interval <N>h\|<N>d` | Save 1 hour–30 days; it must not exceed duration. |
| `get sync.time.publish.duration` | Show schedule duration. |
| `set sync.time.publish.duration <N>d` | Save 1–365 days. |
| `get sync.time.ntp.interval` | Show the publisher's NTP refresh cadence. |
| `set sync.time.ntp.interval <N>h\|<N>d\|0` | Save 1 hour–365 days, or `0` for every sample. |
| `sync.time publish <region\|*> <channel>` | Start a persistent signed time-sample schedule. |
| `sync.time publish.abort` | Stop future samples locally; previously accepted corrections stand. |
| `sync.time publish.status` | Show on/off, active schedule, next/final sample, and latest received result. |
