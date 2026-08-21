// Constants register — rewrite_code.md §7. Every value carries its provenance
// tag. Values tagged `measured`, `protocol` or `datasheet` are hardware facts
// and may not be changed without a hardware retest (C0).
//
// Only values the built modules actually consume are declared here; the register
// in §7 remains the complete list.

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

// `protocol`. The DFU port's baud. The readiness probe opens and closes
// immediately, so the rate only carries meaning on the DFU path itself — but the
// probe uses it so that a port which refuses this rate fails at probe time
// rather than mid-flash.
export const PORT_PROBE_BAUD_RATE = 115200;

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

// `measured`. The first command after a fresh erase can land while the device is
// still generating its identity keypair, which takes far longer than any later
// command. CLI traffic during keypair generation is suspected of corrupting it.
export const CLI_FIRST_COMMAND_TIMEOUT_MS = 30000;

// `design`. Once the CLI is confirmed up and answering.
export const CLI_COMMAND_TIMEOUT_MS = 5000;

// `measured`. Between commands in a sequence.
export const CLI_INTER_COMMAND_DELAY_MS = 100;

// `design`. Liveness probe only (§10.2 state 1), where a *fast* no is the point: a
// device stuck in init never answers, and waiting the full command timeout to learn
// that delays the unknown-state report. §7.1 carries no measured value for this;
// revisit against hardware.
export const CLI_PROBE_TIMEOUT_MS = 1500;

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

// `design`. Connect attempts for the passive probe. esptool defaults to 7, but each
// attempt already performs five SYNC exchanges internally; with no reset strategy
// between attempts, repeating one only adds latency.
export const SYNC_PROBE_ATTEMPTS = 1;

// --- Download-mode entry (§7.1, §10.3) ---

// `protocol`. Espressif's USB vendor id, and the ROM bootloader's product id.
// Both supported boards expose the same ROM identity; on ≥1.17 a running node
// shares it, which is why it selects a reset path and never an install decision (C1).
export const ESPRESSIF_VENDOR_ID = 0x303a;
export const ROM_BOOTLOADER_PRODUCT_ID = 0x1001;
export const LEGACY_CDC_PRODUCT_ID = 0x0002;

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

// --- Flash writing (§7.1, §10.1) ---

// `matched`. Nominal for native USB-Serial/JTAG; esptool renegotiates to it once
// the stub is up, so it is not a clock constraint. Lower values cost real time on
// a 1.3 MB image.
export const ESPTOOL_BAUD_RATE = 2000000;

// `protocol`. Applied to `flashMode`, `flashFreq` and `flashSize` alike. Each image
// carries its own correct flash-config header, baked in at compile time; overriding
// any of the three corrupts boot.
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
