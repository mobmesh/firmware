# try-settings

`tryset <secs> <key> <value>` applies a setting, then puts it back unless you confirm with
`tryset keep`. For the settings that can make a node unreachable, or whose worth can only be
judged at the site it is deployed to.

MeshCore itself already owns the radio-parameter case: its `tempradio` applies `freq,bw,sf,cr` live,
reverts on its own timer, and writes nothing to prefs, so a reboot mid-trial restores the saved
config. This mod covers what `tempradio` does not.

## CLI Commands

<table>
<thead><tr><th align="left">Command</th><th align="left">What it does</th></tr></thead>
<tbody>
<tr><th colspan="2" align="left">🧪 Start a trial</th></tr>
<tr><td><code>tryset &lt;secs&gt; &lt;key&gt; &lt;value&gt;</code></td><td>Snapshot, apply, and start the clock (60s minimum, 86400s maximum).</td></tr>
<tr><td><code>tryset &lt;secs&gt; radio f,bw,sf,cr</code></td><td>Proxied to MeshCore's own <code>tempradio</code>.</td></tr>
<tr><th colspan="2" align="left">✅ Finish a trial</th></tr>
<tr><td><code>tryset keep</code></td><td>Commit the running trial.</td></tr>
<tr><td><code>tryset revert</code></td><td>Put it back now.</td></tr>
<tr><th colspan="2" align="left">🔎 Status</th></tr>
<tr><td><code>get tryset</code></td><td>What is running, and for how long.</td></tr>
</tbody>
</table>

## Keys

Gain and power, where nothing temporary exists otherwise:
`tx`, `radio.rxgain`, `radio.fem.rxgain`, `radio.fem.txgain`.

Site RF, which `Dispatcher::loop()` re-reads so a trial takes hold within a calibration tick:
`int.thresh`, `cad`, `agc.reset.interval`.

Strand risk -- these can cut a node off from the far mesh while it still answers up close:
`flood.max`, `flood.max.unscoped`, `flood.max.advert`, `repeat`, `dutycycle`.

Plus `radio` via the proxy, which is upstream's `tempradio` underneath.

An allowlist, not a denylist: a key earns its place by affecting whether the node can be
reached, or by being site-dependent enough that it has to be judged where the node lives.

## Two things worth knowing

**A reboot ends a trial, it never resumes one.** An unscheduled restart is most likely a
brownout, and the trial's countdown does not survive a restart. A slot found at boot is
reverted, not resumed. The countdown runs on the node's uptime, not its clock, so setting or
correcting the clock during a trial neither shortens nor extends it.

**`tryset keep` only commits radio settings while the trial is still running.** Once the
trial has lapsed, keep is refused and you have to start another one. That refusal is the
guarantee: to commit the settings you must be talking to the node *on those settings*, which
proves it is still reachable with them. Committing after a lapse would prove nothing -- if the
params were bad you lost contact and got it back only because the trial reverted.

## Per-board keys

`radio.fem.*` exists only where the board can control the FEM (`fem_lna_control`). MeshCore
answers `Error: unsupported` on both the read and the write there, so the snapshot fails and the
trial is declined -- one binary serves every variant.

`radio.rxgain` needs its own care: MeshCore saves the pref *before* testing whether the board
supports it, so a failed apply is followed here by an explicit restore rather than left as-is.
