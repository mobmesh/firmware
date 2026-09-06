#### Try a setting and have it put back

**Usage:**
- `tempset <secs> <key> <value>`
- `tempset <secs> radio <freq>,<bw>,<sf>,<cr>`
- `tempset keep`
- `tempset revert`
- `get tempset`

**Parameters:**
- `secs`: how long to keep the trial value (60-86400)
- `key`: one of `tx`, `radio.rxgain`, `radio.fem.rxgain`, `radio.fem.txgain`, `radio`
- `value`: the same value `set <key>` would take

**Examples:**
- `tempset 3600 radio.fem.rxgain on` -- an hour of FEM RX gain, then back
- `tempset 600 tx 14` then `tempset keep` -- commit it
- `tempset 300 radio 915.0,250,11,5` -- proxied to `tempradio`, kept with `tempset keep`

**Notes:**
- One trial at a time. Start a second and it is refused, naming the one already running.
- A reboot restores the original and clears the trial; a countdown is never resumed.
- `radio` trials are upstream's `tempradio` underneath, so they revert on its timer and touch no
  prefs. `tempset keep` reads the params off the live trial rather than re-typing them.
- Keys the board does not support are declined when the current value is read back.
