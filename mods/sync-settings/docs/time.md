# Time campaigns

Part of [sync-settings](../README.md). Trusted publishers, channels, campaign mechanics and
monitoring are covered there.

A repeater with no battery-backed clock can lose the date entirely, and one left running for
months can drift. A time campaign lets a trusted publisher repair clocks that are **materially
wrong**, in either direction.

- The publisher's clock is authoritative.
- A receiving repeater changes its clock only when it differs from the publisher by more than
  the publisher's **tolerance** (20 minutes unless changed).
- This is coarse date and time repair, not precision time: flood and relay delay of a few
  seconds is expected and ignored.

| Receiving repeater versus publisher | Result with the 20-minute tolerance |
|---|---|
| 19 minutes slow or fast | Unchanged |
| 21 minutes slow | Set forward |
| 21 minutes fast | Set backward |

Each sample is one signed frame. Unlike region and policy campaigns there is nothing to
reassemble, and nothing to roll back: an accepted correction stands.

## Publishing

**Syntax:**

```text
sync.time publish <region|*> <channel>
```

| Argument | Meaning |
|---|---|
| `<region>` | A defined, flood-enabled region: samples flood only within that region. |
| `*` | Unscoped: samples flood everywhere wildcard flooding is allowed. |
| `<channel>` | The subscription channel receiving repeaters must match. |

**Example**, unscoped on channel `release`, sampling weekly for six months:

```text
set sync.time.tolerance 20m
set sync.time.publish.interval 7d
set sync.time.publish.duration 180d
sync.time publish * release
```

- One sample goes out immediately, then one every interval until the duration ends.
- The schedule survives a reboot. A publisher that was off for several intervals sends one
  fresh sample, never a burst to catch up.
- Tolerance and interval are captured when you publish; changing them affects the next
  `sync.time publish`, not a running one.
- Publishing does not require `sync.time on`.
- `sync.time publish.abort` stops future samples on this repeater only. Nothing is sent to
  receiving repeaters, and corrections they already made stand.

**The publisher's own clock.** Every sample carries the publisher's clock, so that clock must
be right.

- A Heltec V4 publisher with a hotspot configured (see hotspot-ota's `ota wan verify`) checks
  its internet path and refreshes its clock by NTP before each sample. This happens only
  after `ota wan verify` has succeeded once with the saved WiFi settings.
- Without that, the publisher uses its clock as it stands. Set it first, for example with
  `time` or `ota wan verify`.
- If the publisher's clock is corrected while a schedule runs, the schedule moves with it.
- These boards have no battery-backed clock. After a power loss a publisher with the hotspot
  check refreshes its clock before resuming the schedule; one without it waits until its clock
  is set, for example with `time`.

### Commands

| Command | What it does |
|---|---|
| `get sync.time.tolerance` | Show the tolerance placed in outgoing samples. |
| `set sync.time.tolerance <N>m` | Save tolerance: 1–1440 minutes. |
| `get sync.time.publish.interval` | Show the sample interval. |
| `set sync.time.publish.interval <N>h\|<N>d` | Save interval: 1 hour to 30 days, never longer than the duration. |
| `get sync.time.publish.duration` | Show how long a schedule runs. |
| `set sync.time.publish.duration <N>d` | Save duration: 1–365 days. |
| `sync.time publish <region\|*> <channel>` | Start a schedule. |
| `sync.time publish.abort` | Stop this repeater's schedule. |

## Receiving

```text
sync.time on
```

Requires a channel. Every [trusted publisher](../README.md#trusted-publishers) gains the
authority to correct this repeater's clock while `sync.time on` is set.

- A receiving repeater does not need an accurate clock to start with. An unset clock, or one
  wrong by years, is simply corrected.
- Each sample is checked for channel, trusted publisher and signature, and must be newer than
  the last one accepted from that publisher. Older or repeated samples are ignored.
- Samples are ignored while a radio campaign is in progress.
- `sync.time off` stops accepting samples; it does not undo a correction.
- `sync.publisher forget` also removes that publisher's time replay protection.

### Commands

| Command | What it does |
|---|---|
| `sync.time on` | Accept time samples. Requires a channel. |
| `sync.time off` | Stop accepting samples. |

## Monitoring

| Command | What it does |
|---|---|
| `sync.time publish.status` | Show on/off, whether a schedule is active with its next and final sample times, and the last received sample. |

Status words while a schedule is active:

| Word | Meaning |
|---|---|
| `verifying` | The hotspot preflight is refreshing the publisher's clock. |
| `sending` | A sample has been handed to the radio. |
| `retry` | The last attempt did not complete; it retries after a minute. |

The last received sample appears as `last corrected <N>s` or `last within <N>s`, where `N` is
this repeater's clock minus the publisher's, or as `last replay` or `last storage`. Time has
no `publish.report`.
