# batt-saver - Automatic Power Saving When the Battery Gets Low

MeshCore already has a power-saving mode. On a repeater it makes the node take
short naps (about 30 seconds at a time) whenever it has nothing to do, waking
immediately if a radio packet arrives. It saves real power, but it is a manual
switch: somebody has to type `powersaving on`, and somebody has to remember to
type `powersaving off` again later.

This mod can turn that switch automatically, based on the battery. When the
battery falls low the node starts napping to stretch what is left. When the
battery recovers -- the sun comes back, or someone swaps the cell -- it stops
napping and goes back to normal. **That behaviour is off by default** and is
enabled with `powersaving auto on`.

Below the napping mark sits a second, independent rung: at
`BATT_SAVER_SLEEP_MV` the node leaves service entirely and deep-sleeps, keeping
the reserve a solar pack needs to climb back. Napping stretches runtime; this
stops spending it. The two are separate switches, not stages of one.

The intent is a solar or battery repeater that looks after itself: it slows
down before it dies rather than after, and it comes back on its own without a
site visit.

**Repeater only, for now.** The napping rung has nothing to switch on a room
server -- that firmware has no sleep path. The deep-sleep rung would work there,
since it calls `enterDeepSleep()` directly, and would inherit the board's
`BATT_SAVER_SLEEP_MV`. Untested, not impossible.

## What It Actually Does

Every minute it reads the battery. If the battery reads at or below the "low"
mark several times in a row, it turns napping on. If it reads at or above the
"recovered" mark several times in a row, it turns napping off.

The two marks are deliberately far apart, and it insists on several readings in
a row rather than acting on one. Both of those are there for the same reason:
**napping itself changes the reading.** A sleeping node draws less current, so
the measured voltage rises the moment it starts saving power. With the marks
close together the node would immediately decide it had recovered, wake up, sag
again, and oscillate forever. Transmitting has the same effect in reverse -- a
transmission burst pulls the voltage down briefly, and that dip is not a flat
battery.

It also ignores readings above 4.5 V, because a node running on USB or a bench
supply is measuring the charger rail rather than a cell. A Heltec V4 with no
battery attached at all reports roughly 4.24 V, which would otherwise look like
a perfectly healthy full battery forever.

## What It Deliberately Does Not Do

**It never writes to the saved `powersaving` setting.** That setting is the
operator's stated intent, and MeshCore writes it to flash every time it
changes. If this mod drove that field directly, two bad things would follow:
every low-battery transition would be a flash write, and a node that saved
itself once would come back afterwards reporting `powersaving on` forever --
quietly overwriting a choice a human made, with no record of why.

Instead the mod keeps its own in-memory flag and the sleep decision becomes
"the operator asked for it **or** the battery is low". `powersaving` keeps
reporting what the operator asked for.

The deep-sleep rung's own settings *are* persisted, in an NVS namespace of our
own rather than `NodePrefs` -- no upstream struct to drift, and an absent key
stays distinguishable from a stored 0.

## Tuning

Thresholds are build flags, set per board in `variants/<board>/overrides.yaml`
alongside the `OTA_*` values. Defaults:

| Flag | Default | Meaning |
| --- | --- | --- |
| `BATT_SAVER_ON_MV` | 3500 | Start napping at or below this |
| `BATT_SAVER_OFF_MV` | 3800 | Stop napping at or above this |
| `BATT_SAVER_SAMPLE_INTERVAL_MS` | 60000 | How often to read the battery |
| `BATT_SAVER_CONSECUTIVE_SAMPLES` | 3 | Agreeing readings needed to switch |
| `BATT_SAVER_IMPLAUSIBLE_MV` | 4500 | Above this, assume no battery |
| `BATT_SAVER_DROP_FEM_LNA` | 0 | Also bypass the FEM LNA while saving |
| `BATT_SAVER_AUTO_DEFAULT` | 0 | Napping auto-engage; off unless asked for |
| `BATT_SAVER_SLEEP_MV` | 0 | Leave service below this; 0 disables the rung |
| `BATT_SAVER_SLEEP_SECS` | 60 | First wait only; boot-pwrcheck backs off after |
| `BATT_SAVER_SLEEP_FLOOR_MV` | 2500 | Lower rail accepted for a stored threshold |

`variants/heltec_v4/overrides.yaml` currently sets 3300 / 3600 -- a late
engagement that favours uptime, leaving little runway once it triggers.

The gap between the two voltage marks should stay wide. On heltec_v4 the
battery reading quantises to roughly 17 mV per step and depends on a per-unit
calibration value (`set adc.multiplier`, defaulting to `ADC_MULTIPLIER` in
`variants/heltec_v4/overrides.yaml`), so treat any single reading as approximate.

## Dropping the LNA (off by default)

While saving is engaged the mod can also bypass the LoRa front-end LNA, and put
it back on release. **This is disabled by default**, because it does not pay for
itself.

Measured on a Heltec V4.3: roughly **0.3 mA** difference against a **~5.5 mA**
sleeping floor -- about 5%. On a 3000 mAh cell that is the difference between
roughly 23 and 22 days of idle runtime, in exchange for around 10 dB of RX
sensitivity. A repeater that hears meaningfully less is a routing hazard:
neighbours keep routing through a node that no longer covers what it
advertised, and from outside it looks like a partial fault rather than
deliberate conservation.

The saving is small because the sleeping floor is dominated by the SX1262
sitting in RX -- the radio must stay listening to wake on an incoming packet.
The nap itself is where the power goes: an awake ESP32-S3 draws tens of mA
against that ~5.5 mA floor, so the MCU sleep is a 5-10x win and the LNA is
rounding error.

Set `BATT_SAVER_DROP_FEM_LNA: 1` to enable it anyway -- reasonable only where
survival clearly beats coverage, such as a remote solar site nobody can reach.

If enabled, the mod reads the current hardware state before bypassing and only
restores the LNA if it was the one that turned it off, so an operator running
`radio.fem.rxgain off` is not overridden. Nothing is persisted. Boards without
FEM control (xiao_c3) skip it entirely.

### Upstream bug worth knowing

`CommonCLI.h` maps both JSON prefs keys to the same field:

    def("rxgain",     _parent->rx_boosted_gain);
    def("fem_rxgain", _parent->rx_boosted_gain);   // should be radio_fem_rxgain

Since v1.17.0 stores config in `/prefs.json`, `radio.fem.rxgain` does not
round-trip a reboot -- it reloads whatever `radio.rxgain` was. Note that
`radio.rxgain` (SX1262 boosted gain) and `radio.fem.rxgain` (FEM LNA) are
different settings; this mod only touches the latter.

## Enabling It

Add `batt-saver` to a target's `mods:` list in `build-targets.yaml`:

```yaml
    mods: [shim, hotspot-ota, timing-safety, batt-saver]
```

The mod contributes no suffix, so asset basenames are unchanged -- it is a
failsafe rather than a feature, same reasoning as `boot-pwrcheck`.

Neither rung engages on its own: `BATT_SAVER_AUTO_DEFAULT` is 0, and
`BATT_SAVER_SLEEP_MV` is 0 unless a board states a measured value. A board whose
gauge has never been characterised stays dark.

## OTA Updates Are Already Safe

A napping node could not otherwise complete an OTA update -- `start ota wan`
needs sustained uptime and a live WiFi association, and a 30-second sleep
mid-download would break it. That is handled already, and not by this mod.

`ESP32Board::sleep()` refuses outright while `inhibit_sleep` is set, and both
upstream's `startOTAUpdate()` and our own `hotspot-ota` patch set it around the
download. Since the guard sits inside `sleep()` itself, it applies no matter who
asked for the sleep -- so a low battery cannot interrupt an update in progress.

This mod therefore carries no inhibit API of its own. It hooks into
`helpers/ModHooks.cpp` rather than editing upstream's `main.cpp`; that file comes
from `shim/0001`. It also declares `hotspot-ota/0002` as a context dependency,
since that patch edits the same regions of `ModHooks.cpp`.

## Debugging

Our heltec_v4 repeaters ship headless (`-UDISPLAY_CLASS`), so there is no screen
either. A node that engages saving gives no local indication at all.

Once the node is napping, serial goes quiet -- light sleep has no serial wake
source, so commands typed at it are silently dropped. Battery voltage remains
visible over the mesh, since it is the first field of the repeater status
response and is also published as channel 1 of the telemetry data, both
available without admin rights.

## Commands

Claimed ahead of upstream's `powersaving` handler, which matches on an 11-character
prefix and would otherwise answer these as a status query.

    powersaving safe              on/off, threshold, last reading
    powersaving safe.mv <mv>      set threshold; 2500-3299, or 0 to disable
    powersaving safe on|off       toggle the rung, leaving the threshold alone
    powersaving auto              napping state, active flag, transition count
    powersaving auto on|off       toggle automatic napping

`safe.mv` and `safe on|off` persist. They are deliberately separate, so a
threshold can be tuned for a site and the rung still switched off without losing
it. `auto` is runtime only -- a reboot restores the compiled default.
