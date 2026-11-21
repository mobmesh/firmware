# rx-duty-cycle

[↩ Back to firmware readme](../../README.md)

While the node runs power saving, the SX1262 sleeps between short listen windows instead of receiving continuously, waking inside each packet's preamble. On a Heltec V4 repeater (LNA off, power saving on) idle draw fell from 8.4 mA to 4.8 mA.

- `set radio.rxdc auto` (default) runs it whenever power saving does: the operator's `powersaving on`, or a mod that asks for it (power-guard on a low battery). `on` runs it whatever power saving says; `off` keeps continuous receive.
- With `radio.fem.rxdc` on (the default), boards with a KCT8103L FEM (Heltec V4.3) also bypass the LNA while the radio sleeps: during CPU light sleep the ESP32-S3's ULP coprocessor copies the radio's BUSY pin onto the FEM's CTX pin, and hands the pin back on every wake, so it never owns the FEM while the CPU can transmit. Heltec V4.3 repeater, power saving and LNA on: 15.0 mA to 9.5 mA. `set radio.fem.rxdc off` leaves the LNA as set.
- Sleep is the preamble less 10.2 symbols still to catch. The listen window starts from the SX126x datasheet's timer rule and is lengthened to the SX1262's measured post-preamble timer, so preamble, sync and header always fit.
- At SF7 / 62.5 kHz: listen ~28 ms, sleep 44.6 ms. Radio settings with no safe timing (SF7 at 500 kHz, for example) keep continuous receive.
- The cycle stops before every send, channel scan and AGC reset, and re-arms afterwards. Three failed arms in a row fall back to continuous receive; the radio is never left deaf.
- The noise floor is measured only from continuous receive: once a minute, and after every AGC reset, the radio receives continuously until the next floor is published.
- Instantaneous RSSI readings by other mods (diag-reports' noise watch and LNA auto) briefly switch to continuous receive, so they read the true floor.
- SX1262 boards only; the build refuses this mod on a board without the `sx1262_radio` capability.

## CLI Commands

Detailed syntax and responses are in the [rx-duty-cycle CLI reference](docs/cli-additions.md).

[↩ Back to firmware readme](../../README.md)
