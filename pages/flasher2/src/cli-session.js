// MeshCore CLI over serial — §7.1 constants, §10.2 state 1, §10.7 / §11.6 provisioning.
//
// Universal: the CLI is the same firmware regardless of MCU family, so this serves
// both ESP32 and nRF52 (§5.4). Nothing here knows which chip it is talking to.

import {
  CLI_COMMAND_TIMEOUT_MS,
  CLI_INTER_COMMAND_DELAY_MS,
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

/**
 * Start a CLI session on an already-open port. One command in flight at a time.
 *
 * The caller owns the port: opening it, and closing it after `close()` releases
 * the stream locks. Sessions do not reopen ports — that is the acquisition
 * ladder's job (§5.3), and mixing the two hides which layer lost the device.
 */
export function startCliSession(port) {
  const decoderStream = new TextDecoderStream();
  const readableClosed = port.readable.pipeTo(decoderStream.writable);
  const reader = decoderStream.readable.getReader();
  const writer = port.writable.getWriter();

  let buffer = '';
  let pending = null; // { resolve, reject, timer }

  function deliverIfComplete() {
    if (!pending) return;
    const markerAt = buffer.indexOf(CLI_RESPONSE_MARKER);
    if (markerAt === -1) return;
    const lineEnd = buffer.indexOf('\r\n', markerAt);
    if (lineEnd === -1) return;

    const answer = buffer.slice(markerAt + CLI_RESPONSE_MARKER.length, lineEnd).trim();
    buffer = buffer.slice(lineEnd + 2);
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

  async function runCommand(command, { timeoutMs = CLI_COMMAND_TIMEOUT_MS } = {}) {
    if (pending) throw new Error('runCommand called while a command is still in flight');

    // Discard anything already buffered. A response cannot precede its command, so
    // whatever is sitting there is residue — device boot chatter, or the
    // "Unknown command" a running node emits after something else wrote to the
    // port (the §10.2 SYNC probe does exactly that). Pairing residue with this
    // command would report a confident wrong answer.
    buffer = '';

    await writer.write(new TextEncoder().encode(`${command}\r`));

    const answer = await new Promise((resolve, reject) => {
      const timer = setTimeout(() => {
        pending = null;
        // Drop whatever is buffered. A late answer left in place would be paired
        // with the *next* command, silently shifting every response in a
        // provisioning sequence by one. Losing a late answer is the cheaper error.
        buffer = '';
        reject(new CliTimeoutError(command, timeoutMs));
      }, timeoutMs);
      pending = { resolve, reject, timer };
      // The answer may already be sitting in the buffer from before this call.
      deliverIfComplete();
    });

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

/**
 * §10.2 state 1: does a running application answer?
 *
 * An affirmative signal only — a silent device is *not* reported as a bootloader
 * here, because silence is also what a device stuck in init looks like (§10.2
 * state 3). Returns the version string, or null for "did not answer".
 */
export async function probeCliVersion(port) {
  const session = startCliSession(port);
  try {
    return await session.runCommand('ver', { timeoutMs: CLI_PROBE_TIMEOUT_MS });
  } catch (error) {
    if (error instanceof CliTimeoutError || error instanceof CliConnectionLostError) return null;
    throw error;
  } finally {
    await session.close();
  }
}
