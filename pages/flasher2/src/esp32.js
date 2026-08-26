// ESP32 state and transitions: work out what state the device is in, move it to the one
// we need, write, and put it back. One module because it is one state machine.

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
import { readNodeConfig } from './cli-session.js';
import {
  hardResetDevice,
  probeEsptoolSync,
  readFlashChunked,
  readFlashRegion,
  resetIntoDownloadMode,
  writeEsptoolRegister,
  writeFlashFiles,
} from './esptool.js';
import {
  PARTITION_TABLE_MAX_BYTES,
  PARTITION_TABLE_OFFSET,
  findFilesystemPartition,
  isSpiffsPartition,
  parsePartitionTable,
  partitionTablesMatch,
} from './partitions.js';
import { buildSpiffsImage, readSpiffsFiles, spiffsUsedBytes } from './spiffs.js';
import { closeSerialPortQuietly, listGrantedSerialPorts } from './serial-port.js';


function sleep(milliseconds) {
  return new Promise((resolve) => setTimeout(resolve, milliseconds));
}

// Carries the board's own button wording (PGM+RST vs BOOT+RESET); the UI must not compose it.
export class ManualEntryRequiredError extends Error {
  constructor(instruction) {
    super(instruction);
    this.name = 'ManualEntryRequiredError';
    this.instruction = instruction;
  }
}

// The running app watches for this signal pattern and calls `esp_restart()` — native-USB
// boards have no RTS/DTR-to-EN wiring. Does nothing on a device with no cooperating app.
async function fireLegacyEntryGesture(port) {
  if (!port.readable) await port.open({ baudRate: CLI_BAUD_RATE });
  try {
    await port.setSignals({ dataTerminalReady: false });
    await port.setSignals({ requestToSend: true });
    await sleep(LEGACY_ENTRY_STEP_GAP_MS);
    await port.setSignals({ dataTerminalReady: true });
    await port.setSignals({ requestToSend: false });
  } finally {
    // The device is about to re-enumerate under the ROM identity, so this handle is
    // finished either way and the poll below needs it closed.
    await closeSerialPortQuietly(port);
  }
}

// Entry from legacy app mode changes the USB identity (`0x0002` -> `0x1001`), and Web Serial
// authorises per device, so the ROM interface may never have been granted here.
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
    // Only a ROM port that appeared *after* the gesture is the board we rebooted. With
    // two attached, matching any ROM port hands back the wrong one — and flashes it.
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

// Mechanism chosen by observed product id, the other tried on failure, then the
// manual instruction. Returns the port now in download mode, often not the one passed in.
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
    // A USB Serial/JTAG device keeps its identity across this reset, so success means
    // this same port is now in download mode.
    if (await resetIntoDownloadMode(port)) return port;
    return waitForRomPort(ROM_APPEAR_TIMEOUT_MS, before);
  }

  // The PID selects only *how* to talk to what is there. Each mechanism is tried first
  // where it is known to work, since neither works on the other's hardware.
  const mechanisms =
    observedProductId === LEGACY_CDC_PRODUCT_ID
      ? [attemptLegacyGesture, attemptUsbJtagReset]
      : [attemptUsbJtagReset, attemptLegacyGesture];

  for (const attempt of mechanisms) {
    const romPort = await attempt();
    if (!romPort) continue;

    // Affirmative confirmation before handoff — the whole point of the entry sequence.
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

// Affirmative signals only, CLI first: silence from both is a real state, and calling it
// "bootloader" is what erases a live device. Takes a closed port and returns it closed.
//
// The app's settings come back with the mode: `ver` proves the CLI is alive and the reads
// that follow ride the same session, because this is the last moment they are reachable —
// download mode is entered immediately after and serves no CLI.
export async function resolveEsp32Mode(port) {
  await port.open({ baudRate: CLI_BAUD_RATE });
  let config = null;
  try {
    config = await readNodeConfig(port);
  } finally {
    await closeSerialPortQuietly(port);
  }

  const version = config?.version ?? null;
  if (version) return { mode: ESP32_MODE.APP, version, config };

  // Passive — no reset is issued, so a device in state 3 is still in state 3
  // afterwards and can be reported to the user as it was found.
  const answeredSync = await probeEsptoolSync(port);
  return { mode: answeredSync ? ESP32_MODE.BOOTLOADER : ESP32_MODE.UNKNOWN, version: null, config: null };
}

// `phase` is what the UI must say next: nothing was touched on `connect`, whereas a
// `write` failure leaves the device part-written and unsafe to hand back.
export class FlashWriteFailedError extends Error {
  constructor(message, { cause, phase }) {
    super(message, { cause });
    this.name = 'FlashWriteFailedError';
    this.phase = phase;
  }
}

// Fatal by design — the decision this feeds is erase-vs-preserve.
export class FlashReadFailedError extends Error {
  constructor(message, { cause }) {
    super(message, { cause });
    this.name = 'FlashReadFailedError';
  }
}

// Leaving FORCE_DOWNLOAD_BOOT set strands the part in download mode. Needs a live session
// — nothing may re-sync the ROM between unlock and re-lock — and never throws.
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
      // Expected: the chip can reset mid-sequence and take the bus with it. Fall through
      // rather than report a failure that may not be one.
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

// One executor for both ESP32 paths. The caller owns the session and the reset out, since
// the restore must write in that same session. What is verified here is the write.
export async function executeFlashPlan(session, plan, { onProgress, onStatus } = {}) {
  if (plan.engine !== 'esptool') {
    throw new Error(`executeFlashPlan received a '${plan.engine}' plan; esptool only.`);
  }

  // The plan names the payload `data` without fixing its type, and esptool needs a
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

  // The full-chip erase runs before esptool's first progress callback and can take most
  // of a minute, so it is announced up front.
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

  return {
    chipName: session.chipName,
    chipDescription: session.chipDescription,
    bytesWritten: totalBytes,
  };
}

// A failed read raises: "could not read" and "there is nothing there"
// license opposite actions. An empty list is the blank-chip answer.
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

/**
 * Names that identify a MeshCore filesystem; any one is enough. Only the first two go
 * back to v1.0.0c, so dropping the old names would stop recognising the oldest devices.
 */
const MESHCORE_FILE_MARKERS = [
  '/identity/_main.id',
  '/node_prefs',
  '/com_prefs',
  '/prefs.json',
  '/regions2',
  '/s_contacts',
];

// Contents are the evidence, never enumeration. Empty or unparsable answers no, which is
// correct.
export function looksLikeMeshCore(files) {
  const bare = (name) => name.replace(/^\/+/, '');
  const present = new Set(files.map((file) => bare(file.name)));
  return MESHCORE_FILE_MARKERS.some((marker) => present.has(bare(marker)));
}

// The reading half; deciding is separate because a user declaration may override it.
// A failed read is fatal, but an *unparsable* filesystem just means nothing of ours.
export async function readFlashEvidence(session, plannedPartitions, { onProgress, onStatus, onNotice } = {}) {
  onStatus?.('Checking what is on the device…');
  const partitions = await readPartitionTable(session);
  const partition = findFilesystemPartition(partitions);

  // No filesystem partition is a legitimate reading, not a failure: a blank chip
  // has nothing to find. It is only reachable because the table read succeeded.
  if (!partition) {
    return {
      partitions,
      filesystem: { status: 'absent', partition: null, files: [], isMeshCore: false },
      layoutMatches: partitionTablesMatch(partitions, plannedPartitions),
    };
  }

  onStatus?.('Reading existing data…');
  let image;
  try {
    image = await readFlashChunked(session, partition.offset, partition.size, { onProgress, onNotice });
  } catch (error) {
    throw new FlashReadFailedError(
      `This device has existing data that could not be read back (${error.message}). Nothing ` +
        `has been written or erased, so nothing has been lost — try again, and if it keeps ` +
        `failing try a different USB cable or port.`,
      { cause: error }
    );
  }

  let files = [];
  try {
    files = readSpiffsFiles(image);
  } catch (error) {
    // Unparsable is not unreadable: the bytes came back fine and simply are not ours,
    // the same answer as another project's install.
    console.warn('[esp32] Could not parse the filesystem that was read back:', error);
  }

  return {
    partitions,
    filesystem: { status: 'ok', partition, image, files, isMeshCore: looksLikeMeshCore(files) },
    layoutMatches: partitionTablesMatch(partitions, plannedPartitions),
  };
}

/** @typedef {'app-slots-only'|'full-layout'} WriteScope */

// The deciding half. App slots only on positive evidence and nothing less. Not the
// same question as whether the user's data survives — that is the restore.
export function decideWriteScope(evidence) {
  if (evidence.filesystem.status !== 'ok') {
    return { scope: 'full-layout', reason: 'no filesystem was found on this device' };
  }
  if (!evidence.filesystem.isMeshCore) {
    return { scope: 'full-layout', reason: 'the filesystem on this device is not MeshCore\u2019s' };
  }
  if (!evidence.layoutMatches) {
    return { scope: 'full-layout', reason: 'the partition layout differs from the one being written' };
  }
  return { scope: 'app-slots-only', reason: 'a MeshCore filesystem on a matching layout' };
}

// A raw copy works only if the partition kept its size, since block magic derives from
// the block count. Never throws: the firmware is already written.
export async function restoreFilesystem(
  session,
  { evidence, plannedPartitions, eraseAll },
  { onStatus, onProgress } = {}
) {
  const backup = evidence.filesystem;
  if (backup.status !== 'ok' || !backup.isMeshCore) {
    return { action: 'skipped', reason: 'there was nothing of ours on this device to keep' };
  }
  if (backup.files.length === 0) {
    return { action: 'skipped', reason: 'the filesystem held no files' };
  }

  const target = findFilesystemPartition(plannedPartitions);
  if (!target) {
    return { action: 'skipped', reason: 'the firmware being written has no filesystem partition' };
  }

  try {
    onStatus?.('Restoring your settings…');
    const resized = backup.partition.size !== target.size;

    if (resized) {
      if (!isSpiffsPartition(backup.partition)) {
        // LittleFS lays out differently and this cannot rebuild it. Refusing is the
        // safe answer: a raw copy into a resized partition is worse than none.
        return { action: 'skipped', reason: 'the partition changed size and this filesystem is not SPIFFS' };
      }
      // The whole partition, not just the used part: the magic in the *unused*
      // blocks is what marks the rest of it formatted.
      const image = buildSpiffsImage(backup.files, target.size);
      await writeFilesystemImage(session, target.offset, image, onProgress);
      return {
        action: 'rebuilt',
        reason: `rebuilt ${backup.files.length} file(s) for a ${Math.round(target.size / 1024)}K partition`,
      };
    }

    const usedBytes = isSpiffsPartition(backup.partition)
      ? spiffsUsedBytes(backup.image)
      : backup.image.length;
    if (usedBytes === 0) {
      return { action: 'skipped', reason: 'the filesystem had nothing allocated' };
    }
    // Same offset, same size, and nothing erased — what is on the device is already
    // what would be written back.
    if (!eraseAll && backup.partition.offset === target.offset) {
      return { action: 'skipped', reason: 'the filesystem partition was never disturbed' };
    }

    // The whole partition, never just the used part: without magic in the remaining
    // blocks SPIFFS reformats. Measured — a 16K prefix cost the bench node its identity.
    await writeFilesystemImage(session, target.offset, backup.image, onProgress);
    return { action: 'raw-copy', reason: `copied back ${Math.round(usedBytes / 1024)}K of data` };
  } catch (error) {
    // Deliberately swallowed: see the contract above. The user loses settings, not
    // a working device, and the reason reaches them through the return value.
    console.warn('[esp32] Could not restore the filesystem:', error);
    return { action: 'skipped', reason: `the restore failed (${error.message})` };
  }
}

function writeFilesystemImage(session, address, data, onProgress) {
  return writeFlashFiles(session, {
    files: [{ data, address }],
    // Never here: this runs *after* the main write, and erasing the whole chip now
    // would take the firmware with it. The write erases the region it covers.
    eraseAll: false,
    onProgress: (_fileIndex, written, total) => onProgress?.(total > 0 ? written / total : 1),
  });
}
