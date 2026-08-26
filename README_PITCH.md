<div align="center">

# Why this firmware exists

### Fewer trips and no climbing.

⚡️ **[Flash a device now](https://tools.mobmesh.org/flasher)** &nbsp;•&nbsp; [What you get](#the-short-version) &nbsp;•&nbsp; [How it works](#still-reading-good) &nbsp;•&nbsp; [Mods](#current-mods-applied)

</div>

---

> **Somebody has to climb or make the drive.**

That's the whole problem in one sentence. A mesh node does its best work when it's somewhere inconvenient: a rooftop, a tower, a pole at the back of a property, a weatherproof box on a hill that took a four-wheel-drive to reach. The better the location, the worse the trip. And every firmware update, every "wait, did that actually take?" means making the trip again.

This project is a set of changes to MeshCore that shrink the number of times anyone has to climb or drive.

## The short version

### 📡 Update a node without going to it
Send it a command over the mesh. It powers up a hotspot, pulls the new firmware down, checks it, installs it, and comes back. You never leave the couch.

### 🛟 A bad update can't strand a node
New firmware has to prove it works before it's trusted. If the radio doesn't come up, the node reverts to what it was running before and reboots itself. No trip. No ladder.

### 🔑 Your device keeps its identity
Our web-based flasher reads the device first, then puts configuration data back afterward, <ins>even when the new firmware uses a different partition layout</ins>.

### ⚙️ Settings arrive preconfigured
Frequency, bandwidth, spreading factor, coding rate, advert intervals, region map. All of it applied automatically after the flash, from a config your group publishes. New members don't have to be walked through a CLI session to join correctly.

| | Stock MeshCore | This firmware |
| --- | --- | --- |
| Update a remote node | Walk to it | Send a command over LoRa |
| Bad firmware image | Node halts, stays down | Reverts itself, reboots |
| Reflashing | Identity and settings lost | Read off first, written back |
| Radio settings | Typed in by hand | Applied from your group's config |
| Clock after reboot | Placeholder date | Set from NTP on any OTA join |

> [!TIP]
> If that's all you needed, the ⚡️ **[web-based flasher](https://tools.mobmesh.org/flasher)** is right there and the rest of this page is optional.

---

## Still reading? Good.

### Remote updates, done properly

The stock `start ota` command turns the node into a WiFi access point and waits for someone standing nearby to upload a file through a web form. Standing on your toes, holding a phone in the air, does this sound familiar? Not to mention making an appointment for a site visit, or crossing your fingers the node is in range today and doesn't brick itself because of bad 'tip-toe' reception.

This firmware adds a second path. The node powers up the rail feeding its hotspot or cell modem, joins a network you configured once, confirms it actually has internet, downloads the image, vets it while downloading, installs it, then powers down the hotspot. The whole process takes about 60 seconds. The commands are ordinary MeshCore CLI commands, which means they travel over LoRa like anything else. A node three hops away can be firmware updated from your kitchen table.

Because LoRa messages are small and precious, there's a short form. Save the URL once:

```text
set ota.fw.url https://example.org/firmware/heltec_v4_repeater.bin
```

and from then on the whole update is:

```text
start ota wan update
```

Every download is checked against a SHA-256 hash the image carries inside itself, so there's no second file to fetch and nothing to keep in step. The same block tells the node whether the image is genuinely a build from this project (and has remote OTA abilities), whether it was built for this board and role, and whether it's the build already running — each decided from the first few hundred bytes, before the rest comes down. This prevents you from losing remote OTA abilities by flashing a non-capable image, and from spending a metered connection on a download that was never going to be used.

### The part that lets you sleep

Remote updates are only appealing if a failed one isn't a disaster. Stock MeshCore, faced with firmware whose radio won't initialize, calls `halt()`. The node goes quiet and stays quiet until someone shows up, and makes the climb with a cable.

Here, new firmware starts on probation. It has to run about ninety seconds with a working radio before it's marked good. If the radio fails to come up during that window, the node concludes the update was bad, switches back to the previous image, and reboots. It fixes itself.

Radio failures that aren't related to a recent update are treated differently, with a bounded number of retries before giving up, so a genuinely broken component doesn't turn into an endless reboot loop.

> [!NOTE]
> None of this needs extra hardware. It uses the app-rollback support already sitting in the ESP-IDF toolchain that MeshCore builds on.

### Clocks that don't lie

These boards have no battery-backed clock. Every reboot resets them to a placeholder date, which makes "last heard from" numbers meaningless right after a restart.

Two things address that. Any time the node joins WiFi for OTA reasons, it also asks an NTP server what time it is and sets its clock. Free, automatic, riding along on a connection it was already making. Separately, a pair of timing fixes stop the long-uptime timer from rolling over incorrectly after several weeks, and stop elapsed-time math from producing nonsense right after a reboot.

Both of those timing bugs are known upstream and both have fixes sitting in pull requests that have gone unmerged for months. They're here now rather than whenever.

### The flasher earns its keep

It's a web page. Use your favorite Chromium-based browser. Plug in the board, pick it from a list, done. It automatically pulls the most recent build for whatever you selected.

The interesting part is what it does about your data. This flasher reads the filesystem off the device before touching anything, then writes it back afterward. If the new firmware's partition layout is a different size, it rebuilds the filesystem to fit rather than giving up.

It also checks whether the device you plugged in is actually running MeshCore before deciding to preserve anything. Another project's files mean nothing to MeshCore, and carrying them into a fresh install would be worse than starting clean, so it doesn't.

### Group settings, without the tutorial

Getting a new node onto a mesh correctly means matching frequency, bandwidth, spreading factor and coding rate exactly. One wrong value and the node transmits happily into the void, hearing nothing and heard by nobody, with no error to explain it.

Pick your group from the menu at the top of the flasher and those values are applied for you after the flash, along with advert timings and the full region map. The settings live in a small published file, so when the group changes something, everyone who flashes afterward gets the change without having to hunt for the latest information.

### How it stays current

This project isn't a fork. Forks drift, and eventually somebody is maintaining a parallel universe.

Instead the repo holds patches. Every build starts by pulling the latest upstream MeshCore release and applying those patches to it. Each patch declares what it depends on, and GitHub verifies the whole chain before building anything.

When upstream changes something a patch relies on, the build fails loudly and opens an issue naming the patch. Nothing gets silently merged or auto-fixed, because a patch that half-applies to firmware running on a tower is worse than one that doesn't apply at all.

Every ESP32-S3 based release also gets booted. Not a special test build, the exact binary that ships, byte for byte, started in an emulator and watched for ninety seconds. Radio init is expected to fail there because there's no real LoRa hardware attached, so the check knows the difference between that controlled restart and a real problem: a panic, a backtrace, an abort, a watchdog or brownout reset, or simply restarting more times than a healthy boot should. Anything that looks like a genuine crash or a boot loop auto raises an issue against the release.

Board configuration works the same way. Anything MeshCore already knows about a board, like its MCU, flash size or USB IDs, is read from upstream rather than copied. Only the genuinely local details are kept here.

---

## What it runs on today

| Board | Roles |
| --- | --- |
| Heltec V4 | Repeater, Room Server |
| Seeed Xiao C3 | Repeater, Room Server |
| The Grumpy Board | Repeater, Room Server |

More boards are in progress.

Every release is built by GitHub from a tagged upstream version, published with checksums, and flashable from the browser page.

## Worth knowing

> [!IMPORTANT]
> This is not the official MeshCore flasher, and this is not official MeshCore firmware. It's a set of additions maintained by GulfCoastMesh Mobile for our own nodes, shared because other people have the same rooftops and the same ladders.

---

## Current Mods Applied

Mods add features or changes to the standard MeshCore firmware. Each mod is maintained as a separate set of patches and can include its own board configuration and documentation.

| Mod           | Description                                                                                                                                                                                                                   | Main Features                                                                                                                                                                                         |
| ------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| [`hotspot-ota`](https://github.com/mobmesh/firmware/tree/main/mods/hotspot-ota) | Adds remote firmware updates over WiFi ( or cellular hotspot) and automatic rollback protection. A device can connect to an existing WiFi network, download a firmware image, verify it, and install it without needing to be onsite with the node. | Remote OTA updates, power control of external cell modems, firmware SHA-256 verification, firmware authenticity checks, OTA slot management, automatic rollback / recovery after failed updates, automatic clock sync via NTP whenever WiFi is joined, remote updates through MeshCore CLI commands |
| [`timing-safety`](https://github.com/mobmesh/firmware/tree/main/mods/timing-safety) | Small fixes for how the firmware tracks time. Keeps timers working correctly on devices that run for many weeks, and stops "time since last heard from" numbers from showing garbage right after a reboot. | Long-uptime timer fix, safer elapsed-time math across reboots |
| [`power-guard`](https://github.com/mobmesh/firmware/tree/main/mods/power-guard) | Keeps a bad situation from becoming an unrecoverable one, and puts the battery under its own management. Brownouts happen -- a flat pack, a cold morning, a cloudy week. Left alone, a node that browns out reboots straight into a loop that burns whatever charge is left and ends in a trip up the tower. This hibernates before it gets there, retries on a widening schedule, and comes back by itself once the battery does. Beyond the standard `powersaving on` / `off` it adds `powersaving auto`, which saves power only when the battery says to, and `powersaving safe`, the brownout failsafe. It also stops a mistyped `poweroff` from ending a node permanently. | Hibernation before the bootloop threshold, automatic recovery, power saving that engages only when it's needed, thresholds set over serial or the mesh and kept across reboots, `poweroff` requires a wake time and is refused over the mesh |

More information about each mod can be found in its own README under `mods/<name>/`.
