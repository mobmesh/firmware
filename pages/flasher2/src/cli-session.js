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
