// nRF52 device transitions and DFU execution — §11.1-§11.3. Peer of `esp32.js`:
// same shape, different MCU family. Entry, port re-acquisition, write.
//
// §11.7's exit back to the application is not written into the spec yet and is
// deliberately absent here.

import {
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

// `phase` is what the UI must say next, as on ESP32: nothing was written on `connect`,
// whereas a `write` failure leaves the device part-written.
export class DfuWriteFailedError extends Error {
  constructor(message, { cause, phase }) {
    super(message, { cause });
    this.name = 'DfuWriteFailedError';
    this.phase = phase;
  }
}

// §11.2. `forceDfuMode` opens the port at 1200 baud, holds, closes and waits 1.5 s
// internally — do not add a settle on top. It returns nothing and the port it was
// handed is dead afterwards.
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

// Only a port that appeared *after* the touch can be the device we just rebooted — the
// same rule as `waitForRomPort` on ESP32. Returns null when the DFU identity has never
// been granted to this origin, which is the common case (§11.3).
async function newlyGrantedPort(before) {
  const granted = await listGrantedSerialPorts();
  return granted.find((port) => !before.includes(port)) ?? null;
}

// Re-acquire after any transition, rather than predicting what the device will do.
// Whether a board changes USB identity — on entering DFU, after an erase package, on
// booting the application — is device-specific, so ask the bus instead of assuming.
// A device that re-enumerated comes back as a new entry and is preferred; one that kept
// its identity leaves the held port as the best candidate.
async function reacquireAfterTransition(before, heldPort, { prompt, onStatus, reenter } = {}) {
  return acquireUsableSerialPort({
    preferredPort: (await newlyGrantedPort(before)) ?? heldPort,
    prompt: prompt ?? 'Select the port to continue.',
    onStatus,
    reenterProgrammingMode: reenter ?? null,
  });
}

/**
 * §11.2 + §11.3. Takes the running application's port, returns the DFU port.
 * Raises PortSelectionRequiredError when the DFU identity was never granted — the UI
 * answers that with a picker button (§11.4), never this module.
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
 * §11.1. The DFU executor, peer of `executeFlashPlan`. Takes a closed port — `dfuUpdate`
 * opens it itself and closes it again on the way out — and returns the port it finished
 * on, which a retry may have replaced. `onProgress` is one 0-1 fraction across both stages.
 */
export async function executeDfuPlan(port, plan, { onProgress, onStatus } = {}) {
  if (plan.engine !== 'dfu') {
    throw new Error(`executeDfuPlan received a '${plan.engine}' plan; dfu only.`);
  }
  const { Dfu } = await loadDfuApi();
  let target = port;

  // `eraseBeforeUpdate` erases the application region only — not the filesystem. A wipe is
  // the erase package, its own DFU update on the same port, ahead of the firmware (§11.3).
  const stages = plan.erasePackage
    ? [
        { package: plan.erasePackage, label: 'Erasing the device…' },
        { package: plan.package, label: 'Writing firmware…' },
      ]
    : [{ package: plan.package, label: 'Writing firmware…' }];
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

  for (let attempt = 1; attempt <= DFU_FLASH_ATTEMPTS; attempt += 1) {
    await closeSerialPortQuietly(target);
    onStatus?.(stage.label);

    const dfu = new Dfu(target, eraseAll);
    let started = false;
    try {
      await dfu.dfuUpdate(stage.package, (percent) => {
        started = true;
        onProgress?.(percent / 100);
      });
      return target;
    } catch (error) {
      // `dfu.js` owns the open, so a port that would not open arrives as an untyped
      // DOMException. Retry on "no byte has moved yet" rather than matching Chrome's
      // message text (§12.3); a part-written device is never retried silently.
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

// §11.7. Best-effort: a failure to open or signal is not an error, because unplugging does
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
 * §11.7. Returns `{ port, method }` — `self` when the bootloader booted the application on
 * its own, `dtr` after the gesture, `unconfirmed` when a usable port is there but nothing
 * distinguishes it from the DFU port we started on, and a null port when nothing came back.
 * Confirming *which* firmware runs is the caller's: only it knows if the role answers.
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

// Absence is an answer here, not a failure — §11.5's manual route is what follows it.
async function tryReacquire(before, heldPort, onStatus) {
  try {
    return await reacquireAfterTransition(before, heldPort, { onStatus });
  } catch (error) {
    if (error instanceof PortSelectionRequiredError) return null;
    throw error;
  }
}
