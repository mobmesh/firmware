# diag-report

[↩ Back to firmware readme](../../README.md)

Diagnostics that answer with a verdict and one action instead of raw readings: a one-screen summary,
a 24-hour battery report, a link check, a 24-hour noise watch, a nearby-interference scan and an LNA
study (does this site hear better with the board's external LNA on or off?).

```text
diag
diag radio
diag batt
diag radio.links
diag radio.noise
diag radio.rfscan
diag radio.lna start
diag radio.lna
diag radio.lna stop
set diag.tz -5h
```

## Summary

`diag` puts every area on one screen: links, LNA, nearby interference, noise, battery, failed decodes, channel load,
paths and clock, each 🟢 🟡 🔴 or ⚪ while data builds up. The worst area comes with one fix. It reads
what the other checks collected, running an interference scan only when none is cached or the last is a day old.

## Battery report

`diag batt` shows the last 24 hours of battery voltage as one row of hourly bars, with the verdict first and one action last:

```text
🔋 Battery: 🟡 Not Recovering Daily
Now 3.72 V
▅▆▆▇▇▇███▇▇▆▆▅▅▄▄▄▃▃▂▂▁▁
    6p    12a   6a    12p
Night drain 9 mV/h, charged 7 hours
Low 3.72 V at 1pm, high 3.91 V at 9pm
Net -108 mV over 23h
➜ Check the Panel: it isn't refilling each day
```

| Verdict | Meaning |
| --- | --- |
| 🟢 Steady | Within 30 mV a day |
| 🟢 Charging Fine | Rising more than 30 mV a day |
| 🟡 Net Loss Today | Falling, though it charged: may just be a low sun day |
| 🟡 Not Recovering Daily | Falling, and each of the last two whole days ended lower too |
| 🟡 Draining | Falling, with no hour charging |
| 🔴 Low | At 3.35 V or below; `heltec_v4` only, the board with a measured battery divider |
| ⚪ Watching | Under 20 hours of readings, too short for a full charge and drain cycle |

- Reports what was measured: no percentage or time left, because voltage is a poor guide to capacity.
- Needs the clock set. Reads the battery every 5 minutes and saves once an hour, so history survives a reboot.
- A board that reads 0 mV shows ⚪ Not Available. After 3 timed zero readings in a row, checking and logging stop until a reboot.
- Passive: never transmits.

## LNA study

`diag radio.lna` answers whether this site hears better with the board's external LNA on or off:

```text
📶 LNA: 🟢 Keep It On
Studied 48h, 288 pairs.
On hears 38% more packets
from 6 neighbours.
Restored: on. No change needed.
```

- Switches the LNA on and off in paired blocks of 4-6 minutes, in random order, for up to 48 hours
  (6-168 with `diag radio.lna start <hours>`).
- Compares the same neighbours in both states: how many packets each one is heard with, and how
  clearly the strong ones decode.
- Ends early, after at least a day, once the answer is clear.
- Puts the LNA back where it was. The saved setting is never changed, so a reboot always comes up as
  configured.
- The verdict comes with the command to try, for example
  `tryset 86400 radio.fem.rxgain off`, which reverts unless kept.

| Verdict | Meaning |
| --- | --- |
| 🟢 Keep It On | The LNA hears more, or a neighbour is only heard with it on |
| 🔴 Overloaded | Off hears more or clearer: a strong signal nearby swamps the LNA |
| 🟡 No Benefit Here | No difference; off saves about 9 mA |
| 🔴 Not Working | Switching it barely moves the noise floor: likely an LNA or FEM fault |
| ⚪ Not Enough Data | Too little traffic to judge |

A clear daily pattern adds one line, such as `Off better 7am-3pm`. Times are local once `diag.tz`
is set, and UTC until then.

- Only on boards that can switch the LNA (`radio.fem.rxgain`, heltec_v4 V4.3); elsewhere it refuses.
- Progress survives a reboot, unless the LNA setting was changed meanwhile.
- A `set` or `tryset` of `radio.fem.rxgain` ends the study and leaves the operator's value in place.
- If another feature moves the LNA (power-guard on low battery), the study pauses and retries.
- Weak neighbours may be missed during the off blocks, about half the study time.

## Link check

`diag radio.links` rates each neighbour link from what the node has heard since boot: solid, can drop, or
close to dropping (SNR margin above the decode limit), plus one-way links and a clock that disagrees with
the neighbours'. Passive: never transmits.

## Noise watch

`diag radio.noise` tracks the mesh channel's noise floor every hour of the day, without being started, and reports it
in six 4-hour blocks of local time:

```text
🔊 Noise: 🔴 Loud 8am-5pm, Losing Packets
Floor -112 (6pm-6am). Not mesh traffic.
🟢 12a-4a  +0 dB
🟢 4a-8a   +0 to +2 dB
🔴 8a-12p  +7 dB  9% err
🔴 12p-4p  +7 dB  9% err
🔴 4p-8p   +0 to +7 dB
🟢 8p-12a  +0 dB
C5FB drops below its limit 8am-5pm.
Try: diag radio.rfscan at 8am to find it.
```

- The floor is the quietest hour; each block shows how far above it the noise rose. A rise of N dB takes N dB
  off every link's margin.
- Failed decodes appear only where they rose above the site's normal rate, which also catches packets failing
  with no noise rise at all.
- Needs the clock set. Kept across reboots; hours sampled with the LNA in the other state are left out.
- Passive: one idle RSSI reading every 15 s. Never transmits.

## Interference scan

`diag radio.rfscan` checks the mesh frequency +/-4 MHz for anything strong enough to hurt reception, steady
or in bursts, and answers with a small chart and plain findings:

```text
near 910.525 ±4M 6sw fl-69 LNA on
⚫🟡🟠🔴 bursts/6 ■ avg >4dB 2dB
909 🟡 🟩🟩 -63
910 ⚫ 🟩 -65 ◂mesh
912 ⚫ 🟩🟩🟩 -60
weak steady 912.03 (+1.50) 9 dB
```

- Takes about 5 s; off the mesh channel for at most about 1.3 s at a time. Never transmits.
- SX126x radios only; other radios refuse.

## CLI Commands

Detailed syntax and responses are in the [diag-report CLI reference](docs/cli-additions.md).

[↩ Back to firmware readme](../../README.md)
