# boot-pwrcheck - Never Strand a Node

Two guards against a repeater that cannot recover without a site visit. Both use
`ESP32Board::enterDeepSleep(secs)` with a non-zero time, which powers the radio off
and reboots on the timer.

## 0001 - low-battery boot sleep

Runs at the top of `modRadioInit()`, before `radio_init()` powers the radio up. If
the battery is below `BOOT_PWRCHECK_MIN_MV`, the node deep-sleeps and reboots to
check again, backing off 60s, 120s, 300s, 600s, then `BOOT_PWRCHECK_RETRY_SECS`.
A check that passes resets the backoff, so a brief dip costs a minute rather than
inheriting a long interval from an earlier depletion.

The retry counter lives in RTC slow memory, so nothing is written to flash at low
voltage. Loss or corruption falls back to the shortest interval.

Without it, a pack too weak to survive radio startup browns out mid-init and resets,
forever: each cycle spends charge and reaches nothing. Deep sleep drops draw to
3-4 mA on heltec_v4, against 20-54 mA for a reset loop.

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
| `BOOT_PWRCHECK_RETRY_SECS` | 900 | Ceiling the backoff settles onto |
| `POWEROFF_MIN_SECS` | 60 | Reject shorter waits |
| `POWEROFF_MAX_SECS` | 86400 | Reject longer waits |

`BOOT_PWRCHECK_MIN_MV` should sit below `BATT_SAVER_ON_MV` (3300 on heltec_v4) so
power saving gets a chance to extend runtime before the node stops entirely.

**Contributes no suffix** -- a failsafe, not a feature.

## Testing

A bench supply on the battery JST is the rig. Set it below `BOOT_PWRCHECK_MIN_MV`,
boot, and watch the supply's current readout: a drop to the few-mA sleep floor means
the sleep path was taken, and current returning after the backoff interval proves the
timer wake. Serial prints the reason and the interval before it sleeps.

The check is boot-only, so lowering the supply under a running node does nothing --
it has to be rebooted to hand control to the guard.
