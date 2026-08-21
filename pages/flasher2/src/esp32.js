// ESP32 device state and transitions — §10.2 discrimination, §10.3 entry,
// §10.1 flash execution, §10.4 exit.
//
// One module because it is one state machine: work out what state the device is
// in, move it to the state we need, write to it, and put it back. The steps share
// constants, imports and call order; splitting them would follow the
// specification's headings rather than anything that changes independently.
//
// ESP32 only. The nRF52 equivalents are §11.2-§11.4.

import {
  CLI_BAUD_RATE,
  ESPRESSIF_VENDOR_ID,
  FORCE_DOWNLOAD_BOOT_MASK,
  LEGACY_CDC_PRODUCT_ID,
  LEGACY_ENTRY_STEP_GAP_MS,
  PORT_RESCAN_INTERVAL_MS,
  POST_WATCHDOG_RESET_WAIT_MS,
  ROM_APPEAR_TIMEOUT_MS,
  ROM_BOOTLOADER_PRODUCT_ID,
  ROM_PORT_SETTLE_MS,
  WATCHDOG_CHIP_RESET_VALUE,
  WATCHDOG_RESET_REGISTERS,
  WATCHDOG_TIMEOUT,
  WATCHDOG_WRITE_PROTECT_KEY,
} from './constants.js';
import { probeCliVersion } from './cli-session.js';
import {
  closeEsptoolSession,
  hardResetDevice,
  openEsptoolSession,
  probeEsptoolSync,
  readFlashRegion,
  resetIntoDownloadMode,
  writeEsptoolRegister,
  writeFlashFiles,
} from './esptool.js';
import {
  PARTITION_TABLE_MAX_BYTES,
  PARTITION_TABLE_OFFSET,
  parsePartitionTable,
} from './partitions.js';
import { closeSerialPortQuietly, listGrantedSerialPorts } from './serial-port.js';


function sleep(milliseconds) {
  return new Promise((resolve) => setTimeout(resolve, milliseconds));
}

/**
 * Both automatic mechanisms failed. Carries the board's own button instruction,
 * because the wording differs per board (PGM+RST vs BOOT+RESET) and the UI should
 * not compose it.
 */
export class ManualEntryRequiredError extends Error {
  constructor(instruction) {
    super(instruction);
    this.name = 'ManualEntryRequiredError';
    this.instruction = instruction;
  }
}

/**
 * The legacy TinyUSB gesture. Native-USB boards have no RTS/DTR-to-EN wiring —
 * the *running app* watches for this signal pattern and calls `esp_restart()`.
 * That makes it the only automated path on legacy firmware, and also means it
 * does nothing at all on a device that is not running a cooperating app.
 *
 * Both transitions are required, spaced per §7.1.
 */
async function fireLegacyEntryGesture(port) {
  if (!port.readable) await port.open({ baudRate: CLI_BAUD_RATE });
  try {
    await port.setSignals({ dataTerminalReady: false });
    await port.setSignals({ requestToSend: true });
    await sleep(LEGACY_ENTRY_STEP_GAP_MS);
    await port.setSignals({ dataTerminalReady: true });
    await port.setSignals({ requestToSend: false });
  } finally {
    // The device is about to disconnect and re-enumerate under the ROM's identity
    // — this port object is finished either way, and the poll below needs it
    // closed rather than still held open.
    await closeSerialPortQuietly(port);
  }
}

/**
 * Wait for the ROM identity to appear among granted ports.
 *
 * Entry from legacy app mode *changes the USB identity* (`0x0002` → `0x1001`), and
 * Web Serial authorises per device, so the ROM interface may never have been
 * granted on this machine. When that is the case no amount of waiting helps — the
 * caller escalates to the picker. Measured: a `0x0002`-only grant sees nothing
 * once the board enters download mode.
 */
function isRomIdentity(port) {
  const info = port.getInfo();
  return (
    info.usbVendorId === ESPRESSIF_VENDOR_ID && info.usbProductId === ROM_BOOTLOADER_PRODUCT_ID
  );
}

/** ROM-identity ports visible right now. Snapshot this *before* firing a gesture. */
async function romPortsPresent() {
  return (await listGrantedSerialPorts()).filter(isRomIdentity);
}

async function waitForRomPort(timeoutMs, alreadyPresent = []) {
  const deadline = Date.now() + timeoutMs;
  do {
    const granted = await listGrantedSerialPorts();
    // Only a ROM port that appeared *after* the gesture can be the device we just
    // rebooted. Verified on the bench: with two boards attached, a permanently
    // stuck xiao_c3 sits on the ROM identity forever, and `getInfo()` exposes no
    // serial to tell the two apart. Matching any ROM port hands back the wrong
    // board — and if that board were itself in download mode, it would be flashed.
    const rom = granted.find((port) => isRomIdentity(port) && !alreadyPresent.includes(port));
    if (rom) {
      // Settle before use: opening and closing once lets the interface finish
      // coming up, which it does not always have done when it first appears.
      if (!rom.readable) await rom.open({ baudRate: CLI_BAUD_RATE }).catch(() => {});
      await sleep(ROM_PORT_SETTLE_MS);
      await closeSerialPortQuietly(rom);
      return rom;
    }
    await sleep(PORT_RESCAN_INTERVAL_MS);
  } while (Date.now() < deadline);

  return null;
}

/**
 * §10.3: bring a device into download mode and prove it got there.
 *
 * Mechanism is chosen by the observed product id, then the *other* mechanism is
 * tried on failure, then the manual button instruction. Returns the port that is
 * actually in download mode, which is frequently not the port passed in.
 *
 * @param {SerialPort} port
 * @param {{ manualInstruction: string, onStatus?: (message: string) => void }} options
 */
export async function enterDownloadMode(port, { manualInstruction, onStatus } = {}) {
  const observedProductId = port.getInfo().usbProductId;

  async function attemptLegacyGesture() {
    onStatus?.('Asking the device to reboot into flash mode…');
    const before = await romPortsPresent();
    await fireLegacyEntryGesture(port);
    return waitForRomPort(ROM_APPEAR_TIMEOUT_MS, before);
  }

  async function attemptUsbJtagReset() {
    onStatus?.('Resetting the device into flash mode…');
    const before = await romPortsPresent();
    // A USB Serial/JTAG device keeps its identity across this reset — measured: the
    // control-line reset produces no re-enumeration at all — so success means this
    // same port is now in download mode.
    if (await resetIntoDownloadMode(port)) return port;
    return waitForRomPort(ROM_APPEAR_TIMEOUT_MS, before);
  }

  // Observed, not assumed: §10.2 establishes state, and the PID selects only how
  // to talk to what is there. The app-cooperative gesture does nothing on a
  // USB Serial/JTAG device, and esptool's reset has no app to cooperate with on
  // legacy firmware — so each is tried first where it is known to work.
  const mechanisms =
    observedProductId === LEGACY_CDC_PRODUCT_ID
      ? [attemptLegacyGesture, attemptUsbJtagReset]
      : [attemptUsbJtagReset, attemptLegacyGesture];

  for (const attempt of mechanisms) {
    const romPort = await attempt();
    if (!romPort) continue;

    // Affirmative confirmation before handoff — the whole point of §10.3.
    if (await probeEsptoolSync(romPort)) return romPort;
  }

  throw new ManualEntryRequiredError(manualInstruction);
}

/** @typedef {'app'|'bootloader'|'unknown'} Esp32Mode */

export const ESP32_MODE = {
  APP: 'app',
  BOOTLOADER: 'bootloader',
  UNKNOWN: 'unknown',
};

/**
 * Resolve the device's mode from affirmative signals only.
 *
 * Order is fixed and load-bearing: the CLI is asked first, because a running
 * application is the state that must never be misread. Only its silence licenses
 * asking the ROM. Silence from both is `unknown` — a real, verified state (a board
 * whose radio never initialises enumerates stably, stays silent, and still holds a
 * valid filesystem). Treating that silence as "bootloader" is what erases a live
 * device, so it is reported, never guessed at.
 *
 * Takes a closed port and returns it closed. Both probes need exclusive use of it.
 *
 * @returns {Promise<{ mode: Esp32Mode, version: string|null }>}
 */
export async function resolveEsp32Mode(port) {
  await port.open({ baudRate: CLI_BAUD_RATE });
  let version = null;
  try {
    version = await probeCliVersion(port);
  } finally {
    await closeSerialPortQuietly(port);
  }

  if (version) return { mode: ESP32_MODE.APP, version };

  // Passive — no reset is issued, so a device in state 3 is still in state 3
  // afterwards and can be reported to the user as it was found.
  const answeredSync = await probeEsptoolSync(port);
  return { mode: answeredSync ? ESP32_MODE.BOOTLOADER : ESP32_MODE.UNKNOWN, version: null };
}

/**
 * A write or the connection that carried it failed. `phase` is what the UI needs to
 * say next: nothing was touched on `connect`, whereas a `write` failure leaves the
 * device part-written and it must not be sent back to the application as if it
 * were fine.
 */
export class FlashWriteFailedError extends Error {
  constructor(message, { cause, phase }) {
    super(message, { cause });
    this.name = 'FlashWriteFailedError';
    this.phase = phase;
  }
}

/**
 * A read that had to succeed did not. §10.5 treats this as fatal on purpose: the
 * decision it feeds is erase-vs-preserve, and a failed read must never be allowed
 * to read as "nothing here".
 */
export class FlashReadFailedError extends Error {
  constructor(message, { cause }) {
    super(message, { cause });
    this.name = 'FlashReadFailedError';
  }
}

/**
 * §10.4: put the device back into the application.
 *
 * The RTC watchdog chip-reset sequence, not `hard_reset` — `hard_reset` does not
 * re-sample the boot strapping pins on these parts, so the watchdog path is
 * required rather than preferred.
 *
 * FORCE_DOWNLOAD_BOOT is cleared *first*. Measured: esptool's own
 * `--after watchdog-reset`, which omits that clear, strands an S3 in download mode
 * indefinitely instead of booting the application.
 *
 * Takes a live session because the sequence only works inside one: the unlock,
 * the two config writes and the re-lock have to reach the chip with nothing
 * re-syncing the ROM in between.
 *
 * Never throws — the write it follows has already succeeded, and losing the
 * automatic reset costs the user a button press, not their firmware.
 *
 * @returns {Promise<'watchdog'|'hard-reset'|null>} how the device was reset, or
 *   null if it could not be.
 */
export async function returnToApplication(session, { onStatus } = {}) {
  onStatus?.('Restarting the device…');
  const registers = WATCHDOG_RESET_REGISTERS[session.chipName];

  if (registers) {
    try {
      // Bit 0 only — the rest of OPTION1 is not ours to clear.
      await writeEsptoolRegister(session, registers.OPTION1, 0, FORCE_DOWNLOAD_BOOT_MASK);
      await writeEsptoolRegister(session, registers.WDTWPROTECT, WATCHDOG_WRITE_PROTECT_KEY);
      await writeEsptoolRegister(session, registers.WDTCONFIG1, WATCHDOG_TIMEOUT);
      await writeEsptoolRegister(session, registers.WDTCONFIG0, WATCHDOG_CHIP_RESET_VALUE);
      // Re-lock. Any value but the key locks it; the reset fires on the timeout.
      await writeEsptoolRegister(session, registers.WDTWPROTECT, 0);
      await sleep(POST_WATCHDOG_RESET_WAIT_MS);
      return 'watchdog';
    } catch (error) {
      // Expected on the way out: the chip can reset mid-sequence and take the bus
      // with it. Fall through to the weaker reset rather than reporting a failure
      // that may not be one.
      console.warn('[esp32] Watchdog chip-reset sequence failed:', error);
    }
  }

  try {
    await hardResetDevice(session);
    return 'hard-reset';
  } catch (error) {
    // Nothing left to try. The caller tells the user to press RST; the flash itself
    // is unaffected, so this is a warning and not a failure.
    console.warn('[esp32] Hard reset failed:', error);
    return null;
  }
}

/**
 * §10.1: write a flash plan to a device that is already in download mode.
 *
 * One executor serves both ESP32 paths (C3) — custom and stock differ only in what
 * the plan contains. The device must have been brought to download mode and
 * *confirmed* there by §10.3 first; this does not discriminate or enter.
 *
 * `plan.verify.sha256` is not checked here. It covers the provenance of the bytes
 * the resolver fetched and belongs with the fetch (§4.2 lists it as its own
 * module). What this verifies is the *write*: esptool compares its own MD5 of each
 * image against the device's `flashMd5sum` of the region it landed in, and throws
 * if they differ (§12.3 — that check is never stubbed).
 *
 * `onProgress` receives one continuous 0–1 fraction across all files, weighted by
 * byte count.
 *
 * @param {SerialPort} port  closed, in download mode
 * @param {import('./flash-plan.js').FlashPlan} plan
 * @param {{ onProgress?: (fraction: number) => void, onStatus?: (message: string) => void }} options
 * @returns {Promise<{ chipName: string, chipDescription: string, bytesWritten: number,
 *   reset: 'watchdog'|'hard-reset'|null }>}
 */
export async function executeFlashPlan(port, plan, { onProgress, onStatus } = {}) {
  if (plan.engine !== 'esptool') {
    throw new Error(`executeFlashPlan received a '${plan.engine}' plan; esptool only.`);
  }

  // §4.1 names the payload `data` without fixing its type, and esptool needs a
  // Uint8Array — it reads `.length` and slices. Normalise once, here.
  const files = plan.files.map(({ data, address }) => ({
    data: data instanceof Uint8Array ? data : new Uint8Array(data),
    address,
  }));
  const totalBytes = files.reduce((sum, file) => sum + file.data.length, 0);
  // Byte offset of each file within the whole write, so per-file progress maps onto
  // one continuous bar.
  const fileStartBytes = [];
  let running = 0;
  for (const file of files) {
    fileStartBytes.push(running);
    running += file.data.length;
  }

  onStatus?.('Connecting to the device…');
  let session;
  try {
    session = await openEsptoolSession(port);
  } catch (error) {
    throw new FlashWriteFailedError(
      `Could not start a flashing session (${error.message}). Nothing has been written.`,
      { cause: error, phase: 'connect' }
    );
  }

  try {
    // The full-chip erase runs before esptool's first progress callback and can take
    // most of a minute, so it is announced up front and the label flips once bytes
    // start moving.
    let erasing = plan.eraseAll;
    onStatus?.(erasing ? 'Erasing the device (this can take up to a minute)…' : 'Writing firmware…');

    try {
      await writeFlashFiles(session, {
        files,
        eraseAll: plan.eraseAll,
        onProgress: (fileIndex, written, total) => {
          if (erasing) {
            erasing = false;
            onStatus?.('Writing firmware…');
          }
          if (!onProgress || totalBytes === 0) return;
          const weight = files[fileIndex].data.length / totalBytes;
          const base = fileStartBytes[fileIndex] / totalBytes;
          onProgress(base + (total > 0 ? written / total : 1) * weight);
        },
      });
    } catch (error) {
      throw new FlashWriteFailedError(
        `Writing the firmware failed (${error.message}). The device is part-written ` +
          `and needs to be flashed again before it will run.`,
        { cause: error, phase: 'write' }
      );
    }
    onProgress?.(1);

    // Same session by necessity — see returnToApplication.
    const reset = await returnToApplication(session, { onStatus });
    return {
      chipName: session.chipName,
      chipDescription: session.chipDescription,
      bytesWritten: totalBytes,
      reset,
    };
  } finally {
    // The watchdog reset re-enumerates the device, so this usually closes a port
    // that is already gone. It still has to run: esptool keeps a reader locked on
    // the port, and the CLI probe that follows cannot open it until that is released.
    await closeEsptoolSession(session);
  }
}

/**
 * §10.5 input 1: the device's own partition table.
 *
 * A failed *read* raises — it must abort before anything is written or erased,
 * because "could not read" and "there is nothing there" license opposite actions
 * and only one of them is safe. An empty list is the other answer: the read
 * worked and the chip is blank.
 *
 * @returns {Promise<import('./partitions.js').Partition[]>}
 */
export async function readPartitionTable(session) {
  let raw;
  try {
    raw = await readFlashRegion(session, PARTITION_TABLE_OFFSET, PARTITION_TABLE_MAX_BYTES);
  } catch (error) {
    throw new FlashReadFailedError(
      `Could not read the device's partition table (${error.message}). Nothing has been ` +
        `written or erased — check the USB cable and port, then try again.`,
      { cause: error }
    );
  }
  return parsePartitionTable(raw);
}
