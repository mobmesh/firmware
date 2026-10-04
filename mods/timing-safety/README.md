# timing-safety

## Small Clock and Timer Fixes for MeshCore

This mod fixes two time-related problems in MeshCore:

| When | Visible effect | Fix |
| --- | --- | --- |
| After many weeks of uptime, an internal timer rolls over. | Screen refreshes or GPS checks may fire at the wrong time. | Compare elapsed time correctly across rollover. |
| After reboot, the clock resets but saved neighbor timestamps remain. | A neighbor's “last heard” age may show a nonsense value. | Show `0` until the age can be calculated reliably. |

## Why This Exists

Both fixes remain unmerged upstream (PRs 1972 and 1349). Their status is tracked in
`patches/0001.meta.yaml` and checked daily by `patch-drift-canary`.

The second upstream PR also removes the `time` command's guard against setting the clock
backward. This mod keeps that guard. `hotspot-ota` can already correct the clock in either
direction through NTP when it joins WiFi, without changing the rule for manual commands.
