# diag-reports

[↩ Back to firmware readme](../../README.md)

Diagnostics that relate what the node sees: each symptom is matched to the cause that explains it, the summary gives one fix, and a live probe confirms it on request.

```text
diag
diag probe
diag auto
diag radio.links
diag radio.noise
diag radio.rfscan
diag radio.lna start
diag batt
diag batt.history
diag temp
clear diag
```

## One cause, one fix

```text
🔗 Links    🟡 C5FB drops in noise
📶 LNA      🟢 on helps (+18%)
📡 Nearby   🟢 nothing close
🔊 Noise    🔴 Loud 8am-5pm
🛡 Guard    🟢 No Freezes
📉 Decodes  🟡 4.5%, from noise
🔋 Battery  🟢 holding, 3.98V
📊 Load     🟢 channel 6% busy
🧭 Paths    🟢 spread over 3
🕐 Clock    🟢 in step
~
🩺 Diag     🔴 noise 8am-5pm
Best fix: scan at 9am to find it.
See: diag radio.rfscan
(sure: seen 5 of 5 days)
```

- Every observation is kept by hour of day, so a symptom is blamed on the cause active in the same hours: a link that drops only in the loud hours is noise, not the link.
- Causes weighed: noise, a blocker beside the channel, receiver freezes, lost lock-up protection, LNA overload or fault, the node's own antenna, a neighbour that does not hear us, collisions, channel load, cold, load drain, the panel, heat, the clock.
- How sure: the same cause in the same hours across recorded days.

## Guard

SX126x receivers can lock up after a strong signal. The guard reads the radio's lock-up protection bit and receiver state just before each AGC reset. A busy state that outlasts the signal, with the channel back at its floor, counts as a freeze: freezes are labelled in the noise report and kept out of the floor. A busy state that clears with the signal counts as a strong signal at the reset, not a freeze. Protection found off, or freezes with the AGC reset off, are problems in their own right.

## Probes and captures

- `diag probe` runs the one live check that matches the leading cause: an RSSI burst, a scan, the guard registers, the LNA floor on vs off, the battery's last hour, or one ping to the neighbour in question. It takes no arguments. Only the ping transmits.
- Automatic captures record the cause while it is happening (a scan after repeated freezes, RSSI bursts at a loud onset or a failed-decode spike), capped and never transmitting. `diag auto off` or `pause` stands them down.
- Slow commands answer `ok, scanning ~5s` at once and send the report when the job is done; a repeat during the job gets its progress instead of a second job.

## LNA study

`diag radio.lna start` switches the LNA in paired blocks for up to 48 hours (6-168) and compares the same neighbours in both states, then advises with the `tryset` to try. Only on boards that can switch the LNA; elsewhere no report mentions it.

## Storage

One store file written once an hour, plus the study's file while one exists. Upgrading from diag-report deletes its old files; the time zone carries over to core's `set tz`.

## CLI Commands

Detailed syntax and responses are in the [diag-reports CLI reference](docs/cli-additions.md).

[↩ Back to firmware readme](../../README.md)
