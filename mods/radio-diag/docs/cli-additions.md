# radio-diag CLI reference

[↩ Back to radio-diag readme](../README.md)

Commands added by `radio-diag`, available over serial and authenticated remote CLI.

`<value>` denotes a required parameter. `[value]` is optional.

## Command index

| Command | Purpose |
| --- | --- |
| [`diag`](#diag) | One-screen summary: every area, the worst first, one best fix |
| [`diag lna`](#diag-lna) | Study progress, or the last result |
| [`diag lna start [hours]`](#diag-lna-start) | Start an LNA study |
| [`diag lna stop`](#diag-lna-stop) | End the study early |
| [`diag links`](#diag-links) | How solid each neighbour link is |
| [`diag noise`](#diag-noise) | The mesh channel's noise over the last 24 h, in 4-hour blocks |
| [`diag rfscan`](#diag-rfscan) | Check the mesh frequency +/-4 MHz for nearby interference |
| [`get diag.tz`](#diagtz) | Local time offset used in reports |
| [`set diag.tz <offset>`](#diagtz) | Set the local time offset |

## diag

```text
diag
```

**Response:** a header set by the worst area, one line per area with 🟢 🟡 🔴 or ⚪ (not enough data yet), then
the single best fix and the command with details. Arrives as several replies.

| Area | Source | ⚪ until |
| --- | --- | --- |
| 🔗 Links | `diag links` table | A neighbour has 5 packets |
| 📶 LNA | The running or last `diag lna` study; boards with a switchable LNA only | A study has run |
| 📡 Nearby | Last `diag rfscan`, one run when none is cached or it is 24 h old; SX126x only. 🔴 energy on the channel, 🟡 a steady signal 250 kHz beside it, 🟢 anything further out (noted, the receiver rejects it) | The scan succeeds |
| 🔊 Noise | `diag noise` verdict | 6 hours watched with the clock set |
| 📉 Decodes | Upstream packet counters over the last 24 h (since boot until the noise watch has an hour): 2% failed decodes 🟡, 10% 🔴 | 50 packets |
| 📊 Load | Upstream airtime over uptime: 25% 🟡, 50% 🔴 | 10 minutes up |
| 🧭 Paths | Share of packets per neighbour: 70% or more through one 🟡 | 20 packets |
| 🕐 Clock | Neighbours' zero-hop advert times | 3 neighbours' adverts |

**Best fix:** a problem outranks a caution; among equals, causes come before symptoms: interference and noise, antenna,
LNA, one-way link, weak link, load, errors, LNA power saving, paths, clock.

**Effect:** instant from cache; about 5 s when it runs a scan (see `diag rfscan`). Starts nothing else.

## diag lna

```text
diag lna
```

**Response:** while a study runs, progress (`📶 LNA: ⚪ studying, <h> of <h>h`, pairs, neighbours heard, a
leaning). Otherwise the last result, or `📶 LNA: no study yet.` Results longer than one reply arrive as
several.

| Result | Meaning |
| --- | --- |
| `📶 LNA: 🟢 keep it on` | On hears more, or a neighbour is heard only with it on |
| `📶 LNA: 🔴 overloaded` | Off hears more or decodes clearer |
| `📶 LNA: 🟡 no benefit here` | No significant difference after 60 or more pairs |
| `📶 LNA: 🔴 not working` | Floor moved less than 10 dB between on and off |
| `📶 LNA: ⚪ not enough data` | Fewer than 30 decided pairs or 2 neighbours |

A result that suggests a change ends with `tryset 86400 radio.fem.rxgain <on|off>` and `then: tryset keep`.

## diag lna start

```text
diag lna start [hours]
```

| Parameter | Definition |
| --- | --- |
| `hours` | Study length, 6-168; omitted means 48 |

**Response:** `📶 LNA study started, up to <hours>h` and what happens, or one of:

| Response | Meaning |
| --- | --- |
| `ERR: no switchable LNA on this board` | The board cannot switch its LNA |
| `ERR: study already running (diag lna stop)` | One study at a time |
| `ERR: hours 6-168` | Length out of range |

**Effect:** switches the live LNA state in paired blocks; the saved `radio.fem.rxgain` is not written.
Ends at the requested length, or early once the result is clear and at least 24 hours (or the
requested length, if shorter) have passed. The LNA is then restored to its state at the start.

**Persistence:** progress is saved about hourly and resumes after a reboot, unless `radio.fem.rxgain`
no longer matches its value at the start.

**Ends without a restore:** a `set radio.fem.rxgain` or `tryset ... radio.fem.rxgain`; the command still
runs and its value stands.

## diag lna stop

```text
diag lna stop
```

**Response:** the result so far, as for `diag lna`, or `ERR: no study running`.

**Effect:** restores the LNA to its state at the start.

## diag links

```text
diag links
```

**Response:** a verdict, one line per neighbour (strongest first: SNR, margin above the decode limit for the
current spreading factor, ✅ >= 6 dB, 🟡 3-6, 🔴 < 3, `hears us`), then one action.

| Result | Rule |
| --- | --- |
| `🔗 Links: ⚪ not enough data` | No neighbour with 5 or more packets yet |
| `🔗 Links: 🔴 most links weak` | 3 or more neighbours, two thirds under 3 dB: check antenna, cable and connector |
| `🔗 Links: 🟡 one-way link` | A neighbour heard well (10+ packets, >= 6 dB) relayed none of 20+ echoes of our relays |
| `🔗 Links: 🟡 <n> link(s) close to dropping` | A neighbour under 3 dB |
| `🔗 Links: 🟡 <n> link(s) can drop` | A neighbour at 3-6 dB |
| `🔗 Links: 🟡 our clock off` | 3 or more neighbours' zero-hop adverts, two thirds more than 2 min ahead or behind us |
| `🔗 Links: 🟢 all solid` | Every judged neighbour >= 6 dB |

**Method:** passive; every reception since boot counts toward its transmitter (last path entry, or a zero-hop
advert's sender), duplicates included, with a running SNR average. `hears us` means it relayed a packet
straight after us, or relayed our own advert. Neighbours silent for 24 h drop out. Never transmits, not saved.

## diag noise

```text
diag noise
```

**Response:** a verdict, the floor and when it occurs, one line per 4-hour block of local time (midnight first)
with the rise above the floor, then what it does to the links and one action. Arrives as several replies.

| Result | Rule |
| --- | --- |
| `🔊 Noise: ⚪ watching, <n> of 24h` | Fewer than 6 hours watched |
| `🔊 Noise: 🟢 quiet site` | No hour 3 dB or more above the floor, decodes normal |
| `🔊 Noise: 🟡 errors, not noise` | Decodes failing in a block without a noise rise: a colliding node or a strong signal |
| `🔊 Noise: 🟡 loud <hours>, no loss` | 3 dB or more above the floor, decodes normal |
| `🔊 Noise: 🟡 loud <hours>` | As above, too few packets to judge decodes |
| `🔊 Noise: 🔴 loud <hours>, links drop` | The rise exceeds the weakest judged link's margin (`diag links`) |
| `🔊 Noise: 🔴 loud <hours>, losing packets` | Loud, and failed decodes rose with it |

`<hours>` is the longest loud run, such as `8am-5pm`, or `at times` when loud at three or more separate times.

| Block line | Meaning |
| --- | --- |
| 🟢 | Under 3 dB above the floor |
| 🟡 | 3 dB or more, or failed decodes above normal |
| 🔴 | Loud with failed decodes above normal, or loud enough to sink the weakest link |
| ⚪ `no data` | No hour of the block watched yet |
| `+<lo> to +<hi> dB` | Lowest and highest hourly rise above the floor |
| `<n>% err` | Shown only when the block's failed-decode rate is at least twice and 3 points above normal (the quietest block with 20 or more packets) |

Then `Not mesh traffic.` or `Busy mesh hours.` (loud hours with at least twice the packets of quiet ones),
`<id> drops below its limit <hours>.` or `Links keep <n>+ dB margin through it.`, and
`Try: diag rfscan at <hour> to find it.` Or `ERR: clock not set (time <epoch>)`.

**Method:** one idle RSSI reading every 15 s (skipped mid-packet), median per hour, plus upstream's packet
and failed-decode counters per hour. The floor is the quietest hour of the last 24. Hours sampled with the
LNA in the other state from now (an LNA study's opposite blocks, or a changed setting) are left out, because
the LNA moves the floor by 20 dB or more. Never transmits.

**Effect:** none; the watch runs from boot whenever the clock is set and saves its 24 hours to flash each
hour.

## diag rfscan

```text
diag rfscan
```

**Response:** a header (`near <freq> ±4M 6sw fl<floor> LNA <on|off|n/a>`), a legend, one line per whole
MHz from mesh frequency -4 to +4, then findings. Arrives as several replies.

| Line part | Meaning |
| --- | --- |
| ⚫ 🟡 🟠 🔴 | Bursts out of 6 sweeps: none, 1-2, 3-4, 5-6 |
| `·` | Average within 4 dB of the floor |
| 🟩 🟨 🟧 🟥 | One square per 2 dB above 4 dB, up to 12, followed by the average in dBm |
| `◂mesh` | The row holding the mesh frequency |

| Finding | Rule |
| --- | --- |
| `⚠️ mesh channel steady <n> dB` | The mesh channel itself averages 6 dB or more above the floor |
| `⚠️ steady <freq> (<offset>) <n> dB` | A nearby step averages 10 dB or more above the floor |
| `⚠️ bursts <freq> (<offset>) <n>/6 pk<dBm>` | A nearby step burst in 3 or more of 6 sweeps |
| `mesh ch bursts <n>/6 (mesh traffic?)` | Bursts on the mesh channel itself |
| `weak steady <freq> (<offset>) <n> dB` | Nothing above, but a step 6-10 dB over the floor |
| `nearby band clear` | No finding |

Or `ERR: rfscan needs an SX126x radio`, `ERR: radio busy, try again` (a packet was arriving), or
`ERR: radio settings unavailable`.

**Method:** floor from 26 readings 1 MHz apart around the mesh frequency (25th percentile); then 6 sweeps
of 33 steps, 250 kHz apart, at 250 kHz bandwidth, in two runs of three with 1.5 s back on the mesh between
them. Each step settles 5 ms and averages 16 readings; a burst is a sweep 6 dB or more above that step's
own average. Never transmits, and leaves the LNA as it is.

**Effect:** the command takes about 5 s; the node is off its channel for at most about 1.3 s at a time
and then returns to its current channel (a running `tempradio` trial included).

## diag.tz

```text
get diag.tz
set diag.tz <offset>
```

| Parameter | Definition |
| --- | --- |
| `offset` | Whole hours from UTC, -12 to +14, with or without `h` (`-5h`, `-5`, `+9`) |

**Response:** `> <hours>` for `get`; `OK` or `Error, must be from -12 to +14` for `set`.

**Storage:** persistent. Stored like the companion's `tz_offset` (whole hours, key `tz_offset`) in the
mod's own file. A fixed offset with no daylight-saving rules: update it when the clocks change.

**Effect:** reports print local times when the offset is not 0, and UTC times when it is 0.
