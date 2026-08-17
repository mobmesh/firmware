# boot-pwrcheck - Never Strand a Node

Guards against a repeater that cannot recover without a site visit. The sleep paths
use `ESP32Board::enterDeepSleep(secs)` with a non-zero time, which powers the radio
off and reboots on the timer.

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

## 0003 - brownout clock rebase

`ESP32RTCClock::begin()` re-baselines only on a power-on reset, so a brownout that
scrambles the RTC leaves the garbage in place. Neither `time <epoch>` nor `clock sync`
will move a clock backwards, so a node that comes back reading years in the future
stays that way until NTP corrects it or someone disconnects the battery.

On `ESP_RST_BROWNOUT` this resets the clock to the same epoch a power-on would use.
It rebases unconditionally rather than judging whether the value looks wrong, because
a plausible-looking wrong date is indistinguishable from a real one: discarding a good
clock costs one re-sync, since forward corrections always work, while keeping a corrupt
one costs a site visit.

Runs before the battery check, which may sleep and never return -- the next wake
reports `ESP_RST_DEEPSLEEP` and the brownout would go unnoticed.

## 0004 - runtime low-battery sleep

The boot check only runs at boot, so a draining node stays in service until it browns
out. That is survivable now, but it spends the reserve a solar pack needs to climb
back: deep sleep draws 3-4 mA while a reset loop draws 20-54 mA, and at dawn the panel
has to beat whichever one the node is in.

From `modLoop()`, samples the battery every `RUNTIME_SLEEP_CHECK_SECS` and deep-sleeps
after `RUNTIME_SLEEP_CONSECUTIVE` readings below `RUNTIME_SLEEP_MV`. The boot check
owns the backoff from the first wake, so there is one retry schedule, not two.

The consecutive count is what stops a transmit triggering it -- a dip lasts under a
second against a 30s sampler. Measured at 45 mV above the threshold with adverts every
5s: 12% of samples fell below it, none four in a row, 60 transmits with no sleep.

`RUNTIME_SLEEP_MV` defaults to 0, which disables the check, so a board only gets this
once its gauge has been characterised.

## Tuning

| Flag | Default | Meaning |
| --- | --- | --- |
| `BOOT_PWRCHECK_MIN_MV` | 3200 | Sleep at boot below this |
| `BOOT_PWRCHECK_RETRY_SECS` | 900 | Ceiling the backoff settles onto |
| `RUNTIME_SLEEP_MV` | 0 | Leave service below this while running; 0 disables |
| `RUNTIME_SLEEP_CHECK_SECS` | 30 | Sampling interval for the runtime check |
| `RUNTIME_SLEEP_CONSECUTIVE` | 4 | Consecutive low readings before sleeping |
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

To exercise 0004 instead, leave the node running and lower the supply below
`RUNTIME_SLEEP_MV`: it should stay in service for
`RUNTIME_SLEEP_CHECK_SECS * RUNTIME_SLEEP_CONSECUTIVE` before sleeping, not drop out
immediately. With `RUNTIME_SLEEP_MV` at 0 the boot check is the only one that fires,
so the supply has to be lowered and the node rebooted.
