# boot-pwrcheck - Never Strand a Node

Two guards against a repeater that cannot recover without a site visit. Both use
`ESP32Board::enterDeepSleep(secs)` with a non-zero time, which powers the radio off
and reboots on the timer.

## 0001 - low-battery boot sleep

Runs at the top of `modRadioInit()`, before `radio_init()` powers the radio up. If
the battery is below `BOOT_PWRCHECK_MIN_MV`, the node sleeps for
`BOOT_PWRCHECK_RETRY_SECS` and reboots to check again.

Without it, a pack too weak to survive radio startup browns out mid-init and resets,
forever: each cycle spends charge and reaches nothing. Deep sleep drops draw from the
~5.5 mA listening floor to microamps, which is what lets a solar pack climb back.

A reading of 0 mV is treated as "no battery attached" (USB-powered, divider reads
nothing) and is ignored.

## 0002 - poweroff requires a wake time

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

## Tuning

| Flag | Default | Meaning |
| --- | --- | --- |
| `BOOT_PWRCHECK_MIN_MV` | 3200 | Sleep at boot below this |
| `BOOT_PWRCHECK_RETRY_SECS` | 900 | How long to sleep before re-checking |
| `POWEROFF_MIN_SECS` | 60 | Reject shorter waits |
| `POWEROFF_MAX_SECS` | 86400 | Reject longer waits |

`BOOT_PWRCHECK_MIN_MV` should sit below `BATT_SAVER_ON_MV` (3300 on heltec_v4) so
power saving gets a chance to extend runtime before the node stops entirely.

**Contributes no suffix** -- a failsafe, not a feature.

## Testing

A bench supply on the battery JST is the rig. Set it below `BOOT_PWRCHECK_MIN_MV`,
boot, and watch the supply's current readout: a drop to microamps means the sleep
path was taken, and current returning after `BOOT_PWRCHECK_RETRY_SECS` proves the
timer wake. Serial prints the reason before it sleeps.
