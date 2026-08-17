# batt-saver TODO

Open work. The mod ships on heltec_v4 repeater as of 2026-08-16.

## 1. Persisted `auto` state (open)

`powersaving auto on|off` is runtime only, so a reboot returns it to the compiled
default. `safe` persists both its threshold and its on/off flag; `auto` does not.
Decide whether that asymmetry is wanted -- a runtime-only override cannot strand a
node, but it also means an operator's choice is lost on every reset.

## 3. Compile it in CI -- done

`heltec_v4 repeater` lists the mod, so CI applies and builds it.

## 5. Bench-test the napping rung (open)

The deep-sleep rung and the CLI were tested on board 1 (2026-08-16). The napping
rung still has not run: verify engage/release at 3300/3600, that mesh CLI reaches
a napping node, and measured current in both states.
