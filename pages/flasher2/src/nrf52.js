// nRF52 device transitions and DFU execution — §11.1-§11.3. Peer of `esp32.js`:
// same shape, different MCU family. Entry, port re-acquisition, write.
//
// §11.7's exit back to the application is not written into the spec yet and is
// deliberately absent here.

import { DFU_FLASH_ATTEMPTS } from './constants.js';
import {
  acquireUsableSerialPort,
  closeSerialPortQuietly,
  listGrantedSerialPorts,
  waitForUsableSerialPort,
} from './serial-port.js';

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

/**
 * §11.2 + §11.3. Takes the running application's port, returns the DFU port.
 * Raises PortSelectionRequiredError when the DFU identity was never granted — the UI
 * answers that with a picker button (§11.4), never this module.
 */
export async function enterDfuMode(appPort, { prompt, onStatus } = {}) {
  onStatus?.('Asking the device to enter DFU mode…');
  const before = await listGrantedSerialPorts();
  await touchIntoDfuMode(appPort);

  // Entering DFU re-enumerates the board as a different USB device, so the ladder is
  // given the new port when one appeared and the old one otherwise — a touch that did
  // nothing (already in DFU) leaves the handed-in port as the best candidate.
  return acquireUsableSerialPort({
    preferredPort: (await newlyGrantedPort(before)) ?? appPort,
    prompt: prompt ?? 'Select the DFU port to continue.',
    onStatus,
    reenterProgrammingMode: () => retouchQuietly(appPort),
  });
}

/**
 * §11.1. The DFU executor, peer of `executeFlashPlan`. Takes a closed port — `dfuUpdate`
 * opens it itself and closes it again on the way out — and returns the port it finished
 * on, which a retry may have replaced. `onProgress` is one 0-1 fraction.
 */
export async function executeDfuPlan(port, plan, { onProgress, onStatus } = {}) {
  if (plan.engine !== 'dfu') {
    throw new Error(`executeDfuPlan received a '${plan.engine}' plan; dfu only.`);
  }
  const { Dfu } = await loadDfuApi();
  let target = port;

  for (let attempt = 1; attempt <= DFU_FLASH_ATTEMPTS; attempt += 1) {
    await closeSerialPortQuietly(target);
    onStatus?.('Writing firmware…');

    // `eraseBeforeUpdate` erases the application region only, not the filesystem; a
    // filesystem wipe is upstream's separate erase package, which nothing resolves yet.
    const dfu = new Dfu(target, plan.eraseAll);
    let started = false;
    try {
      await dfu.dfuUpdate(plan.package, (percent) => {
        started = true;
        onProgress?.(percent / 100);
      });
      onProgress?.(1);
      return { port: target, bytesWritten: plan.package.size };
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
