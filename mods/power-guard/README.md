# power-guard - Never Strand a Node

Everything this project does about power: when a node conserves, when it stops
relaying, when it comes back, and what happens if it browns out anyway. Formerly
two mods, `batt-saver` and `boot-pwrcheck`, merged because they were never separable
-- they edit the same regions, share one threshold ladder, and the ordering between
them is a contract nothing could enforce across a mod boundary.

**Repeater only, for now.** The napping rung has nothing to switch on a room server
-- that firmware has no sleep path. Everything else would work there, and would
inherit the board's thresholds. Untested, not impossible.

## The Ladder

What happens as a heltec_v4 repeater's battery falls, highest to lowest:

| mV | Name | What happens |
| --- | --- | --- |
| 3600 | `BATT_SAVER_OFF_MV` | Stops napping |
| 3300 | `BATT_SAVER_ON_MV` | Starts napping |
| 3150 | `BOOT_PWRCHECK_MIN_MV` | Won't come back into service below this |
| 2800 | `BATT_SAVER_SLEEP_MV` | Hibernates |
| 2400 | *(measured)* | Can't boot at all below this |

The ordering is the contract. Hibernate must sit under the return-to-service mark,
or a node hibernates and immediately qualifies to run again -- alive, but only
between naps. `BATT_SAVER_SLEEP_MAX_MV` (3149) is the CLI ceiling that enforces it.

All values are gauge millivolts, not volts at the pins. The two differ by roughly
30 mV at 3.7 V and 200 mV at 2.6 V on this board, and the boot floor is measured the
same way, so comparing gauge against gauge keeps the error out of the margin.

## Napping

MeshCore already has a power-saving mode: short naps whenever the node has nothing
to do, waking immediately on a packet. It is a manual switch. This turns it on and
off from the battery instead -- below `BATT_SAVER_ON_MV`, off again above
`BATT_SAVER_OFF_MV`.

**Off by default.** Light sleep drops serial and thins RX, so it waits for
`powersaving auto on`.

The two marks are far apart, and it insists on several agreeing readings rather than
one, for the same reason: **napping changes the reading.** A sleeping node draws
less, so measured voltage rises the moment saving engages. Close marks would make it
decide it had recovered, wake, sag, and oscillate. Transmitting does the same in
reverse -- a burst pulls voltage down briefly, and that dip is not a flat battery.

## Hibernating

Below `BATT_SAVER_SLEEP_MV` the node leaves service entirely and deep-sleeps.
Napping stretches runtime; this stops spending it, keeping the reserve a solar pack
needs to climb back. Deep sleep draws 3-4 mA against 20-54 mA for a reset loop, and
at dawn the panel has to beat whichever state the node is in.

Sampled from `modLoop()`, and it takes `BATT_SAVER_CONSECUTIVE_SAMPLES` agreeing
readings. That count is what keeps a transmit from triggering it: a dip lasts well
under a second against a 60 s sampler. Measured on board 1 at 45 mV above the
threshold with adverts every 5 s -- 13% of samples fell below it, never three in a
row, 41 transmits and no sleep.

It sleeps `BATT_SAVER_SLEEP_SECS` once and the boot check owns the schedule after,
so there is one retry ladder rather than two.

## Checking at boot

Runs at the top of `modRadioInit()`, before `radio_init()` powers the radio up. Below
`BOOT_PWRCHECK_MIN_MV` the node deep-sleeps and reboots to check again, doubling from
`BOOT_PWRCHECK_BACKOFF_START_SECS` until it reaches `BOOT_PWRCHECK_RETRY_SECS`. A
check that passes resets the backoff, so a brief dip costs a minute rather than
inheriting a long interval from an earlier depletion.

The retry counter lives in RTC slow memory, so nothing is written to flash at low
voltage. Loss or corruption falls back to the shortest interval.

Without it, a pack too weak to survive radio startup browns out mid-init and resets,
forever: each cycle spends charge and reaches nothing.

A reading of 0 mV is ignored, since both rungs act on low voltage and a zero would
otherwise read as flat. That is what a board powered through its 3.3 V pins reports:
the divider feeding the ADC sits on the battery node, which those pins bypass. High
readings need no guard -- nothing engages above the marks, so a board on USB or a
bench supply is already left alone.

## poweroff requires a wake time

Upstream's `poweroff` / `shutdown` calls `powerOff()` -> `enterDeepSleep(0)`, which
never wakes -- and it carries no serial-only guard, so it is reachable over the mesh.
One mistyped command ends a repeater until someone climbs to it.

This claims the verb ahead of upstream's branch:

    poweroff <secs>    deep sleep, then reboot
    poweroff           refused -- usage
    poweroff 0         refused -- would never wake
    (over the mesh)    refused -- serial only

Bare `poweroff` is refused rather than defaulted, so old muscle memory fails safe.

The confirmation is written straight to `Serial`, because `enterDeepSleep()` does not
return and `reply[]` would never be printed.

## Rebasing the clock after a brownout

`ESP32RTCClock::begin()` re-baselines only on a power-on reset, so a brownout that
scrambles the RTC leaves the garbage in place. Neither `time <epoch>` nor `clock sync`
will move a clock backwards, so a node that comes back reading years in the future
stays that way until NTP corrects it or someone disconnects the battery.

On `ESP_RST_BROWNOUT` this resets the clock to the same epoch a power-on would use.
It rebases unconditionally rather than judging whether the value looks wrong, because
a plausible-looking wrong date is indistinguishable from a real one: discarding a good
clock costs one re-sync, since forward corrections always work, while keeping a corrupt
one costs a site visit.

Runs before the battery check, which may sleep and never return -- the next wake
reports `ESP_RST_DEEPSLEEP` and the brownout would go unnoticed.

## Commands

Claimed ahead of upstream's `powersaving` handler, which matches on an 11-character
prefix and would otherwise answer these as a status query.

    powersaving safe              on/off, threshold, last reading
    powersaving safe.mv <mv>      set threshold; FLOOR-MAX, or 0 to disable
    powersaving safe on|off       toggle the rung, leaving the threshold alone
    powersaving auto              napping state, active flag, transition count
    powersaving auto on|off       toggle automatic napping

`safe.mv` and `safe on|off` persist in an NVS namespace of our own -- no upstream
struct to drift, no flash write on the low-battery path, and an absent key stays
distinguishable from a stored 0, which is what lets `off` persist while a board
default stays in place. They are deliberately separate, so a threshold tuned for a
site survives the rung being switched off. `auto` is runtime only; a reboot restores
the compiled default.

## Tuning

Set per board in `variants/<board>/overrides.yaml`. Defaults are what a board gets if
it says nothing.

| Flag | Default | Meaning |
| --- | --- | --- |
| `BATT_SAVER_OFF_MV` | 3800 | Stop napping at or above |
| `BATT_SAVER_ON_MV` | 3500 | Start napping at or below |
| `BATT_SAVER_AUTO_DEFAULT` | 0 | Napping auto-engage; off unless asked for |
| `BATT_SAVER_SLEEP_MV` | 0 | Hibernate below this; 0 disables the rung |
| `BATT_SAVER_SLEEP_SECS` | 60 | First hibernate wait only |
| `BATT_SAVER_SLEEP_FLOOR_MV` | 2500 | CLI floor for `safe.mv` |
| `BATT_SAVER_SLEEP_MAX_MV` | `ON_MV` | CLI ceiling; must stay under `MIN_MV` |
| `BATT_SAVER_SAMPLE_INTERVAL_MS` | 60000 | How often to read the battery |
| `BATT_SAVER_CONSECUTIVE_SAMPLES` | 3 | Agreeing readings needed to act |
| `BATT_SAVER_DROP_FEM_LNA` | 0 | Also bypass the FEM LNA while napping |
| `BOOT_PWRCHECK_MIN_MV` | 3200 | Won't return to service below this |
| `BOOT_PWRCHECK_BACKOFF_START_SECS` | 60 | First retry wait; doubles from here |
| `BOOT_PWRCHECK_RETRY_SECS` | 900 | Ceiling the backoff settles onto |
| `POWEROFF_MIN_SECS` | 60 | Reject shorter waits |
| `POWEROFF_MAX_SECS` | 86400 | Reject longer waits |

`BATT_SAVER_SLEEP_FLOOR_MV` is a board fact, not a preference -- it should sit just
above that board's measured boot floor, or the rung can never fire before a brownout
does. 2500 is right for heltec_v4 and unverified anywhere else.

The battery reading quantises to roughly 17 mV per step on heltec_v4 and depends on a
per-unit calibration (`set adc.multiplier`, defaulting to `ADC_MULTIPLIER`), so treat
any single reading as approximate.

**Contributes no suffix** -- failsafes, not features.

## What it deliberately does not do

**It never writes the saved `powersaving` setting.** That is the operator's stated
intent, and MeshCore writes it to flash on every change. Driving it directly would
make every low-battery transition a flash write, and a node that saved itself once
would come back reporting `powersaving on` forever, quietly overwriting a human's
choice.

Instead the sleep decision becomes "the operator asked for it **or** the battery is
low", and `powersaving` keeps reporting what the operator asked for.

## Dropping the LNA (off by default)

While napping the mod can also bypass the LoRa front-end LNA, and restore it on
release. **Disabled by default**, because it does not pay for itself.

Measured on a Heltec V4.3: roughly **0.3 mA** against a **~5.5 mA** napping floor --
about 5%, in exchange for around 10 dB of RX sensitivity. A repeater that hears
meaningfully less is a routing hazard: neighbours keep routing through a node that no
longer covers what it advertised, and from outside it looks like a partial fault
rather than deliberate conservation.

That ~5.5 mA is the *napping* floor -- light sleep, radio still in RX. Hibernation is
a different path and a different number.

The saving is small because the napping floor is dominated by the SX1262 sitting in
RX; the radio must stay listening to wake on a packet. The nap itself is where the
power goes: an awake ESP32-S3 draws tens of mA against that floor.

Set `BATT_SAVER_DROP_FEM_LNA: 1` to enable it anyway -- reasonable only where survival
clearly beats coverage. If enabled, the mod reads the current hardware state before
bypassing and only restores the LNA if it was the one that turned it off, so an
operator running `radio.fem.rxgain off` is not overridden. Nothing is persisted.
Boards without FEM control (xiao_c3) skip it entirely.

`radio.rxgain` (SX1262 boosted gain) and `radio.fem.rxgain` (FEM LNA) are different
settings; this mod only touches the latter.

## Enabling it

Add `power-guard` to a target's `mods:` list in `build-targets.yaml`:

```yaml
    mods: [power-guard]
```

Neither rung engages on its own: `BATT_SAVER_AUTO_DEFAULT` is 0, and
`BATT_SAVER_SLEEP_MV` is 0 unless a board states a measured value. A board whose gauge
has never been characterised stays dark.

## OTA updates are already safe

A napping node could not otherwise complete an OTA update -- `start ota wan` needs
sustained uptime and a live WiFi association, and a 30-second sleep mid-download would
break it. That is handled already, and not by this mod.

`ESP32Board::sleep()` refuses outright while `inhibit_sleep` is set, and both
upstream's `startOTAUpdate()` and our own `hotspot-ota` patch set it around the
download. Since the guard sits inside `sleep()` itself, it applies no matter who asked
for the sleep -- so a low battery cannot interrupt an update in progress.

This mod therefore carries no inhibit API of its own. It hooks into
`helpers/ModHooks.cpp` and `helpers/esp32/CommonCliMods.cpp` rather than editing
upstream files; both come from `shim/0001`. It also declares `hotspot-ota/0001` and
`0002` as context dependencies, since those edit the same regions.

## Testing

A bench supply on the battery JST is the rig, USB disconnected.

**Boot check.** Set the supply below `BOOT_PWRCHECK_MIN_MV`, boot, and watch the
current readout: a drop to the sleep floor means the sleep path was taken, and current
returning after the backoff interval proves the timer wake. Serial prints the reason
and the interval before it sleeps.

**Hibernate rung.** Leave the node running and lower the supply below
`BATT_SAVER_SLEEP_MV`. It should stay in service for
`SAMPLE_INTERVAL_MS * CONSECUTIVE_SAMPLES` before sleeping, not drop out immediately,
then hand off to the boot check's backoff.

**That a transmit does not trip it.** Hold the gauge just above `BATT_SAVER_SLEEP_MV`
and fire adverts at full power. It must stay in service.

**Napping rung.** Not yet run on hardware. Verify engage and release at the real
marks, that mesh CLI still reaches a napping node, and measured current in both
states.

## Debugging

Our heltec_v4 repeaters ship headless (`-UDISPLAY_CLASS`), so there is no screen. A
node that engages napping gives no local indication at all, and serial goes quiet --
light sleep has no serial wake source, so commands typed at it are silently dropped.
LoRa still wakes it, so the mesh side stays reachable.

Battery voltage remains visible over the mesh: it is the first field of the repeater
status response and is published as channel 1 of the telemetry data, both without
admin rights. `powersaving safe` and `powersaving auto` report the rest.
