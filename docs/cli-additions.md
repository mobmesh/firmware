# CLI commands added by this patch

This is the documentation `patches/0001-hotspot-fetch-ota.patch` adds to MeshCore's own
`docs/cli_commands.md`. It's kept here as well so it's readable without checking out and building the
patched source — the build environment that applies the patch is torn down after every run.

This file is regenerated automatically by `build-release.yml` on every successful build and committed
back to the repo if it changed. Don't hand-edit it — edits will be overwritten by the next build. Edit
the patch instead.

---

### Start an Over-The-Air (OTA) firmware update via hotspot download
**Usage:**
- `start ota url <url>`

**Parameters:**
- `url`: HTTP(S) URL of the firmware `.bin` to download and flash

**Note:** Requires `ota.wifi` to be set first (see below). Joins the configured WiFi network as a
station, downloads the file, verifies it against `ota.sha256` (or a `<url>.sha256` sidecar fetched
automatically if `ota.sha256` isn't set), and reboots on success. Does not reboot on failure.

**Requires:** `WITH_HOTSPOT_OTA` build flag (Heltec V4 repeater/room server only)

---

#### View or change the hotspot WiFi credentials used by `start ota url`
**Usage:**
- `set ota.wifi <ssid>,<password>`

**Parameters:**
- `ssid`: WiFi network name
- `password`: WiFi network password

**Note:** Persists across firmware updates (stored separately from node prefs). Set once; does not
need to be resupplied for future updates unless the hotspot's credentials change.

**Requires:** `WITH_HOTSPOT_OTA` build flag (Heltec V4 repeater/room server only)

---

#### View or change the manually-supplied firmware hash used by `start ota url`
**Usage:**
- `set ota.sha256 <hex>`
- `set ota.sha256 clear`

**Parameters:**
- `hex`: 64-character lowercase hex SHA-256 digest of the target firmware `.bin`

**Note:** Once set, this hash is used to verify the download and is **not** overridden by a
`<url>.sha256` sidecar, even if one is found. `set ota.sha256 clear` removes it, allowing sidecar-fetch
to take effect again. A stale hash from a previous update will block a correct sidecar for a new one if
left set — clear it before pointing `start ota url` at a different firmware image.

---

#### View or manually control the GPIO47 hotspot power switch
**Usage:**
- `get ota.pwr`
- `set ota.pwr <on|off>`

**Parameters:**
- `on` / `off`: Drives GPIO47 (the hotspot power-switch control pin) directly

**Note:** Diagnostic and recovery command, entirely independent of `start ota url` — does not join WiFi,
download, or flash anything. Use `get ota.pwr` to confirm the rail is off after an update completes or
fails, and `set ota.pwr off` to force it off manually if ever in doubt (e.g. after an unexpected reset
during an update). `HotspotOTA::run()` already drops this pin on every failure path it controls; this
command exists for the cases outside that function's control, such as a crash or watchdog reset
mid-update.
