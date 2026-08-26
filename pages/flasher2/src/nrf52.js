// nRF52 transitions and DFU execution: entry, port re-acquisition, write. Peer of
// `esp32.js`, same shape and a different MCU family.

import {
  CLI_BAUD_RATE,
  DFU_FLASH_ATTEMPTS,
  DFU_POST_ERASE_SETTLE_MS,
  DFU_RESET_DTR_HIGH_MS,
  DFU_RESET_DTR_LOW_MS,
  DFU_RESET_SETTLE_MS,
  PORT_PROBE_BAUD_RATE,
} from './constants.js';
import {
  PortSelectionRequiredError,
  acquireUsableSerialPort,
  closeSerialPortQuietly,
  listGrantedSerialPorts,
  waitForUsableSerialPort,
} from './serial-port.js';
import { probeBootloaderVersion, readNodeConfig } from './cli-session.js';
import { validateDfuPackage } from './flash-plan.js';

function sleep(milliseconds) {
  return new Promise((resolve) => setTimeout(resolve, milliseconds));
}

const DFU_JS_URL = '../vendor/dfu/dfu.js';

let loadedApi = null;

/** Loaded on first use — it pulls in a 171 KB zip reader the ESP32 path never needs. */
export async function loadDfuApi() {
  if (!loadedApi) loadedApi = await import(DFU_JS_URL);
  return loadedApi;
}

// Mirrors `PortSelectionRequiredError`: `showSaveFilePicker()` needs a fresh click, so a
// step that calls it from `run()` (no gesture) must throw this instead — the UI answers it
// with a checkpoint button, whose click is what actually calls the picker.
export class FilePickerRequiredError extends Error {
  constructor(prompt, { bytes, suggestedName }) {
    super(prompt);
    this.name = 'FilePickerRequiredError';
    this.prompt = prompt;
    this.bytes = bytes;
    this.suggestedName = suggestedName;
  }
}

// `phase` is what the UI must say next, as on ESP32: nothing was written on `connect`,
// whereas a `write` failure leaves the device part-written.
export class DfuWriteFailedError extends Error {
  constructor(message, { cause, phase }) {
    super(message, { cause });
    this.name = 'DfuWriteFailedError';
    this.phase = phase;
  }
}

// `forceDfuMode` holds at 1200 baud, closes and waits 1.5 s internally — do not add a
// settle. It returns nothing and the port it was handed is dead afterwards.
async function touchIntoDfuMode(port) {
  const { Dfu } = await loadDfuApi();
  await closeSerialPortQuietly(port);
  await Dfu.forceDfuMode(port);
}

// Re-entry is best-effort: after re-enumeration the application port object is dead and
// opening it raises. A failure here just means the ladder escalates to the picker.
async function retouchQuietly(port) {
  try {
    await touchIntoDfuMode(port);
  } catch (error) {
    console.warn('[nrf52] Re-entry into DFU mode failed:', error);
  }
}

// Only a port that appeared *after* the touch can be the device we just rebooted. Null
// when the DFU identity was never granted here, which is the common case.
async function newlyGrantedPort(before) {
  const granted = await listGrantedSerialPorts();
  return granted.find((port) => !before.includes(port)) ?? null;
}

// Re-acquire after any transition rather than predicting it: whether a board changes USB
// identity is device-specific, so ask the bus. A new entry wins over the held port.
async function reacquireAfterTransition(before, heldPort, { prompt, onStatus, reenter } = {}) {
  return acquireUsableSerialPort({
    preferredPort: (await newlyGrantedPort(before)) ?? heldPort,
    prompt: prompt ?? 'Select the port to continue.',
    onStatus,
    reenterProgrammingMode: reenter ?? null,
  });
}

/**
 * Everything the CLI can tell us before a DFU transition, in one session: the bootloader
 * version the OTAFIX gate reads, and the settings the Upgrade path pre-fills from. DFU
 * serves no CLI, so this is the only window for either.
 *
 * Takes a closed port and returns it closed, like esp32's `resolveEsp32Mode`. Returns
 * `{ bootloaderVersion, config }`, both nullable — silence is "cannot tell", never
 * evidence that a device lacks a bootloader.
 */
export async function readAppState(port) {
  await port.open({ baudRate: CLI_BAUD_RATE });
  try {
    // Settings first: its `ver` probe is the cheap liveness gate, and it returns early
    // on silence rather than letting each read burn the full command timeout.
    const config = await readNodeConfig(port);
    // A device with no CLI cannot answer this either — skip it rather than wait again.
    const bootloaderVersion = config?.version ? await probeBootloaderVersion(port) : null;
    return { bootloaderVersion, config };
  } finally {
    await closeSerialPortQuietly(port);
  }
}

/**
 * Writes a bootloader UF2 to the mounted drive via the file-system picker, then waits for
 * the device to come back as the application on USB. The bootloader boots the app once the
 * write lands — measured on a SenseCAP P1, never over DFU. `w.close()` throwing is normal:
 * the board reboots as the last block lands and the drive unmounts before close() settles.
 *
 * Must be called from a real click — `showSaveFilePicker()` needs a fresh user gesture, so
 * this cannot be called from a step's `run()` directly. Throw `FilePickerRequiredError`
 * there instead; the UI calls this from the checkpoint button it renders in response.
 */
export async function writeBootloaderUf2(appPort, bytes, suggestedName, { onStatus } = {}) {
  if (typeof window === 'undefined' || !window.showSaveFilePicker) {
    throw new Error('This browser cannot write to the UF2 drive directly — use Chrome or Edge.');
  }
  // The picker must be the first thing called here — any await ahead of it risks the
  // browser deciding the click's user activation has lapsed, which can fail the call
  // silently rather than throwing something a caller could act on.
  const handle = await window.showSaveFilePicker({
    suggestedName,
    types: [{ description: 'UF2 firmware', accept: { 'application/octet-stream': ['.uf2'] } }],
  });
  const before = await listGrantedSerialPorts();
  onStatus?.('Writing the bootloader…');
  const writable = await handle.createWritable();
  await writable.write(bytes);
  try {
    await writable.close();
  } catch (error) {
    console.warn('[nrf52] writable.close() threw after a UF2 write — normal, board rebooted:', error);
  }
  onStatus?.('Waiting for the device to come back…');
  return reacquireAfterTransition(before, appPort, {
    prompt: 'Select the device to continue.',
    onStatus,
  });
}

/**
 * Takes the running application's port, returns the DFU port. Raises selection-required
 * when the DFU identity was never granted; the UI answers that, never this module.
 */
export async function enterDfuMode(appPort, { prompt, onStatus } = {}) {
  onStatus?.('Asking the device to enter DFU mode…');
  const before = await listGrantedSerialPorts();
  await touchIntoDfuMode(appPort);

  return reacquireAfterTransition(before, appPort, {
    prompt: prompt ?? 'Select the DFU port to continue.',
    onStatus,
    reenter: () => retouchQuietly(appPort),
  });
}

/**
 * The DFU executor, peer of `executeFlashPlan`. Takes a closed port and returns the one it
 * finished on, which a retry may have replaced.
 */
export async function executeDfuPlan(port, plan, { onProgress, onStatus } = {}) {
  if (plan.engine !== 'dfu') {
    throw new Error(`executeDfuPlan received a '${plan.engine}' plan; dfu only.`);
  }
  const { Dfu } = await loadDfuApi();
  let target = port;

  // Order is load-bearing: the erase package clears the filesystem, then the firmware goes
  // back. The bootloader is a guided pre-flash step now, never a stage here — see
  // nrf52-bootloader-plan.md.
  const stages = [];
  if (plan.erasePackage) stages.push({ package: plan.erasePackage, label: 'Erasing the device…' });
  stages.push({ package: plan.package, label: 'Writing firmware…' });
  const totalBytes = stages.reduce((sum, stage) => sum + stage.package.size, 0);
  let doneBytes = 0;

  // Baseline for spotting a re-enumeration: taken before a write, compared after it.
  // Sampling it afterwards would already include the new port and find nothing "new".
  let before = await listGrantedSerialPorts();

  for (const [index, stage] of stages.entries()) {
    if (index > 0) {
      // Measured on the T1: the erase package re-enumerates the board, so the port held
      // from the previous stage is dead. Re-acquire rather than discovering it by failing.
      await sleep(DFU_POST_ERASE_SETTLE_MS);
      onStatus?.('Reconnecting to the DFU port…');
      target = await reacquireAfterTransition(before, target, { onStatus });
    }
    before = await listGrantedSerialPorts();
    const weight = stage.package.size / totalBytes;
    const base = doneBytes / totalBytes;

    target = await writeDfuPackage(Dfu, target, stage, plan.eraseAll, {
      onStatus,
      onProgress: onProgress && ((fraction) => onProgress(base + fraction * weight)),
    });
    doneBytes += stage.package.size;
  }

  onProgress?.(1);
  return { port: target, bytesWritten: totalBytes };
}

// One stage. Returns the port it succeeded on — a retry may have replaced it.
async function writeDfuPackage(Dfu, port, stage, eraseAll, { onProgress, onStatus }) {
  let target = port;

  // `dfu.js` reads an application package for itself; anything else it cannot read at all,
  // so the manifest is parsed here and handed to the transport directly.
  const parsed = await validateDfuPackage(stage.package, { withBytes: true });
  const readableByDonor = parsed.kind === 'application';

  for (let attempt = 1; attempt <= DFU_FLASH_ATTEMPTS; attempt += 1) {
    await closeSerialPortQuietly(target);
    onStatus?.(stage.label);

    const dfu = new Dfu(target, eraseAll);
    let started = false;
    const report = (fraction) => {
      started = true;
      onProgress?.(fraction);
    };
    try {
      if (readableByDonor) {
        await dfu.dfuUpdate(stage.package, (percent) => report(percent / 100));
      } else {
        await sendParsedPackage(dfu, parsed, report);
      }
      return target;
    } catch (error) {
      // `dfu.js` owns the open, so a refused port arrives untyped. Retry on "no byte has
      // moved yet"; a part-written device is never retried silently.
      if (started || attempt === DFU_FLASH_ATTEMPTS) {
        throw new DfuWriteFailedError(
          started
            ? `Writing the firmware failed (${error.message}). The device is part-written ` +
              `and needs to be flashed again before it will run.`
            : `Could not start the DFU transfer (${error.message}).`,
          { cause: error, phase: started ? 'write' : 'connect' }
        );
      }
      onStatus?.('Waiting for the DFU port to become ready…');
      target = await waitForUsableSerialPort(target, { onStatus });
    }
  }
}

/**
 * Sends a package `dfu.js` cannot read itself: `dfuUpdate` hardcodes mode 4, so a bootloader
 * package never reaches the transport. Unverified beyond mode 4; never call it speculatively.
 */
async function sendParsedPackage(dfu, parsed, onProgress) {
  await dfu.port.open({ baudRate: PORT_PROBE_BAUD_RATE });
  try {
    // `eraseFlash` walks pages from address 0 — the application region. Correct for an
    // application update and actively wrong for anything that writes the bootloader.
    if (dfu.eraseBeforeUpdate && parsed.kind === 'application') {
      await dfu.eraseFlash(parsed.applicationSize);
    }
    await dfu.sendStartDfu(
      parsed.mode,
      parsed.softdeviceSize,
      parsed.bootloaderSize,
      parsed.applicationSize ?? 0
    );
    await dfu.sendInitPacket(parsed.dat);
    await dfu.sendFirmware(parsed.bin, (percent) => onProgress?.(percent / 100));
  } finally {
    // Mirrors `dfuUpdate`'s own teardown: the reader holds a lock the next open would fail on.
    if (dfu.port?.readable) {
      try {
        const reader = dfu.port.readable.getReader();
        await reader.cancel();
        reader.releaseLock();
      } catch {
        // A reader we cannot reach is one the close below discards anyway.
      }
    }
    await closeSerialPortQuietly(dfu.port);
  }
}

// Best-effort: a failure to open or signal is not an error, because unplugging does
// the same thing and the write has already succeeded.
async function toggleDtrReset(port) {
  await closeSerialPortQuietly(port);
  try {
    await port.open({ baudRate: PORT_PROBE_BAUD_RATE });
    await port.setSignals({ dataTerminalReady: false });
    await sleep(DFU_RESET_DTR_LOW_MS);
    await port.setSignals({ dataTerminalReady: true });
    await sleep(DFU_RESET_DTR_HIGH_MS);
    await sleep(DFU_RESET_SETTLE_MS);
  } catch (error) {
    console.warn('[nrf52] DFU reset gesture failed:', error);
  } finally {
    await closeSerialPortQuietly(port);
  }
}

/**
 * Returns `{ port, method }`: `self`, `dtr`, `unconfirmed` when a port is there but
 * indistinguishable from the DFU one, or a null port. Confirming firmware is the caller's.
 */
export async function returnToApplication(dfuPort, { appPort = null, onStatus } = {}) {
  onStatus?.('Restarting the device…');
  const before = await listGrantedSerialPorts();

  // Ask the bus before reaching for the reset. Whether a bootloader boots the application
  // by itself is device-specific — the T1 does — and re-acquiring costs nothing when it has.
  const returned = await tryReacquire(before, appPort ?? dfuPort, onStatus);
  if (returned && returned !== dfuPort) return { port: returned, method: 'self' };

  await toggleDtrReset(dfuPort);
  const afterReset = await tryReacquire(before, appPort ?? dfuPort, onStatus);
  if (afterReset && afterReset !== dfuPort) return { port: afterReset, method: 'dtr' };

  // A device that never re-enumerates hands back the same object in both modes, so
  // "still in DFU" and "back in the application" are indistinguishable from here.
  return { port: afterReset, method: afterReset ? 'unconfirmed' : null };
}

// Absence is an answer here, not a failure — the manual UF2 route is what follows it.
async function tryReacquire(before, heldPort, onStatus) {
  try {
    return await reacquireAfterTransition(before, heldPort, { onStatus });
  } catch (error) {
    if (error instanceof PortSelectionRequiredError) return null;
    throw error;
  }
}
