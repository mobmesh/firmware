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
