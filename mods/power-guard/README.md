# power-guard

Takes a repeater out of service before the battery browns it out, and brings it back
when the pack recovers. Also drops the LoRa FEM rail in deep sleep, which upstream
leaves powered: ~4 mA against ~14 uA, the difference between a solar pack climbing back
at dawn and a node flat by morning.

**Repeater only.** Nothing here has anything to switch on a room server.

## The Ladder

Every board sets its own thresholds. The heltec_v4 values below are an example, not a
default to copy -- they come from bench measurement.

The order matters: the safe mark must sit below the resume mark, or a node leaves service
and immediately qualifies to run again.

| mV | Macro | Effect |
| --- | --- | --- |
| 3600 | `POWER_GUARD_AUTO_OFF_MV` | Stops power saving |
| 3300 | `POWER_GUARD_AUTO_ON_MV` | Starts power saving |
| 3150 | `POWER_GUARD_RESUME_MV` | Won't return to service below this |
| 2800 | `POWER_GUARD_SAFE_MV` | Leaves service |
| 2400 | *(measured)* | Cannot boot below this |

Below the safe mark the node deep-sleeps. On each boot it re-checks before powering the
radio and sleeps again if it is still under the resume mark, backing off progressively,
so a pack too weak to survive radio startup cannot sit in a reset loop. Every rung waits
for several agreeing readings, so a transmit dip never triggers one.

Values are gauge millivolts, not volts at the pins.

## Commands

    powersaving safe              on/off, threshold, last reading
    powersaving safe.mv <mv>      set threshold, or 0 to disable
    powersaving safe on|off       toggle the rung, leaving the threshold alone
    powersaving auto              state, active flag, transition count
    powersaving auto on|off       toggle automatic power saving

    poweroff <secs>               deep sleep, then reboot

Upstream's `poweroff` never wakes and is reachable over the mesh; this replaces it with
a serial-only version that requires a wake time and refuses a bare invocation.

It also rebases the clock after a brownout, which upstream leaves scrambled until NTP
or a battery pull corrects it.

## Enabling

Add `power-guard` to a target's `mods:` in `build-targets.yaml`, then set the board's
thresholds in `variants/<board>/overrides.yaml`. **Every voltage defaults to 0, meaning
off** -- a threshold is a fact about a board's divider and measured boot floor, so a
board that states nothing gets a mod that does nothing. Power saving additionally needs
`POWER_GUARD_AUTO_DEFAULT` or `powersaving auto on`.

Optionally bypasses the FEM LNA while power saving
(`POWER_GUARD_AUTO_DROP_FEM_LNA: 1`, off by default): ~0.3 mA saved for ~10 dB of RX
sensitivity, which usually is not worth it -- a repeater that hears less than it
advertises is a routing hazard.

Requires the `battery_measurement` capability; `fem_lna_control` and
`deep_sleep_rail_shutdown` are optional.
