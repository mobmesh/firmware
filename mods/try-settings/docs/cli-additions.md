#### Try a setting and have it put back

**Usage:**
- `tryset <secs> <key> <value>`
- `tryset <secs> radio <freq>,<bw>,<sf>,<cr>`
- `tryset keep`
- `tryset revert`
- `get tryset`

**Parameters:**
- `secs`: how long to keep the trial value (60-86400)
- `key`: one of `tx`, `radio.rxgain`, `radio.fem.rxgain`, `radio.fem.txgain`, `int.thresh`,
  `cad`, `agc.reset.interval`, `flood.max`, `flood.max.unscoped`, `flood.max.advert`,
  `repeat`, `dutycycle`, or `radio` for the proxy
- `value`: the same value `set <key>` would take

**Examples:**
- `tryset 3600 radio.fem.rxgain on` -- an hour of FEM RX gain, then back
- `tryset 600 tx 14` then `tryset keep` -- commit it
- `tryset 300 radio 915.0,250,11,5` -- proxied to `tempradio`, kept with `tryset keep`

**Notes:**
- One trial at a time, across both mechanisms. A `radio` trial and a key trial cannot overlap;
  starting either while the other runs is refused, naming the one already running.
- `radio` trials take whole minutes, since `tempradio` counts in minutes. A duration that is
  not a multiple of 60 is refused rather than silently floored.
- A reboot restores the original and clears the trial; a countdown is never resumed.
- `radio` trials are upstream's `tempradio` underneath, so they revert on its timer and touch no
  prefs. `tryset keep` reads the params off the live trial rather than re-typing them.
- Keys the board does not support are declined when the current value is read back.
