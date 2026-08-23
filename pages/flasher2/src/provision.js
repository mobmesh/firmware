// §10.7 / §11.6 post-flash provisioning — settings pushed over the CLI once the device
// has rebooted into application firmware.
//
// MCU-agnostic by construction: the CLI is the same firmware on both families, so nothing
// here knows which chip it is talking to. What differs is *when* the port comes back, and
// that is the acquisition ladder's problem (§5.3), not this module's.

import {
  BOOT_ANNOUNCE_TIMEOUT_MS,
  CLI_BAUD_RATE,
  CLI_FIRST_COMMAND_TIMEOUT_MS,
  PORT_DROP_TIMEOUT_MS,
  POST_FLASH_RECONNECT_ATTEMPTS,
  POST_FLASH_RECONNECT_DELAY_MS,
} from './constants.js';
import { startCliSession } from './cli-session.js';
import { acquireUsableSerialPort, closeSerialPortQuietly } from './serial-port.js';

// The firmware answers a failed command in prose. There is no status code, so this is the
// only signal available; a false negative shows a warning, it never fails the flash.
const FAILED_ANSWER = /^(unknown command|err|error|invalid|usage:)/i;

function sleep(milliseconds) {
  return new Promise((resolve) => setTimeout(resolve, milliseconds));
}

/**
 * Block until the device actually leaves the bus, so the reconnect below lands on the
 * rebooted device and not on the pre-reset handle — which stays openable for a moment and
 * then never answers. Reading until the stream closes *is* the drop signal; the timeout is
 * only there so a device that never drops falls through to the reconnect anyway.
 */
async function waitForPortDrop(port, timeoutMs = PORT_DROP_TIMEOUT_MS) {
  try {
    if (!port.readable) await port.open({ baudRate: CLI_BAUD_RATE });
    const reader = port.readable.getReader();
    try {
      const drained = (async () => {
        for (;;) {
          const { done } = await reader.read();
          if (done) return;
        }
      })();
      await Promise.race([drained, sleep(timeoutMs)]);
    } finally {
      await reader.cancel().catch(() => {});
      reader.releaseLock();
    }
  } catch {
    // Never opened, or dropped mid-read. Either way the caller's reconnect is next.
  }
  await closeSerialPortQuietly(port);
}

// The announcement carries a role-dependent prefix — measured: `Repeater ID: <64 hex>` —
// so this matches the key anywhere on the line rather than anchoring at its start, and a
// room server's differing label costs nothing. Requiring 32+ hex characters and a closing
// newline keeps the boot chatter around it out (`SPIFFS: mount failed, -10025`,
// `nvs_open failed: NOT_FOUND`) and stops a half-received line from matching early.
const BOOT_KEY_LINE = /([0-9A-Fa-f]{32,})[ \t]*\r?\n/;

/**
 * Wait for the device to say it is up, without asking it anything.
 *
 * Passive on purpose: the shipped flasher notes that CLI traffic during post-flash keypair
 * generation is suspected of corrupting the key, which rules out polling. Returns the
 * announced key, or null on the timeout — a null is "carry on and let the first command's
 * long timeout decide", never a failure of its own.
 */
export async function waitForBootAnnouncement(port, { onStatus, timeoutMs = BOOT_ANNOUNCE_TIMEOUT_MS } = {}) {
  onStatus?.('Waiting for the device to finish starting up…');
  const decoder = new TextDecoderStream();
  const readableClosed = port.readable.pipeTo(decoder.writable);
  const reader = decoder.readable.getReader();

  let timer = null;
  const expired = new Promise((resolve) => {
    timer = setTimeout(() => resolve(null), timeoutMs);
  });

  let buffer = '';
  try {
    for (;;) {
      // One shared timeout promise, not a fresh one per iteration: re-racing would
      // abandon a pending read and drop whatever it was about to deliver.
      const next = await Promise.race([reader.read(), expired]);
      if (!next || next.done) return null;
      buffer += next.value;
      const found = BOOT_KEY_LINE.exec(buffer);
      if (found) return found[1].toUpperCase();
      // Keep only what could still be the start of a line.
      if (buffer.length > 4096) buffer = buffer.slice(-1024);
    }
  } catch {
    return null; // The port dropped. The reconnect above already happened; nothing to add.
  } finally {
    clearTimeout(timer);
    await reader.cancel().catch(() => {});
    await readableClosed.catch(() => {});
  }
}

function trimmed(value) {
  return typeof value === 'string' ? value.trim() : '';
}

// MeshCore takes plain decimal degrees. Five places is ~1 m — past the point where more
// digits mean anything for a fixed node, and short enough to read back in a log.
function degrees(value) {
  return Number.isFinite(value) ? Number(value.toFixed(5)) : null;
}

/**
 * The command set for this flow's state, in send order. Chosen by role, not by path
 * (§10.7) — a companion has no location to set and no admin password to accept.
 *
 * `heightFt` and `email` are deliberately absent: both exist for the GulfCoastMesh
 * registry record, and the firmware has no command for either.
 *
 * @returns {{ command: string, label: string, awaitReply?: boolean }[]}
 */
export function buildProvisionCommands(state) {
  const steps = [];
  const name = trimmed(state.nodeName);
  const lat = degrees(state.latitude);
  const lon = degrees(state.longitude);
  const password = trimmed(state.adminPassword);
  const privateKey = trimmed(state.identity?.privateKeyHex);

  if (name) steps.push({ command: `set name ${name}`, label: 'Setting the node name' });
  if (lat !== null) steps.push({ command: `set lat ${lat}`, label: 'Setting latitude' });
  if (lon !== null) steps.push({ command: `set lon ${lon}`, label: 'Setting longitude' });
  if (password) steps.push({ command: `password ${password}`, label: 'Setting the admin password' });

  // Last of the settings: it takes effect on the reboot below, and the identity the device
  // generated for itself on first boot stays in use until then.
  if (privateKey) {
    steps.push({ command: `set prv.key ${privateKey}`, label: 'Installing the node identity' });
  }

  // No reply is sent — see commands.json. Only worth the round trip if something changed.
  if (steps.length) {
    steps.push({ command: 'reboot', label: 'Restarting the device', awaitReply: false });
  }
  return steps;
}

/**
 * Get the port back after a write. The device re-enumerates on the way out of programming
 * mode, so the handle the flash used is stale; the ladder waits for the bus and returns
 * whatever came back, which may be a different port object.
 */
export async function acquireCliPort({ preferredPort = null, onStatus } = {}) {
  onStatus?.('Waiting for the device to restart…');
  if (preferredPort) await waitForPortDrop(preferredPort);

  // The drop is the signal, but the device does not come back instantly and the first
  // opens after it fail. Retry rather than escalating to the picker on the first miss.
  let lastError = null;
  for (let attempt = 1; attempt <= POST_FLASH_RECONNECT_ATTEMPTS; attempt += 1) {
    try {
      const port = await acquireUsableSerialPort({
        preferredPort,
        prompt: 'Select the device again to finish setting it up.',
      });
      await port.open({ baudRate: CLI_BAUD_RATE });
      return port;
    } catch (error) {
      lastError = error;
      await sleep(POST_FLASH_RECONNECT_DELAY_MS);
    }
  }
  throw lastError;
}

/**
 * Send the command set. The port must already be open at CLI baud.
 *
 * The first command carries `CLI_FIRST_COMMAND_TIMEOUT_MS` (§7.1): after a fresh erase the
 * device is still generating its identity keypair and answers nothing for up to ~30 s.
 *
 * @returns {{ command: string, answer: string|null, ok: boolean }[]}
 */
export async function sendProvisionCommands(port, commands, { onStatus } = {}) {
  const session = startCliSession(port);
  const results = [];
  try {
    for (const [index, step] of commands.entries()) {
      onStatus?.(step.label);
      const answer = await session.runCommand(step.command, {
        awaitReply: step.awaitReply !== false,
        ...(index === 0 ? { timeoutMs: CLI_FIRST_COMMAND_TIMEOUT_MS } : {}),
      });
      const ok = answer === null || !FAILED_ANSWER.test(answer);
      if (!ok) onStatus?.(`${step.command} → ${answer}`);
      results.push({ command: step.command, answer, ok });
    }
  } finally {
    await session.close();
  }
  return results;
}

/**
 * The whole post-flash pass: reacquire, probe, send. Returns null when the role has
 * nothing to configure, so the caller can say so rather than reporting a no-op as work.
 */
export async function provisionDevice(state, { preferredPort = null, onStatus } = {}) {
  const commands = buildProvisionCommands(state);
  if (!commands.length) return null;

  const port = await acquireCliPort({ preferredPort, onStatus });
  try {
    const announced = await waitForBootAnnouncement(port, { onStatus });
    if (announced) onStatus?.(`Device is up — node ${announced.slice(0, 4)}`);
    return { port, announced, results: await sendProvisionCommands(port, commands, { onStatus }) };
  } catch (error) {
    await closeSerialPortQuietly(port);
    throw error;
  }
}
