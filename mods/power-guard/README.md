# power-guard - Never Strand a Node

**A repeater that stops to save itself draws microamps, not milliamps.**

Upstream's deep sleep leaves the LoRa front-end rail powered -- about **4 mA**, which
is a node spending its last charge on retries that reach nothing. Cut that rail and it
is **~14 uA**, three hundred times lower, so the reserve a solar pack needs to climb
back at dawn is still there in the morning.

Everything this project does about power: when a node conserves, when it stops
relaying, when it comes back, and what happens if it browns out anyway.

**Repeater only, for now.** The power saving rung has nothing to switch on a room server
-- that firmware has no sleep path. Everything else would work there, and would
inherit the board's thresholds. Untested, not impossible.

## The Ladder

What happens as a heltec_v4 repeater's battery falls, highest to lowest:

| mV | Name | What happens |
| --- | --- | --- |
| 3600 | `POWER_GUARD_AUTO_OFF_MV` | Stops power saving |
| 3300 | `POWER_GUARD_AUTO_ON_MV` | Starts power saving |
| 3150 | `POWER_GUARD_RESUME_MV` | Won't come back into service below this |
| 2800 | `POWER_GUARD_SAFE_MV` | Leaves service |
| 2400 | *(measured)* | Can't boot at all below this |

The ordering is the contract. The safe mark must sit under the return-to-service one,
or a node leaves service and immediately qualifies to run again -- alive, but only
between sleeps. `POWER_GUARD_SAFE_MAX_MV` (3149) is the CLI ceiling that enforces it.

All values are gauge millivolts, not volts at the pins. The two differ by roughly
30 mV at 3.7 V and 200 mV at 2.6 V on this board, and the boot floor is measured the
same way, so comparing gauge against gauge keeps the error out of the margin.

## Automatic power saving

MeshCore already has a power-saving mode: short sleeps whenever the node has nothing
to do, waking immediately on a packet. It is a manual switch. This turns it on and
off from the battery instead -- below `POWER_GUARD_AUTO_ON_MV`, off again above
`POWER_GUARD_AUTO_OFF_MV`.

**Off by default.** Light sleep drops serial and thins RX, so it waits for
`powersaving auto on`.

The two marks are far apart, and it insists on several agreeing readings rather than
one, for the same reason: **power saving changes the reading.** A sleeping node draws
less, so measured voltage rises the moment saving engages. Close marks would make it
decide it had recovered, wake, sag, and oscillate. Transmitting does the same in
reverse -- a burst pulls voltage down briefly, and that dip is not a flat battery.

## Leaving service

Below `POWER_GUARD_SAFE_MV` the node leaves service entirely and deep-sleeps.
Power saving stretches runtime; this stops spending it, keeping the reserve a solar pack
needs to climb back. With the rail down (see below) deep sleep draws around 14 uA
against 20-54 mA for a reset loop, and at dawn the panel has to beat whichever state
the node is in -- a difference of three orders of magnitude, not a margin.

Sampled from `modLoop()`, and it takes `POWER_GUARD_CONSECUTIVE_SAMPLES` agreeing
readings. That count is what keeps a transmit from triggering it: a dip lasts well
under a second against a 60 s sampler. Measured on board 1 at 45 mV above the
threshold with adverts every 5 s -- 13% of samples fell below it, never three in a
row, 41 transmits and no sleep.

It sleeps `POWER_GUARD_SAFE_SECS` once and the boot check owns the schedule after,
so there is one retry ladder rather than two.

## Checking at boot

Runs at the top of `modRadioInit()`, before `radio_init()` powers the radio up. Below
`POWER_GUARD_RESUME_MV` the node deep-sleeps and reboots to check again, doubling from
`POWER_GUARD_BACKOFF_START_SECS` until it reaches `POWER_GUARD_BACKOFF_MAX_SECS`. A
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

## Powering down before sleep

`enterDeepSleep()` handles the SX1262 driver and NSS, but nothing board-specific, so
a stock sleep leaves the FEM rail powered. That cost **4 mA**, measured -- a node too
flat to run was spending its remaining charge on every retry, which is the failure
this mod exists to prevent, just slower.

**With the rail down the figure is ~14 uA**, roughly three hundred times lower.
Heltec's own community forum has the same result: a board measured at 300 uA of
residual peaks in deep sleep dropped to about 13 uA once the FEM was switched off at
GPIO7, which is the step upstream's sleep path does not take. Our own meter stops at
milliamps, so we can confirm the drop but not the endpoint -- see Measured below.

`variants/heltec_v4/PowerGuard.h` drops it. Board hardware lives there, beside the
`LoRaFEMControl` it drives; the mod's policy stays board-independent. A board that
ships no `PowerGuard.h` keeps the stock sleep, and needs no placeholder macros to say
so -- the include is gated on the board defining `P_LORA_PA_POWER`.

The FEM has its own supply (`TLV75733PDBVR`, schematic U3, output `Vfem`). Its enable
pin carries a 5.1M pull-up to `VDD_3V3`, so at sleep entry the pad driver powers down
and the pull-up switches the rail back on. It has to be driven low **and latched**,
not merely written low.

Two entry points, because the two sleep paths differ in one decisive way:

| | radio state | how it is slept |
| --- | --- | --- |
| `powerGuardDownPreRadio()` | `radio_init()` has not run | `SetSleep` by hand |
| `powerGuardDownPostRadio()` | already initialised | `radio_driver.powerOff()` |

The boot check runs before `radio_init()`, so upstream's `powerOff()` silently does
nothing there -- the SPI bus was never begun. We issue `SetSleep` (0x84) directly,
which is valid straight from `STDBY_RC` after reset and needs no TCXO, calibration or
PLL.

**Both paths sleep the radio and latch NSS themselves, before calling
`enterDeepSleep()`.** That is deliberate: a falling edge on NSS is exactly what wakes
an SX126x, and RadioLib strobes CS low before any transfer whether or not its bus was
begun. Latching first means `enterDeepSleep()`'s own `powerOff()` and hold are
harmlessly blocked -- the chip is already asleep -- so the order it does things in
stops being something this mod depends on.

Both holds are released here too, first thing in the boot check, on every boot
whatever the reset reason. `LoRaFEMControl::init()` and `HeltecV4Board::begin()` have
both already released by then, which makes ours no-ops today and correct if that ever
changes. Every hold has exactly one release; a missed one leaves a node that
transmits and is never heard.

### Vext is deliberately left alone

An earlier version also dropped Vext, on the understanding that it fed a PE4259 RF
switch. It does not -- that part belonged to the V2.1 boards. On V4 the Vext rail
powers the onboard OLED, which this target builds out (`-UDISPLAY_CLASS`), and the
`Ve` header pins for external sensors.

Cutting a rail whose loads this mod cannot know about is the wrong default. A sensor
may need warm-up, may hold state, may want a defined power sequence -- and breaking
one that way looks like a flaky sensor rather than like us. MeshCore already has the
right home for it: `enterDeepSleep()` stops the GPS through the sensor framework,
which knows what is attached.

If a deployment puts peripherals on `Ve` and wants them powered down while asleep,
that belongs in the sensor framework, not in a blind `digitalWrite` here.

Removing it also retired the mod's only non-RTC hold. `P_LORA_PA_POWER` and
`P_LORA_NSS` are both in the S3's RTC range, so one mechanism covers both, and the
question of whether `gpio_deep_sleep_hold_en()` latches survive a wake reset stopped
mattering.

### Measured

Sleep current fell from 4 mA to below the resolution of a mA-range meter, with the
same supply voltage and the same command either side, so the only variable was the
rail power-down.

The endpoint is unmeasured here. Independent reports for this board, using a meter
that reaches microamps, put it at 13-14 uA once the FEM rail is cut. That is
consistent with what we can see but is not our number, and should not be quoted as
though it were.

The radiated path was checked with a second node rather than trusting `advert`'s
return value, which only means the packet was queued:

    baseline                     SNR 48
    after post-radio power-down  SNR 50
    after pre-radio power-down   SNR 49

with the neighbour entry cleared to `-none-` between each, so a stale row could not
read as success. A latched-off PA or a radio left asleep would have shown nothing.

FEM auto-detection was compared across a deep-sleep wake and a normal boot -- the
first skips the startup delay `LoRaFEMControl::init()` applies -- and did not shift,
so the LDO settles well inside that margin despite C15/C16 on its output.

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

It shares the rail power-down above, so a deliberate day-long sleep gets the
same floor as the boot check rather than spending 4 mA for 24 hours.

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
    powersaving auto              power saving state, active flag, transition count
    powersaving auto on|off       toggle automatic power saving

`safe.mv` and `safe on|off` persist in a separate NVS namespace -- no upstream
struct to drift, no flash write on the low-battery path, and an absent key stays
distinguishable from a stored 0, which is what lets `off` persist while a board
default stays in place. They are deliberately separate, so a threshold tuned for a
site survives the rung being switched off. `auto` is runtime only; a reboot restores
the compiled default.

## Tuning

Set per board in `variants/<board>/overrides.yaml`. The values below are what
heltec_v4 ships, measured on that board's divider -- they are not recommendations for
any other.

**Every voltage defaults to 0, meaning off.** A threshold is a fact about a board's
divider and its measured boot floor, so there is no sensible default: a board that
says nothing gets a mod that does nothing, rather than one quietly running someone
else's numbers.

### Automatic power saving

| Flag | heltec_v4 | Meaning |
| --- | --- | --- |
| `POWER_GUARD_AUTO_ON_MV` | 3300 | Start power saving at or below; 0 disables the rung |
| `POWER_GUARD_AUTO_OFF_MV` | 3600 | Stop power saving at or above |
| `POWER_GUARD_AUTO_DEFAULT` | 0 | Auto-engage; off unless asked for |
| `POWER_GUARD_AUTO_DROP_FEM_LNA` | 0 | Also bypass the FEM LNA while power saving |

### Leaving service

| Flag | heltec_v4 | Meaning |
| --- | --- | --- |
| `POWER_GUARD_SAFE_MV` | 2800 | Leave service below this; 0 disables the rung |
| `POWER_GUARD_SAFE_FLOOR_MV` | 2500 | CLI floor for `safe.mv`; 0 means unenforced |
| `POWER_GUARD_SAFE_MAX_MV` | *derived* | CLI ceiling, `RESUME_MV - 1` |
| `POWER_GUARD_SAFE_SECS` | 60 | First wait only, before the boot check takes over |

### Checking at boot

| Flag | heltec_v4 | Meaning |
| --- | --- | --- |
| `POWER_GUARD_RESUME_MV` | 3150 | Won't return below this; 0 disables the boot check |
| `POWER_GUARD_BACKOFF_START_SECS` | 60 | First retry wait; doubles from here |
| `POWER_GUARD_BACKOFF_MAX_SECS` | 900 | Ceiling the backoff settles onto |

### Sampling

Shared by both rungs. Together they are the debounce window -- three readings a minute
apart -- which is what stops a transmit's voltage sag reading as a flat pack.

| Flag | heltec_v4 | Meaning |
| --- | --- | --- |
| `POWER_GUARD_SAMPLE_INTERVAL_MS` | 60000 | How often to read the battery |
| `POWER_GUARD_CONSECUTIVE_SAMPLES` | 3 | Agreeing readings needed to act |

### `poweroff` bounds

| Flag | heltec_v4 | Meaning |
| --- | --- | --- |
| `POWER_GUARD_POWEROFF_MIN_SECS` | 60 | Reject shorter waits |
| `POWER_GUARD_POWEROFF_MAX_SECS` | 86400 | Reject longer waits |

`POWER_GUARD_SAFE_MAX_MV` is derived rather than set, at `RESUME_MV - 1`. That is
the invariant -- a leave service threshold at or above the return-to-service mark leaves
a node leaving service and immediately qualifying to run again -- so deriving it means a
board cannot break it by omission.

`POWER_GUARD_SAFE_FLOOR_MV` should sit just above that board's measured boot floor,
or the CLI would accept a leave service threshold the node can never act on before a
brownout does. 2500 is right for heltec_v4 and unverified anywhere else.

The battery reading quantises to roughly 17 mV per step on heltec_v4 and depends on a
per-unit calibration (`set adc.multiplier`, defaulting to `ADC_MULTIPLIER`), so treat
any single reading as approximate.

**Contributes no suffix** -- failsafes, not features.

## What it deliberately does not do

**It never writes the saved `powersaving` setting.** That is the operator's stated
intent, and MeshCore writes it to flash on every change. Driving it directly would
make every low-battery transition a flash write, and a node that saved itself once
would come back reporting `powersaving on` forever, quietly overwriting the operator's
choice.

Instead the sleep decision becomes "the operator asked for it **or** the battery is
low", and `powersaving` keeps reporting what the operator asked for.

## Dropping the LNA (off by default)

While power saving the mod can also bypass the LoRa front-end LNA, and restore it on
release. **Disabled by default**, because it does not pay for itself.

Measured on a Heltec V4.3: roughly **0.3 mA** against a **~5.5 mA** power saving floor --
about 5%, in exchange for around 10 dB of RX sensitivity. A repeater that hears
meaningfully less is a routing hazard: neighbours keep routing through a node that no
longer covers what it advertised, and from outside it looks like a partial fault
rather than deliberate conservation.

That ~5.5 mA is the *power saving* floor -- light sleep, radio still in RX. Leaving
service is a different path and a different number.

The saving is small because the power saving floor is dominated by the SX1262 sitting in
RX; the radio must stay listening to wake on a packet. The sleep itself is where the
power goes: an awake ESP32-S3 draws tens of mA against that floor.

Set `POWER_GUARD_AUTO_DROP_FEM_LNA: 1` to enable it anyway -- reasonable only where survival
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

Adding it to a target is not enough on its own -- every voltage defaults to 0, so a
board that states no thresholds gets a mod that does nothing. Power saving needs
`POWER_GUARD_AUTO_DEFAULT` or `powersaving auto on` as well. A board whose gauge
has never been characterised stays dark.

## OTA updates are already safe

A power saving node could not otherwise complete an OTA update -- `start ota wan` needs
sustained uptime and a live WiFi association, and a 30-second sleep mid-download would
break it. That is handled already, and not by this mod.

`ESP32Board::sleep()` refuses outright while `inhibit_sleep` is set, and both
upstream's `startOTAUpdate()` and `hotspot-ota` set it around the
download. Since the guard sits inside `sleep()` itself, it applies no matter who asked
for the sleep -- so a low battery cannot interrupt an update in progress.

This mod therefore carries no inhibit API of its own. Its integration file owns the
pre-radio, loop, power-saving, and CLI contributions. The shim generator wires those
contributions into its aggregates without a patch or a dependency on hotspot OTA.

## Testing

A bench supply on the battery JST is the rig, USB disconnected.

**Boot check.** Set the supply below `POWER_GUARD_RESUME_MV`, boot, and watch the
current readout: a drop to the sleep floor means the sleep path was taken, and current
returning after the backoff interval proves the timer wake. Serial prints the reason
and the interval before it sleeps.

**Safe rung.** Leave the node running and lower the supply below
`POWER_GUARD_SAFE_MV`. It should stay in service for
`SAMPLE_INTERVAL_MS * CONSECUTIVE_SAMPLES` before sleeping, not drop out immediately,
then hand off to the boot check's backoff.

**That a transmit does not trip it.** Hold the gauge just above `POWER_GUARD_SAFE_MV`
and fire adverts at full power. It must stay in service.

**Power saving rung.** Not yet run on hardware. Verify engage and release at the real
marks, that mesh CLI still reaches a power saving node, and measured current in both
states.

## Debugging

Our heltec_v4 repeaters ship headless (`-UDISPLAY_CLASS`), so there is no screen. A
node that engages power saving gives no local indication at all, and serial goes quiet --
light sleep has no serial wake source, so commands typed at it are silently dropped.
LoRa still wakes it, so the mesh side stays reachable.

Battery voltage remains visible over the mesh: it is the first field of the repeater
status response and is published as channel 1 of the telemetry data, both without
admin rights. `powersaving safe` and `powersaving auto` report the rest.
