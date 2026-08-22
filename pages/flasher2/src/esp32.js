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
    // The device is about to disconnect and re-enumerate under the ROM's identity
    // — this port object is finished either way, and the poll below needs it
    // closed rather than still held open.
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

// §10.3. Mechanism chosen by observed product id, the other tried on failure, then the
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

// §10.2. Affirmative signals only. CLI first: a running application is the state that must
// never be misread, and only its silence licenses asking the ROM. Silence from both is a
// real, verified state — calling it "bootloader" is what erases a live device.
// Takes a closed port and returns it closed.
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

// §10.4. `hard_reset` does not re-sample the boot strapping pins on these parts, and
// leaving FORCE_DOWNLOAD_BOOT set — as esptool's `--after watchdog-reset` does — strands
// the part in download mode. Needs a live session: nothing may re-sync the ROM between
// the unlock and the re-lock. Never throws; the write it follows already succeeded.
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

// §10.1. One executor for both ESP32 paths (C3). Takes an open session, like the evidence
// read and the restore: the restore has to write in the same download-mode session, before
// the exit, so the caller owns the session and the reset out. `plan.verify.sha256` is the
// resolver's business (§4.2) — what is verified here is the write, via esptool's MD5
// against the device's own `flashMd5sum`. `onProgress` is one 0-1 fraction across all files.
export async function executeFlashPlan(session, plan, { onProgress, onStatus, verifyWrite = true } = {}) {
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

  // The full-chip erase runs before esptool's first progress callback and can take
    // most of a minute, so it is announced up front and the label flips once bytes
    // start moving.
  let erasing = plan.eraseAll;
  onStatus?.(erasing ? 'Erasing the device (this can take up to a minute)…' : 'Writing firmware…');

  try {
    await writeFlashFiles(session, {
      files,
      eraseAll: plan.eraseAll,
      verifyWrite,
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

// §10.5 input 1. A failed read raises: "could not read" and "there is nothing there"
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
 * Names that identify a MeshCore filesystem, and the release each arrived in.
 *
 * Only the first two go back to v1.0.0c; `/com_prefs` came in v1.4.1,
 * `/s_contacts` v1.9.0, `/regions2` v1.10.0 and `/prefs.json` v1.17.0. Dropping
 * the older names would stop recognising older devices — which is precisely the
 * case where the user has the most to lose. Any one of them is enough: a v1.17
 * device flashed fresh may carry only `/identity/_main.id` and `/prefs.json`,
 * while an upgraded one still has the legacy files alongside.
 */
const MESHCORE_FILE_MARKERS = [
  '/identity/_main.id',
  '/node_prefs',
  '/com_prefs',
  '/prefs.json',
  '/regions2',
  '/s_contacts',
];

// §10.5 input 3. Contents are the evidence, never enumeration (C1). Only the first two
// markers go back to v1.0.0c, so dropping the older names would stop recognising exactly
// the devices with most to lose. Empty or unparsable answers no, which is correct.
export function looksLikeMeshCore(files) {
  const bare = (name) => name.replace(/^\/+/, '');
  const present = new Set(files.map((file) => bare(file.name)));
  return MESHCORE_FILE_MARKERS.some((marker) => present.has(bare(marker)));
}

// §10.5, reading half. Deciding is separate because an explicit user declaration may
// override the evidence entirely. Both reads are fatal on failure; an *unparsable*
// filesystem is a different answer and means nothing of ours is here.
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
    // Unparsable is not unreadable. The bytes came back fine; they are simply not a
    // filesystem this understands, which answers "nothing of ours here" — the same
    // answer as another project's install, and it is handled the same way.
    console.warn('[esp32] Could not parse the filesystem that was read back:', error);
  }

  return {
    partitions,
    filesystem: { status: 'ok', partition, image, files, isMeshCore: looksLikeMeshCore(files) },
    layoutMatches: partitionTablesMatch(partitions, plannedPartitions),
  };
}

/** @typedef {'app-slots-only'|'full-layout'} WriteScope */

// §10.5, deciding half. App slots only on positive evidence and nothing less. Not the
// same question as whether the user's data survives — that is §10.6's restore.
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

// §10.6. A raw copy works only if the partition kept its size: every block's lookup magic
// derives from the image's block count, so the same bytes in a resized partition mount as
// unformatted and get reformatted away. Never throws — the firmware is already written,
// and failing the flash over a lost setting would leave an unusable device. ESP32 only (C5).
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

    // The whole partition, never just the used part: after an erase the remaining blocks
    // carry no lookup magic, so SPIFFS mounts the image as unformatted and reformats it
    // away. Measured — a 16K prefix cost the bench node its identity.
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
