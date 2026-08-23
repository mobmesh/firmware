// Port acquisition, one implementation shared by every consumer. Each function adds one
// fallback over the one below it; the ordering is load-bearing.

import {
  PORT_CONNECT_TIMEOUT_MS,
  PORT_OPEN_PROBE_ATTEMPTS,
  PORT_OPEN_RETRY_DELAY_MS,
  PORT_PROBE_BAUD_RATE,
  PORT_RESCAN_INTERVAL_MS,
  PORT_SETTLE_AFTER_CONNECT_MS,
  ESPRESSIF_VENDOR_ID,
  NORDIC_UF2_VENDOR_ID,
} from './constants.js';
// Typed errors: a failure the user must act on carries its payload as data, never as
// message text for a caller to pattern-match.

/**
 * The only failure that escalates to a user gesture; carries the picker's prompt.
 */
export class PortSelectionRequiredError extends Error {
  constructor(prompt) {
    super(prompt);
    this.name = 'PortSelectionRequiredError';
    this.prompt = prompt;
  }
}

// Typed at the `port.open()` boundary — the donor message-matches Chrome's DOMException
// wording, which is neither stable API nor locale-independent.
export class PortOpenFailedError extends Error {
  constructor(cause) {
    super('Could not open the serial port.');
    this.name = 'PortOpenFailedError';
    this.cause = cause;
  }
}

/** Web Serial absent or unusable in this context. Terminal — no retry ladder. */
export class SerialUnavailableError extends Error {
  constructor(message) {
    super(message);
    this.name = 'SerialUnavailableError';
  }
}

function sleep(milliseconds) {
  return new Promise((resolve) => setTimeout(resolve, milliseconds));
}

function serialApi() {
  if (!('serial' in navigator)) {
    throw new SerialUnavailableError('Web Serial is not available in this browser.');
  }
  return navigator.serial;
}

/**
 * Ports granted to this origin, so repeat visitors skip the picker.
 */
export async function listGrantedSerialPorts() {
  return serialApi().getPorts();
}

/**
 * The picker. Needs a user gesture, so the ladder raises instead of calling it and the
 * UI calls this from the resulting click.
 */
export async function promptForSerialPort() {
  return serialApi().requestPort();
}

// Matches on `getInfo()`, not object identity: `getPorts()` returning the same instances
// is true in practice and false across a re-enumeration.
function isSamePort(a, b) {
  return a === b;
}

// Raise rather than infer. readable/writable are both null on any closed port, so
// guessing from them reads a present device as absent and waits forever.
function isPortConnected(port) {
  if (typeof port.connected !== 'boolean') {
    throw new SerialUnavailableError('This browser does not report serial port connection state.');
  }
  return port.connected;
}

/** Close if open. Failure here is not actionable — see the catch. */
export async function closeSerialPortQuietly(port) {
  if (!port.readable && !port.writable) return;
  try {
    await port.close();
  } catch {
    // A close that fails leaves the port in the state the next open must handle
    // anyway; every caller's next move is to open or to abandon the port.
  }
}

/** Every open in the tool goes through here, so failures arrive typed. */
async function openSerialPort(port, options) {
  try {
    await port.open(options);
  } catch (error) {
    throw new PortOpenFailedError(error);
  }
}

// Side-effecting by necessity — an open/close cycle is the only reliable readiness signal.
// Harmless on these boards; confirmed on the bench.
async function probeSerialPortUsable(port) {
  await closeSerialPortQuietly(port);
  try {
    await openSerialPort(port, { baudRate: PORT_PROBE_BAUD_RATE });
    return true;
  } catch (error) {
    if (error instanceof PortOpenFailedError) return false;
    throw error;
  } finally {
    await closeSerialPortQuietly(port);
  }
}

/**
 * Firmware family from the USB vendor. 'unknown' rather than a guess: a legacy ESP32
 * behind a CP210x or CH340 bridge reports the bridge, and defaulting would be wrong.
 */
export function deviceFamily(port) {
  switch (port.getInfo().usbVendorId) {
    case ESPRESSIF_VENDOR_ID: return 'esp32';
    case NORDIC_UF2_VENDOR_ID: return 'nrf52';
    default: return 'unknown';
  }
}

function sameUsbIdentity(a, b) {
  return a.usbVendorId === b.usbVendorId && a.usbProductId === b.usbProductId;
}

// Listener plus poll, each covering the other's blind spot. Never waits on the handed-in
// port: a re-enumerated device is a new SerialPort and the old one never fires again.
async function waitForDeviceOnBus(port, timeoutMs) {
  if (isPortConnected(port)) return port;

  const wanted = port.getInfo();

  let announceArrival;
  const arrivedByEvent = new Promise((resolve) => {
    announceArrival = resolve;
  });
  const onConnect = (event) => {
    if (sameUsbIdentity(event.target.getInfo(), wanted)) announceArrival(event.target);
  };
  navigator.serial.addEventListener('connect', onConnect);

  try {
    const deadline = Date.now() + timeoutMs;
    do {
      const granted = await listGrantedSerialPorts();
      const live = granted.find(
        (candidate) => isPortConnected(candidate) && sameUsbIdentity(candidate.getInfo(), wanted),
      );
      if (live) return live;

      const arrived = await Promise.race([
        arrivedByEvent,
        sleep(PORT_RESCAN_INTERVAL_MS).then(() => null),
      ]);
      if (arrived) return arrived;
    } while (Date.now() < deadline);

    throw new PortSelectionRequiredError('Timed out waiting for the device to reconnect.');
  } finally {
    navigator.serial.removeEventListener('connect', onConnect);
  }
}

/**
 * Wait for a port to become usable, or raise selection-required.
 */
export async function waitForUsableSerialPort(port, { prompt, onStatus } = {}) {
  const selectionPrompt = prompt ?? 'Select the serial port to continue.';

  await closeSerialPortQuietly(port);
  // The device may come back as a different port object than the one handed in;
  // everything after this point works with what the bus actually returned.
  const live = await waitForDeviceOnBus(port, PORT_CONNECT_TIMEOUT_MS);
  await sleep(PORT_SETTLE_AFTER_CONNECT_MS);

  onStatus?.('Waiting for the port to become ready…');

  for (let attempt = 1; attempt <= PORT_OPEN_PROBE_ATTEMPTS; attempt += 1) {
    if (await probeSerialPortUsable(live)) return live;
    if (attempt < PORT_OPEN_PROBE_ATTEMPTS) await sleep(PORT_OPEN_RETRY_DELAY_MS);
  }

  throw new PortSelectionRequiredError(selectionPrompt);
}

/** One rung down: a port that never becomes usable is a null, not a throw. */
async function tryUsableSerialPort(port, options) {
  try {
    return await waitForUsableSerialPort(port, options);
  } catch (error) {
    if (error instanceof PortSelectionRequiredError) return null;
    throw error;
  }
}

function orderPortsPreferredFirst(ports, preferredPort) {
  if (!preferredPort) return ports;
  const others = ports.filter((port) => !isSamePort(port, preferredPort));
  return ports.some((port) => isSamePort(port, preferredPort))
    ? [preferredPort, ...others]
    : ports;
}

// The preferred port is re-tried even if the caller already did: a reset can move the
// device between attempts. Do not skip the repeat.
async function firstUsableGrantedPort(preferredPort, options) {
  const granted = await listGrantedSerialPorts();
  for (const port of orderPortsPreferredFirst(granted, preferredPort)) {
    const usable = await tryUsableSerialPort(port, options);
    if (usable) return usable;
  }
  return null;
}

// Escalates strictly: held port, then every grant, then one re-entry into programming
// mode if the caller supplied one. Injected so this stays MCU-agnostic.
export async function acquireUsableSerialPort({
  preferredPort = null,
  prompt = 'Select the serial port to continue.',
  onStatus,
  reenterProgrammingMode = null,
} = {}) {
  const options = { prompt, onStatus };

  if (preferredPort) {
    const held = await tryUsableSerialPort(preferredPort, options);
    if (held) return held;
  }

  const granted = await firstUsableGrantedPort(preferredPort, options);
  if (granted) return granted;

  if (reenterProgrammingMode) {
    onStatus?.('Re-entering programming mode…');
    await reenterProgrammingMode();

    const afterReentry = await firstUsableGrantedPort(preferredPort, options);
    if (afterReentry) return afterReentry;
  }

  throw new PortSelectionRequiredError(prompt);
}
