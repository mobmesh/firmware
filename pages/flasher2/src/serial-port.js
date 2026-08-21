// Port module — rewrite_code.md §5.3. One implementation, three consumers (C4).
//
// The ladder is layered rather than branched: each function adds exactly one
// fallback over the one below it, so the escalation order reads as a stack.
// Ordering here is load-bearing (C0) — close-before-open, then
// connect-event-then-settle-then-probe.

import {
  PORT_CONNECT_TIMEOUT_MS,
  PORT_OPEN_PROBE_ATTEMPTS,
  PORT_OPEN_RETRY_DELAY_MS,
  PORT_PROBE_BAUD_RATE,
  PORT_RESCAN_INTERVAL_MS,
  PORT_SETTLE_AFTER_CONNECT_MS,
} from './constants.js';
// Typed errors. A failure the user must act on carries its payload as data —
// never as message text for a caller to pattern-match (§5.3, §12.3). Errors
// live with the module whose contract they are part of.

/**
 * The only failure that escalates to a user gesture. Carries the prompt the UI
 * shows next to the port picker, so the caller never composes one.
 */
export class PortSelectionRequiredError extends Error {
  constructor(prompt) {
    super(prompt);
    this.name = 'PortSelectionRequiredError';
    this.prompt = prompt;
  }
}

/**
 * Raised at the boundary of every `port.open()` call. Chrome reports an
 * unavailable port as a DOMException whose message wording is neither stable
 * API nor locale-independent, so the retry ladder classifies on this type
 * instead of on text (§12.3).
 */
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
 * Ports granted to this origin. Repeat visitors resolve from here rather than
 * through the picker (§5.2).
 */
export async function listGrantedSerialPorts() {
  return serialApi().getPorts();
}

/**
 * The picker. Requires a user gesture, so it is never called from inside the
 * acquisition ladder — the ladder raises PortSelectionRequiredError and the UI
 * calls this from the resulting click.
 */
export async function promptForSerialPort() {
  return serialApi().requestPort();
}

/**
 * Port identity comparison. `getPorts()` returns the same object instances
 * across calls in every browser observed, but the spec does not guarantee it;
 * matching on getInfo() instead would falsely match two identical boards, which
 * is worse. Assumption documented rather than defended.
 */
function isSamePort(a, b) {
  return a === b;
}

// Deviation from §5.3's donor reading: raise rather than infer. See handoff.md.
// Chrome has exposed SerialPort.connected since 117 and reports it correctly for a
// closed-but-present port. There is no safe fallback: readable/writable are both
// null whenever the port is closed, so guessing from them reads a present device
// as absent and sends the caller into a connect wait that can never resolve.
// Fail fast instead (§12.3, "fail fast when the capability is missing").
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

/** Every open in the tool goes through here, so failures arrive typed (§5.3). */
async function openSerialPort(port, options) {
  try {
    await port.open(options);
  } catch (error) {
    throw new PortOpenFailedError(error);
  }
}

/**
 * Readiness probe. Deliberately side-effecting: an open/close cycle is the only
 * reliable readiness signal Web Serial exposes, so the port is opened purely to
 * learn whether it can be. Harmless on the target boards — the port is closed
 * again before anything writes to it.
 */
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

function sameUsbIdentity(a, b) {
  return a.usbVendorId === b.usbVendorId && a.usbProductId === b.usbProductId;
}

/**
 * Resolve the live port for a device, returning the port to continue with.
 *
 * **Deliberate deviation from §5.3 step 2 as written.** The spec says "wait for a
 * `connect` event"; do not "restore" that without re-measuring — waiting on the
 * handed-in port object costs the full CONNECT_TIMEOUT (measured 8506 ms vs 505 ms)
 * because a re-enumerated device arrives as a different object. See handoff.md,
 * "Deviations from the spec".
 *
 * Races two signals, because each covers the other's blind spot:
 *
 *   - a `connect` listener on `navigator.serial` wakes the moment the device
 *     returns, but is deaf to a reset that finished before we started listening —
 *     which is the common case, since the tool triggers the reset itself
 *   - a poll of the granted list sees a device that is already back, but only at
 *     the next interval
 *
 * The listener is registered *before* the first scan, so no arrival can slip
 * through the gap between the two. The poll then bounds the wait regardless of
 * whether any event ever fires.
 *
 * A re-enumerated device arrives as a *new* SerialPort: the held object is dropped
 * from `getPorts()`, reports `connected: false`, and raises NetworkError on open,
 * so neither signal can be replaced by "wait on the port we were handed".
 *
 * Matching is by USB vendor/product, all `getInfo()` exposes. Two identical boards
 * attached at once are indistinguishable here.
 */
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
 * Capability: wait for a port to become usable (§5.3).
 * Returns the port once it opens cleanly, or raises selection-required.
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

// The preferred port is re-tried here even when the caller already tried it:
// a reset can move the device between the two attempts, and the granted list is
// re-read each time. Ordering is per §5.3 — do not skip the repeat.
async function firstUsableGrantedPort(preferredPort, options) {
  const granted = await listGrantedSerialPorts();
  for (const port of orderPortsPreferredFirst(granted, preferredPort)) {
    const usable = await tryUsableSerialPort(port, options);
    if (usable) return usable;
  }
  return null;
}

/**
 * Capability: acquire a ready port with fallbacks (§5.3).
 *
 * Escalates strictly: held port, then every granted port with the held one
 * first, then — only when the caller supplies a re-entry gesture — one attempt
 * at putting the device back into its programming mode and running the first
 * two rungs again. That rung is last because re-touching a healthy device costs
 * a full re-enumeration cycle.
 *
 * `reenterProgrammingMode` is injected rather than imported so this module stays
 * MCU-agnostic; only the nRF52 path supplies one (§11.3).
 */
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
