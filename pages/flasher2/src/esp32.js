// ESP32 device state and transitions — §10.2 discrimination, §10.3 entry.
//
// One module because it is one state machine: work out what state the device is
// in, move it to the state we need, and (later, §10.4) put it back. The steps
// share constants, imports and call order; splitting them would follow the
// specification's headings rather than anything that changes independently.
//
// ESP32 only. The nRF52 equivalents are §11.2-§11.4.

import {
  CLI_BAUD_RATE,
  ESPRESSIF_VENDOR_ID,
  LEGACY_CDC_PRODUCT_ID,
  LEGACY_ENTRY_STEP_GAP_MS,
  PORT_RESCAN_INTERVAL_MS,
  ROM_APPEAR_TIMEOUT_MS,
  ROM_BOOTLOADER_PRODUCT_ID,
  ROM_PORT_SETTLE_MS,
} from './constants.js';
import { probeCliVersion } from './cli-session.js';
import { probeEsptoolSync, resetIntoDownloadMode } from './esptool.js';
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
