// MeshCore CLI over serial: the liveness probe and post-flash provisioning. The same
// firmware on both families, so nothing here knows which chip it is talking to.

import {
  CLI_COMMAND_TIMEOUT_MS,
  CLI_INTER_COMMAND_DELAY_MS,
  CLI_LINE_ENDING,
  CLI_LINE_TERMINATOR,
  CLI_PROBE_TIMEOUT_MS,
  CLI_RESPONSE_MARKER,
} from './constants.js';

function sleep(milliseconds) {
  return new Promise((resolve) => setTimeout(resolve, milliseconds));
}

/** Raised when a command gets no answer in time. Carries the command for the UI. */
export class CliTimeoutError extends Error {
  constructor(command, timeoutMs) {
    super(`No answer to "${command}" within ${timeoutMs} ms.`);
    this.name = 'CliTimeoutError';
    this.command = command;
  }
}

/** Raised when the port drops mid-session. Distinct from a timeout: retrying is futile. */
export class CliConnectionLostError extends Error {
  constructor() {
    super('The serial connection was lost.');
    this.name = 'CliConnectionLostError';
  }
}

// One command in flight at a time. The caller owns the port: sessions never reopen one —
// that is the ladder's job, and mixing the two hides which layer lost the device.
export function startCliSession(port) {
  const decoderStream = new TextDecoderStream();
  const readableClosed = port.readable.pipeTo(decoderStream.writable);
  const reader = decoderStream.readable.getReader();
  const writer = port.writable.getWriter();

  const encoder = new TextEncoder();
  let buffer = '';
  let primed = false;
  let pending = null; // { command, resolve, reject, timer, echoSeen }

  function deliverIfComplete() {
    if (!pending) return;

    // Anchor on the echo: residue always precedes it, so discarding through it discards
    // all of it. Measured — without this a `ver` probe reports a confident wrong version.
    if (!pending.echoSeen) {
      const echo = `${pending.command}${CLI_LINE_ENDING}`;
      const echoAt = buffer.indexOf(echo);
      if (echoAt === -1) return;
      buffer = buffer.slice(echoAt + echo.length);
      pending.echoSeen = true;
    }

    const markerAt = buffer.indexOf(CLI_RESPONSE_MARKER);
    if (markerAt === -1) return;
    const lineEnd = buffer.indexOf(CLI_LINE_ENDING, markerAt);
    if (lineEnd === -1) return;

    const answer = buffer.slice(markerAt + CLI_RESPONSE_MARKER.length, lineEnd).trim();
    buffer = buffer.slice(lineEnd + CLI_LINE_ENDING.length);
    const { resolve, timer } = pending;
    clearTimeout(timer);
    pending = null;
    resolve(answer);
  }

  const draining = (async () => {
    try {
      for (;;) {
        const { value, done } = await reader.read();
        if (done) break;
        buffer += value;
        deliverIfComplete();
      }
    } catch {
      // The port dropped. Nothing to do here — the pending command is rejected
      // in the finally block, which is the only place that can act on it.
    } finally {
      if (pending) {
        clearTimeout(pending.timer);
        const { reject } = pending;
        pending = null;
        reject(new CliConnectionLostError());
      }
    }
  })();

  // `awaitReply: false` is for commands the firmware answers nothing to (`reboot`,
  // `poweroff`); waiting reports a failure for a command that worked.
  async function runCommand(command, { timeoutMs = CLI_COMMAND_TIMEOUT_MS, awaitReply = true } = {}) {
    if (pending) throw new Error('runCommand called while a command is still in flight');

    // A response cannot precede its command, so anything buffered is residue. The echo
    // anchor covers what is still in flight; this covers what has already landed.
    buffer = '';

    if (!primed) {
      primed = true;
      // Ends any partial line the device is holding: a SYNC probe leaves un-terminated
      // bytes that the next command appends to. Always sent, never waited on.
      await writer.write(encoder.encode(CLI_LINE_TERMINATOR));
    }

    console.log(`[cli] > ${command}`);
    await writer.write(encoder.encode(`${command}${CLI_LINE_TERMINATOR}`));

    if (!awaitReply) {
      if (CLI_INTER_COMMAND_DELAY_MS) await sleep(CLI_INTER_COMMAND_DELAY_MS);
      return null;
    }

    const answer = await new Promise((resolve, reject) => {
      const timer = setTimeout(() => {
        pending = null;
        // A late answer left in place pairs with the *next* command and shifts every
        // response by one. Losing it is the cheaper error.
        buffer = '';
        reject(new CliTimeoutError(command, timeoutMs));
      }, timeoutMs);
      pending = { command, resolve, reject, timer, echoSeen: false };
      // The answer may already be sitting in the buffer from before this call.
      deliverIfComplete();
    });

    console.log(`[cli] < ${answer}`);
    if (CLI_INTER_COMMAND_DELAY_MS) await sleep(CLI_INTER_COMMAND_DELAY_MS);
    return answer;
  }

  async function close() {
    writer.releaseLock();
    await reader.cancel().catch(() => {});
    await readableClosed.catch(() => {});
    await draining;
  }

  return { runCommand, close };
}

// Affirmative only: a null means "did not answer", never "is a bootloader". The default
// is a liveness probe; after a flash the caller must pass the longer first-command timeout.
export async function probeCliVersion(port, { timeoutMs = CLI_PROBE_TIMEOUT_MS } = {}) {
  const session = startCliSession(port);
  try {
    return await session.runCommand('ver', { timeoutMs });
  } catch (error) {
    if (error instanceof CliTimeoutError || error instanceof CliConnectionLostError) return null;
    throw error;
  } finally {
    await session.close();
  }
}

// `get` replies carry a second marker the session's own anchor does not strip: measured
// on a P1, `get name` answers "> RigBench" while `ver` answers unprefixed.
function stripGetMarker(answer) {
  return answer?.startsWith('> ') ? answer.slice(2) : answer;
}

/**
 * The device's current name, position and admin-password presence, for pre-filling the
 * Upgrade path rather than overwriting what is already on the node. Read while the
 * application is still running — no transport serves this once the engine is entered.
 *
 * Every field is independently nullable: a device that answers nothing (the T1, a
 * factory-fresh board, Meshtastic) must degrade to blank fields, never fail the flow.
 */
export async function readNodeConfig(port, { timeoutMs = CLI_COMMAND_TIMEOUT_MS } = {}) {
  const session = startCliSession(port);
  const read = async (command, commandTimeoutMs = timeoutMs) => {
    try {
      return stripGetMarker(await session.runCommand(command, { timeoutMs: commandTimeoutMs }));
    } catch (error) {
      if (error instanceof CliTimeoutError || error instanceof CliConnectionLostError) return null;
      throw error;
    }
  };
  try {
    // `ver` first, at the probe timeout, as the liveness gate. Silence here means no CLI
    // at all (a T1, a bootloader, a factory-fresh board) and the reads below would each
    // burn the full command timeout proving the same thing — 15 s to learn nothing.
    const version = await read('ver', CLI_PROBE_TIMEOUT_MS);
    if (version === null) return { version: null, name: null, latitude: null, longitude: null };

    const name = await read('get name');
    // Degrees, not the ×1000 scale the radio fields use — measured on a P1. Stored as
    // float32 on the device, so a value read back is not textually what was written.
    const latitude = numberOrNull(await read('get lat'));
    const longitude = numberOrNull(await read('get lon'));
    // One call returns all four fields `set radio` writes, and `set radio` is one of only
    // three commands the firmware says needs a reboot — so this read is what decides
    // whether an upgrade has to restart a working node.
    const radio = parseRadio(await read('get radio'));
    return { version, name, latitude, longitude, radio };
  } finally {
    await session.close();
  }
}

function numberOrNull(answer) {
  if (answer === null) return null;
  const value = Number.parseFloat(answer);
  return Number.isFinite(value) ? value : null;
}

/**
 * `freq,bw,sf,cr` as the device reports it — measured on a P1: `869.6179809,62.5,8,5`.
 * Comma-separated, and the same four fields `set radio` takes in the same order.
 */
export function parseRadio(answer) {
  if (!answer) return null;
  const parts = answer.split(',').map((part) => Number.parseFloat(part.trim()));
  if (parts.length !== 4 || parts.some((value) => !Number.isFinite(value))) return null;
  const [freq, bw, sf, cr] = parts;
  return { freq, bw, sf, cr };
}

// nRF52-only per the firmware docs; a null means "cannot tell", not "no bootloader" —
// the OTAFIX gate must offer the update rather than act on silence.
export async function probeBootloaderVersion(port, { timeoutMs = CLI_PROBE_TIMEOUT_MS } = {}) {
  const session = startCliSession(port);
  try {
    return stripGetMarker(await session.runCommand('get bootloader.ver', { timeoutMs }));
  } catch (error) {
    if (error instanceof CliTimeoutError || error instanceof CliConnectionLostError) return null;
    throw error;
  } finally {
    await session.close();
  }
}
