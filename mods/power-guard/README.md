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

## CLI Commands

<table>
<thead><tr><th align="left">Command</th><th align="left">What it does</th></tr></thead>
<tbody>
<tr><td><code>powersaving safe</code></td><td>Show on/off, threshold, and last reading.</td></tr>
<tr><td><code>powersaving safe.mv &lt;mv&gt;</code></td><td>Set the threshold, or <code>0</code> to disable.</td></tr>
<tr><td><code>powersaving safe on|off</code></td><td>Toggle the failsafe, leaving the threshold alone.</td></tr>
<tr><td><code>powersaving auto</code></td><td>Show state, active flag, and transition count.</td></tr>
<tr><td><code>powersaving auto on|off</code></td><td>Toggle automatic power saving.</td></tr>
<tr><td><code>poweroff &lt;secs&gt;</code></td><td>Deep sleep for 60 to 86400 seconds, then wake. Works over the mesh too.</td></tr>
<tr><td><code>stats-core</code></td><td>Battery, uptime, errors, queue length.</td></tr>
<tr><td><code>stats-radio</code></td><td>Noise floor, RSSI/SNR, airtime.</td></tr>
<tr><td><code>stats-packets</code></td><td>Packet counters.</td></tr>
</tbody>
</table>

Upstream's `poweroff` never wakes. This replaces it with a version that requires a wake time
and refuses a bare `poweroff`. Over the mesh, the reply goes out first and the node sleeps
5 seconds later. Builds without this mod refuse `poweroff` altogether.

The three `stats-` commands are upstream's own, gated to serial there. This mod forwards
them when they arrive over the mesh, so a remote operator can read a battery level without
a site visit. The remote CLI path is already authenticated and the replies only read state.

It also rebases the clock after a brownout, which upstream leaves scrambled until NTP
or a battery pull corrects it.

Where [`hotspot-ota`](../hotspot-ota) is also installed, the hotspot modem is not powered
while the battery is below the resume mark, so an update, WAN check or clock refresh cannot
add the load that browns the node out. power-guard answers through a shim hook; neither mod
depends on the other, and without power-guard the hotspot is never held back.

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
