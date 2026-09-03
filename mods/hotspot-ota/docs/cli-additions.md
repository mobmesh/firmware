<!-- Source of truth for the CLI commands mods/hotspot-ota adds. The upstream-sync
     workflow appends this to upstream's docs/cli_commands.md daily and regenerates
     pages/flasher/auto_commands.json from the result. -->

# CLI commands added by these patches

Documentation for the commands `mods/hotspot-ota` adds to MeshCore. Kept here
rather than patched into upstream's `docs/cli_commands.md`, which upstream edits
every release.

---

### Start an Over-The-Air (OTA) firmware update via a joined WiFi network
**Usage:**
- `start ota wan <url>`

**Parameters:**
- `url`: HTTP(S) URL of the firmware `.bin` to download and flash

**Note:** Requires `ota.wan.wifi` to be set first (see below). Joins the configured WiFi network as
a station, downloads the file, verifies it against the SHA-256 the image carries in its own final
32 bytes (or against `ota.fw.sha256` when one has been pinned), confirms the download is actually a
build of this project (refuses otherwise, even if the checksum matches -- catches `<url>` mistakenly
pointing at a different, unmodified MeshCore build), and queues the service with an immediate
`OK - OTA queued` response. The service reboots on success and leaves the current firmware running
on failure. Nothing is fetched but the image itself.

Three things are decided from the first 288 bytes, before the rest of the file is pulled over what
may be a metered connection:

- **Not a build of this project** -- aborts. The identifying block sits at a fixed offset, so this
  needs no scanning and nothing fetched from outside the file.
- **Built without OTA support** -- aborts. The block names which mods the image actually carries,
  read out of the built binary rather than off the build config, so flashing a build that could
  never be updated again is refused rather than discovered afterwards.
- **Built for a different board or role** -- aborts, naming both. Nothing before this could tell one
  board's build from another's.
- **Already running exactly this build** -- stops and says so, rather than rewriting a partition
  with what is already in it. `set ota.fw.marker off` forces it through, which is how a deliberate
  re-flash over a corrupt partition still works.

**Requires:** `WITH_HOTSPOT_OTA` build flag on shipped ESP32 targets

---

### Stop or inspect the upstream `start ota` access point
**Usage:**
- `stop ota`
- `get ota.ap`

**Note:** Upstream's `start ota` raises an open access point with an unauthenticated firmware
upload page, blocks power saving, and has no stop of its own, so it stays up until the node
reboots. This mod bounds it: the access point is dropped 20 minutes after `start ota` unless an
upload is actively writing, and an upload that stops making progress for 10 minutes is aborted and
the access point dropped with it. A successful upload reboots the node, so the deadline only ever
ends an abandoned session. `stop ota` ends it on demand and is refused while an upload is writing.
`get ota.ap` reports whether it is up and how long is left.

Upstream keeps no handle on the web server it starts, so port 80 stays bound until the node
reboots. A second `start ota` in the same boot therefore reaches a bind failure that upstream does
not check, raising an access point with nothing serving it while still reporting success. It is
refused here instead, with `reboot first`. Dropping the access point with `WiFi.softAPdisconnect`
in its wifi-off form hangs the node outright, measured on a Heltec V4, so the interface is left up
and only the access point is stopped.

**Requires:** `WITH_HOTSPOT_OTA` build flag on shipped ESP32 targets

---

### Inspect or cancel a WAN OTA update
**Usage:**
- `get ota.status`
- `ota cancel`

**Note:** `get ota.status` reports the current service state. During download it includes the byte
count and total when the server supplies a content length. A failure or cancellation remains
available as a terminal result until another update starts. `ota cancel` requests cancellation
while the service is queued, joining, checking WAN connectivity, opening the URL, or downloading.
Verification and partition commit are intentionally not cancellable.

**Requires:** `WITH_HOTSPOT_OTA` build flag on shipped ESP32 targets

---

### Start an Over-The-Air (OTA) firmware update using the configured default URL
**Usage:**
- `start ota wan update`

**Note:** Identical to `start ota wan <url>`, using the URL persisted via `set ota.fw.url` instead
of one supplied on the command line. Fails with `ERR: ota.fw.url not configured` if none has been
set. Exists to keep remote admin updates short over LoRa -- a full firmware URL can be well over 100
characters, and `start ota wan update` is 21.

**Requires:** `WITH_HOTSPOT_OTA` build flag on shipped ESP32 targets

---

#### View or change the WiFi credentials used by `start ota wan`
**Usage:**
- `set ota.wan.wifi <ssid>,<password>`

**Parameters:**
- `ssid`: WiFi network name
- `password`: WiFi network password

**Note:** Persists across firmware updates (stored separately from node prefs). Set once; does not
need to be resupplied for future updates unless the network's credentials change.

**Requires:** `WITH_HOTSPOT_OTA` build flag on shipped ESP32 targets

---

#### View or change the manually-supplied firmware hash used by `start ota wan`
**Usage:**
- `set ota.fw.sha256 <hex>`
- `set ota.fw.sha256 clear`

**Parameters:**
- `hex`: 64-character lowercase hex SHA-256 digest of the target firmware `.bin`

**Note:** RAM-only — cleared on every boot, never persisted. Pinning a hash here is the only
integrity check that does not come from the same host as the image, so it is what to use when the
source is not trusted. Once set it takes precedence over the image's own embedded digest. `set
ota.fw.sha256 clear` returns to that digest — do this before pointing `start ota wan` at a different
firmware image, or a stale pin will block it.

**Requires:** `WITH_HOTSPOT_OTA` build flag on shipped ESP32 targets

---

#### View or change the default URL used by `start ota wan update`
**Usage:**
- `get ota.fw.url`
- `set ota.fw.url <url>`

**Parameters:**
- `url`: HTTP(S) URL of the firmware `.bin` to download and flash

**Note:** Persisted — unlike `ota.fw.sha256`/`ota.fw.marker`, this names a stable download location
for this device rather than a one-time override. Overwrite to change it; there is no `clear`.
Rejects (does not truncate) a URL longer than the field allows.

**Requires:** `WITH_HOTSPOT_OTA` build flag on shipped ESP32 targets

---

#### View or change whether `start ota wan` checks the download's authenticity marker
**Usage:**
- `set ota.fw.marker <on|off>`

**Parameters:**
- `on` / `off`: Enables or disables the marker/authenticity scan (default `on`)

**Note:** RAM-only and one-time — consumed by the very next `start ota wan` call regardless of
outcome, and always starts back at `on` on every boot (never persisted, never survives a reboot).
`off` is a last resort for firmware that carries no identifying block at all (self-hosted, not built
by this project's own release pipeline, for example) — not intended for routine use, since it skips
the check that exists specifically to catch a mistaken `<url>`. It also forces a re-flash of a build
the node is already running. It never bypasses the hash check, which is always enforced.

**Requires:** `WITH_HOTSPOT_OTA` build flag on shipped ESP32 targets

---

#### View or manually control the GPIO47 WAN power switch
**Usage:**
- `get ota.wan.pwr`
- `set ota.wan.pwr <on|off>`

**Parameters:**
- `on` / `off`: Drives GPIO47 (the WAN power-switch control pin) directly

**Note:** Diagnostic and recovery command, independent of `start ota wan` — does not join WiFi,
download, or flash anything. Manual changes are refused while the OTA service is active. Use to
confirm or force the rail off if state is ever in doubt (e.g. after a crash or watchdog reset).

**Requires:** `WITH_HOTSPOT_OTA` build flag on shipped ESP32 targets

---

#### Pre-flight check the WiFi join and WAN connectivity before `start ota wan`
**Usage:**
- `ota wan join`
- `ota wan check`
- `ota wan leave`

**Note:** `ota wan join` joins the configured WiFi network only (no WAN check, no download),
returning in one quick attempt (~15s worst case) instead of `start ota wan`'s full patient join
budget (~115s). `ota wan check` checks WAN reachability on demand, repeatable without rejoining.
`ota wan leave` disconnects and drops WAN power for a clean retry. A successful `ota wan join` lets
`start ota wan` skip its own join step right after. These commands refuse to interfere with an
active OTA service.

**Requires:** `WITH_HOTSPOT_OTA` build flag on shipped ESP32 targets

---

#### View the active OTA slot and post-update rollback confirmation status
**Usage:**
- `get ota.slot`

**Returns:** `Slots: A=<ver> (active, <state>) | B=<ver> (recorded-<state>, <image>)`, or the other
way round if `B` is the running slot (`A` = `ota_0`, `B` = `ota_1`). `<ver>` is that slot's firmware version plus the short
build commit hash it came from (e.g. `v1.16.0-0f11a30`) -- the hash distinguishes two slots that
happen to share the same version number but came from different builds. Self-reported into SPIFFS
the first time that slot actually boots -- a slot that's never booted (see `n/a` below) reports `v?`
since nothing's been recorded for it yet. The active slot's `<state>` is one of:
- `pending`: This boot is on probation after an OTA update (via either `start ota` or `start ota wan`).
  The device will automatically confirm itself as valid ~90 seconds after boot if the radio initializes
  correctly; an unconfirmed reset before then reverts to the previous firmware automatically.
- `valid`: Already confirmed, or this boot isn't the result of an OTA update in the first place.
- `n/a`: Rollback state could not be queried for the running partition.

The other (non-active) slot reports two independent facts, because one does not imply the other.

`recorded-<state>` is what the bootloader's otadata records about it: `recorded-valid`, `invalid`
(rejected by a previous rollback), `aborted` (an update to it never finished), `new` (flashed but
not yet booted), or `n/a` (never involved in an OTA update, e.g. a factory/USB-only flash). This
records what the last OTA *intended*. It is not evidence that the slot still holds a bootable image:
a cancelled or interrupted download leaves the recorded state untouched over a truncated image.

`<image>` is that evidence, read from the flash itself by `esp_image_verify()`, which checks the
image header, the segment checksum and the appended SHA-256:
- `image-ok`: the slot holds a complete, bootable image. This is the only fact that makes it a
  usable rollback target.
- `image-invalid`: the slot will not boot. Expected after a cancelled or interrupted update.
- `image-absent`: no such partition.
- `image-unchecked`: an update is writing that slot right now, so it was not inspected -- a torn
  image mid-write is expected, not a fault.

`recorded-valid, image-invalid` is a real and important combination: it means otadata still believes
in an image that is no longer there. Only `image-ok` says a rollback target exists.

Verification hashes the whole image, so this command costs roughly 450 ms on a Heltec V4 carrying a
1.35 MB image, against about 20 ms for other CLI commands. Only the inactive slot is verified; the
active one is running, which is proof enough that it boots.

**Note:** Confirms automatically after the confirm delay with a working radio; rejects immediately
(rollback + reboot) if `radio_init()` fails on a probationary boot.

**Requires:** `WITH_OTA_ROLLBACK_GUARD` build flag on shipped ESP32 targets

---

#### Manually switch which OTA slot boots
**Usage:**
- `ota slot boot <A|B>`

**Parameters:**
- `A` / `B`: The OTA slot to boot into next (`A` = `ota_0`, `B` = `ota_1`) -- see `get ota.slot`.

**Note:** Points the bootloader at the requested slot and **reboots into it immediately** without
touching flash contents -- useful when a USB flash lands on the currently-inactive slot (e.g. after
an earlier `start ota wan` update flipped which slot is active) and appears not to take effect.
Refuses if the requested slot is already active, doesn't exist, or has no valid app image flashed to
it.

Switching also re-arms rollback probation for the target slot, even if it was previously `valid` --
ESP-IDF marks any newly-selected boot partition `new`, which the bootloader promotes to `pending` on
that boot. `get ota.slot` will report `pending` right after the swap; this is expected, not a fault,
and confirms `valid` again after the normal ~90s confirm delay.

**Requires:** `WITH_OTA_ROLLBACK_GUARD` build flag on shipped ESP32 targets

---
