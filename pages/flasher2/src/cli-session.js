// MeshCore CLI over serial — §7.1 constants, §10.2 state 1, §10.7 / §11.6 provisioning.
//
// Universal: the CLI is the same firmware regardless of MCU family, so this serves
// both ESP32 and nRF52 (§5.4). Nothing here knows which chip it is talking to.

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
// that is the ladder's job (§5.3), and mixing the two hides which layer lost the device.
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

    // Anchor on the device's echo of this command before looking for an answer.
    // Residue that was queued in the stream *before* this session opened the port
    // is delivered a task later — after `runCommand`'s clear, which can only reach
    // what the drain loop has already pulled in — and would otherwise be paired
    // with this command. Measured: straight after a flash, a `ver` probe returned
    // the "Unknown command" a running node had emitted in reply to an earlier SYNC
    // probe, reporting a confident wrong version. Stream order guarantees residue
    // precedes the echo, so discarding through the echo discards all of it.
    //
    // Unverified on nRF52, which runs the same firmware and is assumed to echo the
    // same way. A device that does not echo times out instead of answering — the
    // conservative direction, and what §10.2 already treats as "did not answer".
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

  async function runCommand(command, { timeoutMs = CLI_COMMAND_TIMEOUT_MS } = {}) {
    if (pending) throw new Error('runCommand called while a command is still in flight');

    // Discard anything already buffered. A response cannot precede its command, so
    // whatever is sitting there is residue — device boot chatter, or the
    // "Unknown command" a running node emits after something else wrote to the
    // port (the §10.2 SYNC probe does exactly that). The echo anchor in
    // `deliverIfComplete` covers residue still in flight; this covers what has
    // already landed, and keeps a stale echo of the *same* command out of the way.
    buffer = '';

    if (!primed) {
      primed = true;
      // A bare terminator ends any partial line the *device* is holding. Measured:
      // the §10.2 SYNC probe leaves un-terminated SLIP bytes in a running node's
      // line buffer, and the next command is appended to them — `<junk>ver` parses
      // as "Unknown command", so the tool reports a confident wrong version for a
      // device that is perfectly healthy. Recovery is device-side and nothing the
      // host can read tells us it is needed, so it is always sent.
      //
      // Not waited on. Its reply arrives before this command's echo, and the echo
      // anchor discards everything up to that — which is what makes sending it
      // free rather than costing another round trip on a silent device.
      await writer.write(encoder.encode(CLI_LINE_TERMINATOR));
    }

    console.log(`[cli] > ${command}`);
    await writer.write(encoder.encode(`${command}${CLI_LINE_TERMINATOR}`));

    const answer = await new Promise((resolve, reject) => {
      const timer = setTimeout(() => {
        pending = null;
        // Drop whatever is buffered. A late answer left in place would be paired
        // with the *next* command, silently shifting every response in a
        // provisioning sequence by one. Losing a late answer is the cheaper error.
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

// §10.2 state 1. Affirmative only: silence is also what a device stuck in init looks like,
// so a null means "did not answer", never "is a bootloader".
//
// The default is a liveness probe — a fast no is the point. After a flash the device is
// still booting and needs `CLI_FIRST_COMMAND_TIMEOUT_MS` instead; measured, a freshly
// written node answers nothing at 1.5 s and answers normally once it has come up.
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
