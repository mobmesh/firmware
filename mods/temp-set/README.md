# temp-set

`tempset <secs> <key> <value>` applies a setting, then puts it back unless you confirm with
`tempset keep`. For the settings that can make a node unreachable, or whose worth can only be
judged at the site it is deployed to.

Upstream already owns the radio-parameter case: `tempradio` applies `freq,bw,sf,cr` live,
reverts on its own timer, and writes nothing to prefs, so a reboot mid-trial restores the saved
config. This mod covers what `tempradio` does not.

## Commands

    tempset <secs> <key> <value>    snapshot, apply, start the clock (60s minimum, 86400s max)
    tempset <secs> radio f,bw,sf,cr proxied to upstream's tempradio
    tempset keep                    commit the running trial
    tempset revert                  put it back now
    get tempset                     what is running, and for how long

## Keys

`tx`, `radio.rxgain`, `radio.fem.rxgain`, `radio.fem.txgain`, and `radio` via the proxy.

An allowlist, not a denylist: a key earns its place by affecting whether the node can be
reached, or by being site-dependent enough that it has to be judged where the node lives.

## Two things worth knowing

**A reboot ends a trial, it never resumes one.** An unscheduled restart is most likely a
brownout, and a brownout leaves the RTC deadline the trial was counting against unverifiable.
A slot found at boot is reverted, not resumed.

**`tempset keep` on radio reads upstream's live state, not a copy.** Upstream clears only the
timer when a tempradio trial reverts -- `pending_*` keep the expired values indefinitely -- so
keep consults both timers via `modTempRadioGet()` and refuses unless the params are actually on
the air. Committing a lapsed trial would be worst exactly when it is most tempting: if the
params were bad you lost contact and got it back only when the revert fired.

## Per-board keys

`radio.fem.*` exists only where the board can control the FEM (`fem_lna_control`). Upstream
answers `Error: unsupported` on both the read and the write there, so the snapshot fails and the
trial is declined -- one binary serves every variant.

`radio.rxgain` needs its own care: upstream saves the pref *before* testing whether the board
supports it, so a failed apply is followed here by an explicit restore rather than left as-is.
