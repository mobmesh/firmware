# diag-reports CLI reference

[↩ Back to diag-reports readme](../README.md)

Commands added by `diag-reports`, available over serial and authenticated remote CLI.

`<value>` denotes a required parameter. `[value]` is optional.

## Command index

| Command | Purpose |
| --- | --- |
| [`diag`](#diag) | Every area on one screen, one cause, one fix |
| [`diag radio`](#diag) | Same report as `diag` |
| [`diag probe`](#diag-probe) | One live check that confirms the leading cause |
| [`diag auto [on\|off\|pause [min]]`](#diag-auto) | Switch the automatic captures |
| [`diag radio.links`](#diag-radiolinks) | How solid each neighbour link is |
| [`diag radio.noise`](#diag-radionoise) | The channel's noise over 24 h, in 4-hour blocks |
| [`diag radio.rfscan`](#diag-radiorfscan) | The mesh frequency +/-4 MHz for nearby interference |
| [`diag radio.lna`](#diag-radiolna) | LNA study progress, or the last result |
| [`diag radio.lna start [hours]`](#diag-radiolna) | Start an LNA study |
| [`diag radio.lna stop`](#diag-radiolna) | End the study early |
| [`set radio.fem.rxgain auto`](#lna-auto) | The LNA on or off per 2-hour block, learned from ping tests |
| [`diag batt`](#diag-batt) | Battery over the last 24 h |
| [`diag batt.history`](#diag-batthistory) | Morning battery low over 30 days |
| [`diag temp`](#diag-temp) | CPU temperature now and over 30 days |
| [`clear diag`](#clear-diag) | Clear every diag history and start over |

## diag

```text
diag
diag radio
```

**Response:** one line per area with 🟢 🟡 🔴 or ⚪ (not enough data yet), a `~` line, then the summary line in the same columns, the single best fix, the command with details and, when the verdict rests on history, how sure it is. Arrives as several replies.

| Area | Shown | ⚪ until |
| --- | --- | --- |
| 🔗 Links | always | A neighbour has 5 packets |
| 📶 LNA | boards with a switchable LNA | A study has run |
| 📡 Nearby | SX126x radios | The first scan |
| 🔊 Noise | always | 6 hours watched with the clock set |
| 🛡 Guard | always; ⚪ `not on this radio` off SX126x | — |
| 📉 Decodes | always | 50 packets |
| 🔋 Battery | always | 20 hours watched with the clock set |
| 📊 Load | always | 10 minutes up |
| 🧭 Paths | always | 20 packets |
| 🕐 Clock | always | 3 neighbours' adverts |

**One cause:** each symptom is matched in time against the causes that could explain it (noise in certain hours, a blocker beside the channel or a transmitter on it, receiver freezes, lost lock-up protection, LNA overload or fault, the node's own antenna, a neighbour that does not hear us, a neighbour strong enough to swamp the receiver, collisions, channel load, cold, load drain, the panel, heat and restarts after heat). What the automatic captures and probes saw counts as evidence: a carrier caught at the failures makes them interference, packets caught in the loud hours make them a busy mesh. An explained area keeps its colour and names its cause (`4.5%, from noise`); the leading cause gives the only fix.

| Guard line | Meaning |
| --- | --- |
| `🟢 No Freezes` | All clear |
| `🟡 Off 2x, restored` | The lock-up protection bit was found cleared; the AGC reset restored it. The `Off` lines appear only on boards whose build sets the bit (heltec_v4, rak3401); elsewhere it is off by design and only freezes are judged |
| `🔴 Off, not restored` | The bit is still cleared with the AGC reset running; summary `guard off, not restored`, fix: reboot |
| `🟡 3 Freezes, Cleared` | The receiver froze; each freeze lasted up to one AGC reset interval |
| `🔴 3 Freezes, blocker` | Freezes, with a strong signal found beside the channel |
| `🔴 3 Freezes, AGC Off` | Freezes with nothing to clear them |
| `🔴 Off, AGC Off` | Unprotected and never restored |
| `🔴 Frozen Now` | Frozen at this moment |

**Confidence:** `(sure: seen 5 of 7 days)` when the same cause showed in the same hours on 3 or more recorded days, `(seen 1 of 2 days)` below that, `(early: 10h seen)` on the first day.

**Effect:** instant, except when no interference scan is cached or the last is a day old: then `ok, scanning ~2s` comes back first and the report follows a short scan of the channel and two bandwidths either side. While the automatic captures are off or paused, the report ends with `Auto captures off.` or `Auto captures paused <n>m.`

## diag probe

```text
diag probe
```

**Response:** the engine names the leading cause and the probe runs the one live check that confirms it, or `Nothing to check: all green.` `ERR: diag probe takes no arguments` when given one.

| Leading cause | Check | Transmits | Reply |
| --- | --- | --- | --- |
| Noise in certain hours | A short scan of the channel and two bandwidths either side | No | `ok, scanning ~2s`, then the result |
| Collisions, channel load, a strong neighbour | 2 s of RSSI on the channel: a steady carrier, packets on air, or quiet at the floor | No | `ok, listening ~2s`, then the result |
| Freezes, a blocker, lost protection | The lock-up guard and receiver state now, last freeze, strong signals at resets | No | instant |
| LNA overload, fault or setting | The floor with the LNA on and off (boards with a switchable LNA only) | No | under 1 s |
| Battery low, cold, panel, load drain | The last hour's battery change against the day's | No | instant |
| A one-way, weak or failing link | One direct ping to that neighbour (the one-way one, else the weakest) | Yes, one direct trace | `ok, pinging <id> ~2s`, then `out / back` SNR and which side is weak, or `No answer from <id>` |

What a probe finds is kept as evidence: a clean ping clears a one-way verdict for 24 hours, a burst is filed as a capture, a scan replaces the cached scan, the LNA check feeds the overload rule.

## diag auto

```text
diag auto
diag auto on
diag auto off
diag auto pause [minutes]
```

| Parameter | Definition |
| --- | --- |
| `minutes` | 1-1440; omitted means 5 |

**Response:** `Auto: <on|off|paused>, <n> of 3 scans today`, the day's bursts, the time left on a pause, and the captures on record (`Scan 9:14: blocker`, `Loud 8:42: carrier`, `Loud 6:27: other LoRa`, `Decodes 13:05: packets`); `OK - auto captures <on|off>`; `OK - auto captures paused <n>m`.

**Effect:** switches only the automatic captures; the passive watch and `diag probe` are unaffected. `off` is saved; a pause ends at a reboot.

| Capture | When | Takes | Cap |
| --- | --- | --- | --- |
| T1 | 2 freezes within an hour | An interference scan | 3 a day, 1 an hour, none within an hour of another scan |
| T2 | The first sample of a 4-hour block 6 dB over the floor | 2 s RSSI burst | 24 a day, 1 an hour |
| T3 | 3 failed decodes inside a minute | 2 s RSSI burst | 24 a day, 1 an hour |
| T4 | The guard found off at an AGC reset, on boards whose build sets it | The guard read again after the reset | every time |

Captures never run mid-packet, with the clock unset, or when the last hour's load was over 25%. They never transmit. A capture with 3+ preambles that never reach our header and nothing decoded counts toward another LoRa network; it is named only after two such hours in a day with none of ours decoded, since weak packets of our own leave orphan preambles too.

## diag radio.links

```text
diag radio.links
```

**Response:** a verdict, one row per judged neighbour (strongest first: status, id, SNR, margin above the decode limit for the spreading factor, ✅ >= 6 dB, 🟡 3-6, 🔴 < 3, then a note), `~`, then the cause or one action. With direct-retry on, the report ends with its week: `Retry 7d: <won> of <retried> won (<n>%)` (packets that needed a resend, and those the next hop then confirmed; sampled hourly, kept 7 days across reboots), with `, was <n>%` when the last 3 days fell 20 points or more behind the 4 before, then `By hop: A6B2 2/12 C5FB 3/3` (won/retried per next hop since boot); `Retry on: none needed this week` before any resend. Nothing when direct-retry is off or not in the build.

| Result | Rule |
| --- | --- |
| `🔗 Links: ⚪ Not Enough Data` | No neighbour with 5 or more packets |
| `🔗 Links: 🟡 <id> drops in noise` | Heard in quiet hours, silent or weak in the loud ones |
| `🔗 Links: 🟡 <id> resends fail` | direct-retry's resends to that next hop mostly go unconfirmed (5 or more, under half confirmed): it may not hear this node; the summary names it one-way with the counts |
| `🔗 Links: 🔴 Most Links Weak` | 3 or more neighbours, two thirds under 3 dB |
| `🔗 Links: 🟡 One-Way Link` | Heard well (10+ packets, >= 6 dB), relayed none of 20+ echoes of our relays |
| `🔗 Links: 🟡 <n> Link(s) Close to Dropping` | A neighbour under 3 dB |
| `🔗 Links: 🟡 <n> Link(s) Can Drop` | A neighbour at 3-6 dB |
| `🔗 Links: 🟡 Our Clock Off` | 3 or more neighbours' adverts, two thirds more than 2 min ahead or behind |
| `🔗 Links: 🟢 All Solid` | Every judged neighbour >= 6 dB |

After `~`: for a one-way link, the last ping's out and back SNR names the weak side; paths leaning on a weak neighbour; a neighbour strong enough to swamp the receiver when decodes fail without noise; the weakest link's weak hours and how many of the last 24 it was heard in.

| Note | Meaning |
| --- | --- |
| `drops <hours>` | The link fails in the loud hours |
| `LNA-only` | Heard only with the LNA on; boards with a switchable LNA |
| `hears us` | It relayed a packet straight after us, or our advert |

**Method:** passive; every reception counts toward its transmitter. A 1-byte hash that starts exactly one known 2-byte neighbour counts as that neighbour. Neighbours silent for 24 h drop out. Setting the clock moves stored advert offsets with it. Not saved.

## diag radio.noise

```text
diag radio.noise
```

**Response:** a verdict, the floor and its quietest hours, one line per 4-hour block of local time, `~`, then the last capture, freezes, what the noise does to the links, and one action. Arrives as several replies. `ERR: clock not set (time <epoch>)` before the clock is set.

| Result | Rule |
| --- | --- |
| `🔊 Noise: ⚪ Watching, <n> of 24h` | Fewer than 6 hours watched |
| `🔊 Noise: 🟢 Quiet Site` | No hour 3 dB or more above the floor, decodes normal |
| `🔊 Noise: 🟡 Freezes, Not Noise` | Receiver freezes; those hours are left out of the floor |
| `🔊 Noise: 🟡 Errors, Not Noise` | Decodes failing in a block without a noise rise |
| `🔊 Noise: 🟡 Busy <hours>, Not Noise` | Loud hours that are mesh traffic: a capture saw packets, or they carry twice the packets of quiet hours |
| `🔊 Noise: 🟡 Loud <hours>, No Loss` | 3 dB or more above the floor, decodes normal |
| `🔊 Noise: 🟡 Loud <hours>` | As above, too few packets to judge decodes |
| `🔊 Noise: 🔴 Loud <hours>, Links Drop` | The rise exceeds the weakest link's margin |
| `🔊 Noise: 🔴 Loud <hours>, Losing Packets` | Loud, and failed decodes rose with it |

Block lines: status first, the hours, `+<lo> to +<hi> dB`, `<n>% err` when failed decodes are at least twice and 3 points above normal, `froze <n>x` for freezes.

After `~`: a weekly schedule; what a capture caught; where the source likely is from its strength, steadiness and hours (`Strong, follows charging: own charger or panel.`, `Strong, daytime: solar gear close by.`, `Weak, steady, evenings: lights or household.`); how much of the failed decodes it explains; how far a floor sits above this site's usual for that hour.

**Method:** one idle RSSI reading every 15 s (skipped mid-packet and just after a scan), median per hour, plus the radio's packet and failed-decode counters per hour. Hours sampled in the other LNA state are left out. Never transmits.

## diag radio.rfscan

```text
diag radio.rfscan
```

**Response:** `ok, scanning ~5s`, then a header (`near <freq> ±4M 6sw fl<floor>`, plus `LNA <on|off>` where the board can switch it), a legend, one line per whole MHz from mesh frequency -4 to +4, and findings. A repeat from the same sender while it runs: `working, scan <n> of 6 sweeps`; from anyone else: `busy, try again in a few seconds`.

| Line part | Meaning |
| --- | --- |
| ⚫ 🟡 🟠 🔴 | Bursts out of 6 sweeps: none, 1-2, 3-4, 5-6 |
| `·` | Average within 4 dB of the floor |
| 🟩 🟨 🟧 🟥 | One square per 2 dB above 4 dB, up to 12, then the average in dBm |
| `◂mesh` | The row holding the mesh frequency |

| Finding | Rule |
| --- | --- |
| `⚠️ mesh channel steady <n> dB` | The mesh channel averages 6 dB or more above the floor |
| `⚠️ steady <freq> (<offset>) <n> dB` | A nearby step averages 10 dB or more above the floor |
| `⚠️ bursts <freq> (<offset>) <n>/6 pk<dBm>` | A nearby step burst in 3 or more sweeps |
| `mesh ch bursts <n>/6 (mesh traffic?)` | Bursts on the mesh channel itself |
| `weak steady <freq> (<offset>) <n> dB` | Nothing above, but a step 6-10 dB over the floor |
| `Nearby Band Clear` | No finding |

Or `ERR: rfscan needs an SX126x radio`, `ERR: radio busy, try again`, `ERR: radio settings unavailable`.

**Method:** floor from 26 readings 1 MHz apart (25th percentile); 6 sweeps of 33 steps, 250 kHz apart, in two runs of three with 1.5 s back on the mesh between them. Each run is off channel for about 1.3 s; the node keeps running between them. Never transmits, leaves the LNA as it is. A steady signal beside the channel (within 1 MHz), 10 dB over the floor and 6 dB over the channel itself, is kept as a possible receiver blocker.

Energy on the channel is reported as where it peaks: `on our freq, <n> dB` when it sits exactly on the mesh frequency, or `<offset>k off ours, <n> dB` when it peaks a measured half-bandwidth step away; the diag fix states the same (`confirmed on 910.525 MHz, 18 dB over the floor.`).

The wide chart is for this command only. Automatic scans (the `diag` refresh, `diag probe`, the freeze capture) cover the channel and two channel bandwidths either side in half-bandwidth steps, which is all a mesh node on a fixed frequency can act on.

## diag radio.lna

```text
diag radio.lna
diag radio.lna start [hours]
diag radio.lna stop
```

| Parameter | Definition |
| --- | --- |
| `hours` | Study length, 6-168; omitted means 48 |

**Response:** progress (`📶 LNA: ⚪ Studying, <h> of <h>h`, pairs, neighbours, a leaning) while a study runs; otherwise the last result, its freezes per state, `~`, and the advice; `📶 LNA: ⚪ No Study Yet` before the first. `ERR: no switchable LNA on this board`, `ERR: study already running (diag radio.lna stop)`, `ERR: hours 6-168`, `ERR: no study running`.

| Result | Meaning |
| --- | --- |
| `📶 LNA: 🟢 Keep It On` | On hears more, or a neighbour is heard only with it on |
| `📶 LNA: 🟡 Keep It On` | As above, but the LNA is now off |
| `📶 LNA: 🔴 Overloaded` | Off hears more or decodes clearer |
| `📶 LNA: 🟢 Overloaded (Study)` | As above, and the LNA is now off |
| `📶 LNA: 🟡 No Benefit Here` | No significant difference after 60 or more pairs |
| `📶 LNA: 🟢 No Benefit Here (Study)` | As above, and the LNA is now off |
| `📶 LNA: 🔴 Not Working` | The floor moved less than 10 dB between on and off |
| `📶 LNA: ⚪ Not Enough Data` | Fewer than 30 decided pairs or 2 neighbours |

**Effect:** switches the live LNA in paired 4-6 minute blocks; the saved `radio.fem.rxgain` is not written. Ends at the requested length, or early once the result is clear after at least 24 hours (or the requested length, if shorter), then restores the LNA. A `set` or `tryset` of `radio.fem.rxgain` ends it without a restore. Saved hourly; resumes after a reboot unless the LNA setting changed meanwhile.

## LNA auto

```text
set radio.fem.rxgain auto
get radio.fem.rxgain
```

**Response:** `OK - auto (learning, starts off)`; `get` shows the mode, the LNA's state now, then the learned on-hours in local time (a run across midnight is joined): `> mode:auto now:off; on 22:00-04:00`, `> mode:auto now:on (learning); off all day` while the current block alternates, `> mode:auto now:off (clock not set); on 22:00-04:00` before the clock is set (auto holds the LNA off until then), `> mode:auto now:on; on all day`; the `diag` 📶 row shows the part before the `;`, or a parked state, followed by `; on 06-08 banned 7d: decodes worse` while a ban is in force. `ERR: no switchable LNA on this board` where the LNA cannot be switched.

**Effect:** each 2-hour local block keeps the state its ping tests last proved best. Until a block is decided it alternates, on in odd local hours and off in even ones, so neighbours heard only with the LNA on are found and the audit gets both records; a decided block switches only when the tests show, with significance, that the other state does better there. A test pings a neighbour (rotating among the 3 weakest heard recently) with the LNA in one state, flips it, pings again and flips back; it scores which state got the reply, or the clearer one. A test whose outbound SNR changed between the pings (fading), or whose idle floor moved more than 6 dB off the LNA's usual shift (noise changed mid-test), is discarded; no test runs while the last hour's load is over 25%.

| Rule | Value |
| --- | --- |
| Test rate | Every 10 minutes for the first 7 days, then one per block; at most 150 tests a day |
| Switch | A block switches state after 8 or more decided tests and a significant score; decided without a winner, it runs off |
| Revert | Two tests in a row lost while a block runs on send it to off, clear its scores and start its learning again |
| Lost-neighbour veto | A neighbour that answers only with the LNA on turns that block on at once and keeps it on for 14 days |
| Fade | Scores fade by 5% a day (half-life about 14 days) |
| Retire | After the learning week, a block within 3% over 30 or more tests stops testing and runs off |
| Park | Floor moving less than 10 dB between states: `mode:auto parked (LNA not working)`, 🔴 in `diag`. Fewer than 2 pingable neighbours for 14 days: `mode:auto parked (too few neighbours)`. Every block retired: `mode:auto now:off; parked (no benefit)`, which runs off. A parked auto learns again after 90 days |
| Freezes | A freeze in a block while the LNA was on counts against on there |
| Audit | Every closed hour adds to its block's record for the state it ran: packets decoded, failed decodes, neighbours heard, freezes and idle floor. Once both states have 6 hours, a block's running state (on, for a block still alternating) is banned for 7 days when it is clearly worse than the other: failed-decode rate up 2 points and by half, packets down 30%, a neighbour fewer per hour, twice the freezes, or (LNA on) the floor more than 6 dB above the usual shift. A ban sends the block to the other state, and the banned state must re-earn it from new tests |
| Refresh | A block running on runs off one day in 14, so both records stay current |
| Overload | The daily engine run's `LNA overload` verdict bans on for 7 days in the blocks of the hours the LNA was on with failed decodes or freezes; with none named, every hour it was on; with no record, all day |

Tests transmit (two direct pings each) and stop while `diag auto` is off or paused, on a low battery (150 mV above power-guard's engage voltage, 3.45 V on builds without it), while a `diag radio.lna` study runs, and for 15 minutes after another feature (power-guard's low-battery drop) moves the LNA. `set radio.fem.rxgain on|off` (or a `tryset`) leaves auto and keeps its scores for a later `auto`. `clear diag confirm` wipes the scores and leaves auto on. `diag radio.lna` ends with the auto state, a row of the 12 blocks (`+` on, `-` off, `.` undecided and alternating, `x` retired), the gain against fixed off, the ban in force, the floor shift, the test tallies and the tests discarded or skipped; the diag 📶 line shows the auto state.

## diag batt

```text
diag batt
```

**Response:** a verdict, the voltage now (`(calibrated)` on boards with a measured divider), 24 hourly bars (oldest left), the hour labels, night drain and hours charged, low and high with times, the change, `~`, then the cause or one action. `🔋 Battery: ⚪ Not Available` on a board that reads 0 mV; `ERR: clock not set (time <epoch>)` before the clock is set.

| Result | Rule |
| --- | --- |
| `🔋 Battery: ⚪ Watching, <n> of 24h` | Under 20 hours watched |
| `🔋 Battery: 🟢 Steady` | Within 30 mV a day |
| `🔋 Battery: 🟢 Charging Fine` | Rising more than 30 mV a day |
| `🔋 Battery: 🟡 Net Loss Today` | Falling more than 30 mV a day, though some hours charged |
| `🔋 Battery: 🟡 Not Recovering Daily` | As above, and each of the last two whole days ended more than 30 mV lower |
| `🔋 Battery: 🟡 Draining` | Falling more than 30 mV a day, with no hour charging |
| `🔋 Battery: 🔴 Low` | At 3.35 V or below; calibrated boards only |

The cause line names the CPU below freezing when nothing charged, or the drain following channel load when busy days cost the battery more than quiet ones over the month.

## diag batt.history

```text
diag batt.history
```

**Response:** a verdict, 30 daily bars of the morning low (lowest reading before local noon), a date every week, days held, the lowest morning, the change, `~`, the days to 3.35 V at the month's rate when it is falling, and one action.

| Result | Rule |
| --- | --- |
| `🔋 30 Days: ⚪ Watching, <n> of 7 days` | Under 7 mornings |
| `🔋 30 Days: 🟢 Holding` | The newest morning is within 30 mV of the latest one at least 6 days before it |
| `🔋 30 Days: 🟡 Losing Ground` | More than 30 mV below it |
| `🔋 30 Days: 🔴 Low` | The newest morning at 3.35 V or below; calibrated boards only |

## diag temp

```text
diag temp
```

**Response:** a verdict, the CPU temperature now, each calendar week's low and high (Monday start, up to 5 weeks), the month's coldest and warmest, counts of freezing and hot days, `~`, then: an unplanned restart in the last 30 days after an hour at 60 °C or more (`Restarted 3 Nov after 67°C: heat.`), cold days the battery did not recover from, and one action. A `reboot` the operator sends is never counted. The CPU's own die temperature, not ambient.

| Result | Rule |
| --- | --- |
| `🌡️ CPU: 🟢 Normal` | No day in the last 7 below 0 °C or at 65 °C or more |
| `🌡️ CPU: 🟡 Below Freezing` | A day in the last 7 below 0 °C |
| `🌡️ CPU: 🟡 Running Hot` | A day in the last 7 at 65 °C or more; takes precedence |
| `🌡️ CPU: ⚪ Watching` | A reading now, no day filed yet |
| `🌡️ CPU: ⚪ Not Available` | No temperature sensor |

## clear diag

```text
clear diag
clear diag confirm
```

**Response:** `clear diag` names what will be cleared and arms the reset for 60 seconds; `clear diag confirm` within that time replies `OK - diag history cleared`. Without a recent `clear diag`: `ERR: send clear diag first, then confirm within 60 s`.

Clears the store (hourly and 30-day history, guard counters, freeze hours, captures, recurrence), the LNA study (a running one is stopped and the LNA restored), the link table and the cached scan. Keeps the time zone (`set tz`).

## Storage

One store file, written once an hour; the LNA study keeps its own file while one exists. On first boot the files of the older diag-report mod are deleted; its time zone carries over to core's `tz` setting.
