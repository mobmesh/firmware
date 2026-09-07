# try-settings

`tryset <secs> <key> <value>` applies a setting, then puts it back unless you confirm with
`tryset keep`. For the settings that can make a node unreachable, or whose worth can only be
judged at the site it is deployed to.

MeshCore itself already owns the radio-parameter case: its `tempradio` applies `freq,bw,sf,cr` live,
reverts on its own timer, and writes nothing to prefs, so a reboot mid-trial restores the saved
config. This mod covers what `tempradio` does not.

## Commands

    tryset <secs> <key> <value>    snapshot, apply, start the clock (60s minimum, 86400s max)
    tryset <secs> radio f,bw,sf,cr proxied to MeshCore's own tempradio
    tryset keep                    commit the running trial
    tryset revert                  put it back now
    get tryset                     what is running, and for how long

## Keys

`tx`, `radio.rxgain`, `radio.fem.rxgain`, `radio.fem.txgain`, and `radio` via the proxy.

An allowlist, not a denylist: a key earns its place by affecting whether the node can be
reached, or by being site-dependent enough that it has to be judged where the node lives.

## Two things worth knowing

**A reboot ends a trial, it never resumes one.** An unscheduled restart is most likely a
brownout, and a brownout leaves the RTC deadline the trial was counting against unverifiable.
A slot found at boot is reverted, not resumed.

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
