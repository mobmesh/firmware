# timing-safety - Small Clock and Timer Fixes for MeshCore

This mod fixes two small but real bugs in how MeshCore keeps track of time.

The first bug shows up if a device stays powered on for a long time (many weeks). The internal timer it uses for things like screen refreshes and GPS checks can roll over and briefly get confused about whether time has passed. This patch fixes the comparison so it keeps working correctly no matter how long the device has been running.

The second bug shows up after a reboot. These boards don't have a battery-backed clock chip, so every time the device restarts, its clock resets to a placeholder date. If the device already remembered when it last heard from its neighbors, that "how long ago" math can come out wildly wrong right after a reboot, showing a nonsense number instead of a normal one. This patch makes it show `0` in that case instead of garbage.

Neither of these needs GPS, a phone app, or an internet connection to matter. They can happen on a plain device that's just running normally and eventually reboots, which happens to every device sooner or later.

## Why This Exists

Both issues are known upstream and both fixes remain unmerged (upstream PRs 1972 and 1349, tracked in `patches/0001.meta.yaml` and checked daily by patch-drift-canary). This mod carries just those two fixes.

One thing on purpose that this mod does *not* do: the second upstream PR also removes the safety check on the `time` CLI command that stops the clock from being set backward by hand. We left that part out. (Automatic NTP sync, on builds where it's enabled, doesn't go through that check and corrects the clock in either direction.) Nothing in our current builds actually needs it yet (no GPS, no phone app syncing time), and removing that check outright trades one problem for a different, unreviewed one. If we add automatic internet time sync later, that piece can be revisited properly alongside it.
