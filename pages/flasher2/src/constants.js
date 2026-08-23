// The constants register. This file is the list — rewrite_code.md §7 is where most
// of these came from, not a second copy kept in step with it.
//
// §7 exists because a reimplementation reads unexplained numbers as arbitrary and
// normalises them away. That protection needs one auditable list, and a list that
// lives beside the code cannot drift from it. Values established during
// implementation and absent from §7 are tagged here the same way and carry the
// same weight.
//
// Every value carries its provenance at the point of declaration. Values tagged
// `measured`, `protocol` or `datasheet` are hardware facts and may not be changed
// without a hardware retest (C0); `matched` needs a stated reason; `design` is a
// judgement call and is safe to revisit.

// --- Port lifecycle (§7.2) ---
// Written into the nRF52 section of the register because that is where they were
// established, but the port module is shared by all three paths (C4).

// `measured`. Waiting for a re-enumerated port to reappear after a reset. nRF52
// changes USB identity on entering DFU, so this covers a full disconnect cycle.
export const PORT_CONNECT_TIMEOUT_MS = 8000;

// `measured`. Between the connect event and the first open probe. The device
// node exists before the interface will accept an open.
export const PORT_SETTLE_AFTER_CONNECT_MS = 500;

// `measured`. Open probes before a port is declared unusable.
export const PORT_OPEN_PROBE_ATTEMPTS = 8;

// `measured`. Between failed open probes.
export const PORT_OPEN_RETRY_DELAY_MS = 750;

// `protocol`. DFU port baud. The probe uses it so a port that refuses the rate fails
// at probe time rather than mid-flash.
export const PORT_PROBE_BAUD_RATE = 115200;

// `measured`. DFU write attempts before escalating to the user. `dfu.js` opens the port
// itself, so an attempt that never moved a byte is the retryable one.
export const DFU_FLASH_ATTEMPTS = 3;

// `matched`. Between the erase package and the firmware package on nRF52. GulfCoastMesh
// measured 3000 ms, and 5000 ms for two RAK boards it names by its own device ids; those
// ids do not map onto upstream's device names, which change weekly. One value, the slower.
export const DFU_POST_ERASE_SETTLE_MS = 5000;

// `matched`. The §11.7 exit gesture, from GulfCoastMesh's `resetDeviceAfterDfu` — the
// bootloader watches for the DTR transition. Not our own hardware measurement.
export const DFU_RESET_DTR_LOW_MS = 50;
export const DFU_RESET_DTR_HIGH_MS = 100;
export const DFU_RESET_SETTLE_MS = 300;

// `matched`. Interval between rescans of the granted-port list while waiting for a
// device to come back after a reset. Matches the existing flasher's bootloader poll
// interval (§7.1), which is hardware-verified against these boards.
export const PORT_RESCAN_INTERVAL_MS = 500;

// --- MeshCore CLI (§7.1) ---
// The CLI is the same firmware on both MCU families, so these serve ESP32 and
// nRF52 alike (§5.4, bootApp's confirmation step).

// `matched`. The CLI's line rate, from the shipped flasher.
export const CLI_BAUD_RATE = 115200;

// `protocol`. The device prefixes every response line with this marker. Framing
// depends on it exactly — including both spaces.
export const CLI_RESPONSE_MARKER = '  -> ';

// `protocol`. What we send to end a command. The device is line-buffered on this
// character, and appends anything not yet terminated to whatever arrives next.
export const CLI_LINE_TERMINATOR = '\r';

// `protocol`. What the device sends: it expands the terminator on the way back, so
// both its echo of a command and the end of a response line carry this pair.
export const CLI_LINE_ENDING = '\r\n';

// `measured`. The first command can land while the device is still generating its identity
// keypair; CLI traffic during that is suspected of corrupting it.
// Measured on a Heltec v4: a full-erase install answers `ver` 37.4 s after the reset, so
// 30 s — the shipped flasher's value — lands just inside the keypair generation and times
// out on a device that is perfectly healthy. This is a single long window on purpose:
// polling would put CLI traffic on the wire during keygen, which is suspected of
// corrupting the key.
export const CLI_FIRST_COMMAND_TIMEOUT_MS = 60000;

// `design`. Once the CLI is confirmed up and answering.
export const CLI_COMMAND_TIMEOUT_MS = 5000;

// `measured`. Between commands in a sequence.
export const CLI_INTER_COMMAND_DELAY_MS = 100;

// `design`. Liveness probe only (§10.2 state 1) — a fast no is the point. No measured
// value exists; revisit against hardware.
export const CLI_PROBE_TIMEOUT_MS = 1500;

// Post-flash reconnect (§10.7). Wait for the device to actually drop off the bus before
// looking for it again — the handle from the write stays openable for a moment after the
// reset and reconnecting to it lands on a port that never speaks.
export const PORT_DROP_TIMEOUT_MS = 10000;

// A freshly written node announces its public key once it has finished generating its
// identity — measured at +33.3 s on a Heltec v4 after a full-erase install, against
// +0.5 s for the SPIFFS mount error that precedes it. Listening for that costs nothing
// and puts no CLI traffic on the wire during keygen; the ceiling is only a fallback.
export const BOOT_ANNOUNCE_TIMEOUT_MS = 60000;
export const POST_FLASH_RECONNECT_ATTEMPTS = 20;
export const POST_FLASH_RECONNECT_DELAY_MS = 500;

// --- esptool / ROM (§7.1) ---

// `protocol`. The ROM loader's default line rate. The 2 Mbaud in §7.1 applies
// after the stub is up; a passive probe never gets that far.
export const ESP_ROM_BAUD_RATE = 115200;

// `measured`. Web Serial's 255-byte default overruns on any main-thread pause and
// surfaces as SLIP framing errors. macOS is less forgiving than Linux.
export const SERIAL_READ_BUFFER_BYTES = 65536;

// `design`. Bounds the whole passive probe, which internally allows 100 ms per SYNC
// packet across five exchanges. Sized so a silent device is reported quickly.
export const SYNC_PROBE_TIMEOUT_MS = 3000;

// `design`. Each attempt already performs five SYNC exchanges, and with no reset between
// them a second attempt asks the same question again.
export const SYNC_PROBE_ATTEMPTS = 1;

// --- Download-mode entry (§7.1, §10.3) ---

// `protocol`. On ≥1.17 a running node shares the ROM's vid:pid, which is why this selects
// a reset path and never an install decision (C1).
export const ESPRESSIF_VENDOR_ID = 0x303a;
export const ROM_BOOTLOADER_PRODUCT_ID = 0x1001;
export const LEGACY_CDC_PRODUCT_ID = 0x0002;

// `measured`. Nordic UF2 bootloader vendor, shared by the application and DFU identities
// (T1: 239a:8029 running, 239a:0071 in DFU). Family detection has no other signal — a
// legacy ESP32 behind a USB-UART bridge shows the bridge's vendor, never the MCU's.
export const NORDIC_UF2_VENDOR_ID = 0x239a;

// `measured`. Between signal transitions in the TinyUSB entry gesture. Both
// transitions are required.
export const LEGACY_ENTRY_STEP_GAP_MS = 100;

// `measured`. Re-enumeration after the reboot gesture has been observed taking
// several seconds; 40 polls at the 500 ms rescan interval.
export const ROM_APPEAR_TIMEOUT_MS = 20000;

// `measured`. After opening the matched bootloader port, before closing it.
export const ROM_PORT_SETTLE_MS = 2000;

// `matched`. esptool's own default for `connect`. Each attempt re-applies the reset
// strategy, which is worth repeating here precisely because a reset *is* issued.
export const ENTRY_CONNECT_ATTEMPTS = 7;

// `measured`. Companion-protocol reply window. Companion builds answer immediately or not at
// all — a build that does not speak it never will, so this is a liveness bound, not patience.
export const COMPANION_REPLY_TIMEOUT_MS = 3000;

// --- Flash writing (§7.1, §10.1) ---

// `protocol`. How much of the spare app slot is blanked to stop the bootloader booting it.
// The image header carrying the 0xE9 magic sits at the slot's first byte, so one sector is
// all it takes to make the slot invalid. Blanking the whole partition has the same effect
// and costs the device 18.6 s to program a 6.5 MB slot — measured on the v4, compressed
// transfer is free but every byte is still written.
export const APP_SLOT_INVALIDATE_BYTES = 0x1000;


// `matched`. Not a clock constraint — esptool renegotiates once the stub is up. Lower
// values cost real time on a 1.3 MB image.
export const ESPTOOL_BAUD_RATE = 2000000;

// `protocol`. For `flashMode`/`flashFreq`/`flashSize`. Each image carries its own
// flash-config header; overriding any of the three corrupts boot.
export const FLASH_IMAGE_PARAMETER_KEEP = 'keep';

// `design`. Compressed writes; the ROM and stub both support them and a uniform
// 0xFF slot-B buffer costs almost nothing on the wire.
export const FLASH_WRITE_COMPRESSED = true;

// --- Exit to application (§7.1, §10.4) ---
// `hard_reset` does not re-sample the boot strapping pins on these parts, so the
// watchdog path is required rather than preferred. Register addresses are per-chip
// and `matched` — taken from esptool's own `targets/esp32s3.py` and `esp32c3.py`;
// the S3 set is undocumented in the TRM. Keyed by esptool's `CHIP_NAME`.
export const WATCHDOG_RESET_REGISTERS = {
  'ESP32-S3': {
    WDTCONFIG0: 0x60008098,
    WDTCONFIG1: 0x6000809c,
    WDTWPROTECT: 0x600080b0,
    OPTION1: 0x6000812c,
  },
  'ESP32-C3': {
    WDTCONFIG0: 0x60008090,
    WDTCONFIG1: 0x60008094,
    WDTWPROTECT: 0x600080a8,
    OPTION1: 0x600080f4,
  },
};

// `matched`. RTC_CNTL_WDT_WKEY — unlocks WDTCONFIG*, and writing anything else
// re-locks them. From esptool; §7.1 carries no entry for it.
export const WATCHDOG_WRITE_PROTECT_KEY = 0x50d83aa1;

// `matched`. WDT stage-0 timeout, from esptool's chip targets.
export const WATCHDOG_TIMEOUT = 2000;

// `matched`. WDT_CHIP_RESET_EN + reset width 5 + a stage-0 action of "reset system"
// + enable. Undocumented in the S3 TRM; from esptool's chip targets.
export const WATCHDOG_CHIP_RESET_VALUE = 0xd0000102;

// `protocol`. OPTION1 bit 0 is FORCE_DOWNLOAD_BOOT. Measured: leaving it set —
// which esptool's own `--after watchdog-reset` does — strands an S3 in download
// mode indefinitely rather than booting the application.
export const FORCE_DOWNLOAD_BOOT_MASK = 1;

// `measured`. For the reset to take effect before anything else touches the bus.
export const POST_WATCHDOG_RESET_WAIT_MS = 500;

// --- Reading flash back (§7.1, §10.5/§10.6) ---
// One dropped packet mid-read otherwise costs the whole partition, which on a
// marginal cable is close to a coin flip over a multi-megabyte filesystem.

// `matched`. Matches esptool's own read chunk. Larger chunks mean one dropped
// packet costs more work; smaller ones cost round trips.
export const FLASH_READ_CHUNK_BYTES = 0x40000;

// `measured`. Attempts at one chunk before reopening the port.
export const FLASH_READ_ATTEMPTS_PER_CHUNK = 3;

// `measured`. Port reopens across a whole read before giving up on it entirely.
export const FLASH_READ_MAX_PORT_REOPENS = 4;

// `measured`. Between disconnect and reconnect when recovering a read.
export const PORT_REOPEN_SETTLE_MS = 100;

// --- GulfCoastMesh node registry (gcm-reg.js) ---

// `design`. Deliberately short. Registration is optional enrichment on top of a flash that
// has already succeeded, so a slow or absent registry must never hold up the wizard.
export const GCM_REGISTRY_TIMEOUT_MS = 3000;

// --- Stock firmware (§9) ---

// `matched`. Upstream ships one merged image (app + partitions + bootloader) for a wipe
// and a bare app image for an update; the two go to different addresses. Sourced from
// flasher.meshcore.io's own flasher, which is what these files are built for.
export const STOCK_ESP32_MERGED_ADDRESS = 0x0;
export const STOCK_ESP32_APP_ADDRESS = 0x10000;

// `design`. Stock bytes cross an origin, so they come through our relay (§9.2) rather
// than from upstream directly. The manifest does not: CI mirrors it same-origin.
export const STOCK_RELAY_BASE = 'https://fw.mobmesh.workers.dev/fw/';

// `matched`. Device artwork is root-relative in the manifest (`/img/…`), so it only
// resolves against upstream's own origin — not `staticPath`, which addresses firmware.
export const STOCK_IMAGE_BASE = 'https://flasher.meshcore.io/';
