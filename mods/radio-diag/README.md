# radio-diag

[↩ Back to firmware readme](../../README.md)

Radio diagnostics that answer with a verdict and one action instead of raw readings: an LNA study
(does this site hear better with the board's external LNA on or off?), a link check, a
24-hour noise watch and a nearby-interference scan.

```text
diag
diag lna start
diag lna
diag lna stop
diag links
diag noise
diag rfscan
set diag.tz -5h
```

## Summary

`diag` puts every area on one screen: links, LNA, nearby interference, noise, failed decodes, channel load,
paths and clock, each 🟢 🟡 🔴 or ⚪ while data builds up. The worst area sets the header and comes with one fix. It reads
what the other checks collected, running an interference scan only when none is cached or the last is a day old.

## LNA study

- Switches the LNA on and off in paired blocks of 4-6 minutes, in random order, for up to 48 hours
  (6-168 with `diag lna start <hours>`).
- Compares the same neighbours in both states: how many packets each one is heard with, and how
  clearly the strong ones decode.
- Ends early, after at least a day, once the answer is clear.
- Puts the LNA back where it was. The saved setting is never changed, so a reboot always comes up as
  configured.
- The verdict comes with the command to try, for example
  `tryset 86400 radio.fem.rxgain off`, which reverts unless kept.

| Verdict | Meaning |
| --- | --- |
| 🟢 keep it on | The LNA hears more, or a neighbour is only heard with it on |
| 🔴 overloaded | Off hears more or clearer: a strong signal nearby swamps the LNA |
| 🟡 no benefit here | No difference; off saves about 9 mA |
| 🔴 not working | Switching it barely moves the noise floor: likely an LNA or FEM fault |
| ⚪ not enough data | Too little traffic to judge |

A clear daily pattern adds one line, such as `Off better 7am-3pm`. Times are local once `diag.tz`
is set, and UTC until then.

- Only on boards that can switch the LNA (`radio.fem.rxgain`, heltec_v4 V4.3); elsewhere it refuses.
- Progress survives a reboot, unless the LNA setting was changed meanwhile.
- A `set` or `tryset` of `radio.fem.rxgain` ends the study and leaves the operator's value in place.
- If another feature moves the LNA (power-guard on low battery), the study pauses and retries.
- Weak neighbours may be missed during the off blocks, about half the study time.

## Link check

`diag links` rates each neighbour link from what the node has heard since boot: solid, can drop, or
close to dropping (SNR margin above the decode limit), plus one-way links and a clock that disagrees with
the neighbours'. Passive: never transmits.

## Noise watch

`diag noise` tracks the mesh channel's noise floor every hour of the day, without being started, and reports it
in six 4-hour blocks of local time:

```text
🔊 Noise: 🔴 loud 8am-5pm, losing packets
Floor -112 (6pm-6am). Not mesh traffic.
🟢 12a-4a  +0 dB
🟢 4a-8a   +0 to +2 dB
🔴 8a-12p  +7 dB  9% err
🔴 12p-4p  +7 dB  9% err
🔴 4p-8p   +0 to +7 dB
🟢 8p-12a  +0 dB
C5FB drops below its limit 8am-5pm.
Try: diag rfscan at 8am to find it.
```

- The floor is the quietest hour; each block shows how far above it the noise rose. A rise of N dB takes N dB
  off every link's margin.
- Failed decodes appear only where they rose above the site's normal rate, which also catches packets failing
  with no noise rise at all.
- Needs the clock set. Kept across reboots; hours sampled with the LNA in the other state are left out.
- Passive: one idle RSSI reading every 15 s. Never transmits.

## Interference scan

`diag rfscan` checks the mesh frequency +/-4 MHz for anything strong enough to hurt reception, steady
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

Detailed syntax and responses are in the [radio-diag CLI reference](docs/cli-additions.md).

[↩ Back to firmware readme](../../README.md)
