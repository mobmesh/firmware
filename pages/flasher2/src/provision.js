// Post-flash provisioning: settings pushed over the CLI once the device has rebooted.
// MCU-agnostic — when the port comes back is the acquisition ladder's problem, not this.

import {
  BOOT_ANNOUNCE_TIMEOUT_MS,
  CLI_BAUD_RATE,
  CLI_FIRST_COMMAND_TIMEOUT_MS,
  PORT_DROP_TIMEOUT_MS,
  POST_FLASH_RECONNECT_ATTEMPTS,
  POST_FLASH_RECONNECT_DELAY_MS,
} from './constants.js';
import { parseRadio, startCliSession } from './cli-session.js';
import { acquireUsableSerialPort, closeSerialPortQuietly } from './serial-port.js';

// The firmware answers a failed command in prose. There is no status code, so this is the
// only signal available; a false negative shows a warning, it never fails the flash.
const FAILED_ANSWER = /^(unknown command|err|error|invalid|usage:)/i;

function sleep(milliseconds) {
  return new Promise((resolve) => setTimeout(resolve, milliseconds));
}

/**
 * Block until the device leaves the bus, so the reconnect lands on the rebooted device
 * and not the pre-reset handle, which stays openable for a moment and then never answers.
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

// The line carries a role-dependent prefix (`Repeater ID: <64 hex>`), so match the key
// anywhere on it. 32+ hex plus a closing newline keeps boot chatter and half-lines out.
const BOOT_KEY_LINE = /([0-9A-Fa-f]{32,})[ \t]*\r?\n/;

/**
 * Wait for the device to say it is up, without asking it anything: CLI traffic during
 * keypair generation is suspected of corrupting the key. Null means carry on regardless.
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

// The device stores positions as float32, so what it reads back is never textually what
// was written — measured on a P1: `set lon -89.01235` reads back `-89.0123519`. Comparing
// at float32 precision is exact, where any decimal tolerance would be a guess.
function sameDegrees(a, b) {
  if (a === null || b === null) return false;
  return Math.fround(a) === Math.fround(b);
}

// The only three the firmware answers "reboot to apply" to — CommonCLI.cpp, lines 518, 602
// and 716. Everything else savePrefs() and takes effect at once, so a list without one of
// these needs no restart. `set freq` is here for completeness; our zone files use `radio`.
const NEEDS_REBOOT = /^set (radio|freq|prv\.key) /;

// `set radio <freq>,<bw>,<sf>,<cr>` against what `get radio` reported. Frequency and
// bandwidth take the float32 treatment for the same reason positions do.
function radioCommandMatches(command, current) {
  if (!current) return false;
  const parsed = parseRadio(command.slice('set radio '.length));
  if (!parsed) return false;
  return (
    Math.fround(parsed.freq) === Math.fround(current.freq) &&
    Math.fround(parsed.bw) === Math.fround(current.bw) &&
    parsed.sf === current.sf &&
    parsed.cr === current.cr
  );
}

/**
 * The command set for this flow's state, in send order, chosen by role. `heightFt` and
 * `email` are absent on purpose: registry fields, with no firmware command for either.
 *
 * @returns {{ command: string, label: string, awaitReply?: boolean }[]}
 */
export function buildProvisionCommands(state) {
  const steps = [];

  // What the device already had, read at arm. Absent on a New install and on any device
  // that answered nothing, in which case everything below counts as changed.
  const existing = state.existingConfig ?? null;

  // Every path, every role: a device that has been unpowered comes up with a bogus clock,
  // and this is the one moment we are certainly talking to it. Forward-only in the
  // firmware — it answers "cannot go backwards" rather than rewinding a good clock — so
  // it is safe to send unconditionally. Also the post-flash liveness check, being first.
  steps.push({
    command: `time ${Math.floor(Date.now() / 1000)}`,
    label: 'Setting the clock',
    // A clock already ahead of ours is refused, which is the firmware working, not a fault.
    tolerateFailure: true,
  });

  // Board defaults first, so a location setting overrides one. From the manifest variant,
  // so only the custom path carries any.
  for (const command of state.plan?.postFlash?.commands ?? []) {
    steps.push({ command, label: 'Applying board defaults' });
  }

  // Then the regional set for the node's zone. Radio and interval settings, so nothing
  // here collides with name or position. Re-asserted on every path so a drifted node is
  // corrected — except `set radio`, which is dropped when the device already reports
  // those exact parameters, because sending it is what forces the reboot below.
  for (const command of state.zoneCommands ?? []) {
    if (command.startsWith('set radio ') && radioCommandMatches(command, existing?.radio)) {
      continue;
    }
    steps.push({ command, label: 'Applying regional settings' });
  }

  const name = trimmed(state.nodeName);
  const lat = degrees(state.latitude);
  const lon = degrees(state.longitude);
  const password = trimmed(state.adminPassword);
  const privateKey = trimmed(state.identity?.privateKeyHex);

  if (name && name !== existing?.name) {
    steps.push({ command: `set name ${name}`, label: 'Setting the node name' });
  }
  if (lat !== null && !sameDegrees(lat, existing?.latitude ?? null)) {
    steps.push({ command: `set lat ${lat}`, label: 'Setting latitude' });
  }
  if (lon !== null && !sameDegrees(lon, existing?.longitude ?? null)) {
    steps.push({ command: `set lon ${lon}`, label: 'Setting longitude' });
  }
  // No `get` for this one, so an unchanged password cannot be detected here — the UI
  // sends an empty string when its masked placeholder was left alone.
  if (password) steps.push({ command: `password ${password}`, label: 'Setting the admin password' });

  // Last of the settings: it takes effect on the reboot below, and the identity the device
  // generated for itself on first boot stays in use until then.
  if (privateKey) {
    steps.push({ command: `set prv.key ${privateKey}`, label: 'Installing the node identity' });
  }

  // No reply is sent — see commands.json. Keyed on what the firmware actually says needs
  // one: everything else saves and applies at once, so restarting a working node for a
  // name change or an interval is a cost with nothing bought.
  if (steps.some((step) => NEEDS_REBOOT.test(step.command))) {
    steps.push({ command: 'reboot', label: 'Restarting the device', awaitReply: false });
  }
  return steps;
}

/**
 * Get the port back after a write. The device re-enumerates on the way out, so the ladder
 * may return a different port object than the flash used.
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
 * Send the command set; the port must already be open at CLI baud. The first command
 * carries the longer timeout, since a freshly erased device is still generating its key.
 *
 * @returns {{ command: string, answer: string|null, ok: boolean }[]}
 */
export async function sendProvisionCommands(port, commands, { onStatus, onProgress } = {}) {
  const session = startCliSession(port);
  const results = [];
  try {
    for (const [index, step] of commands.entries()) {
      onStatus?.(`${step.label} (${index + 1} of ${commands.length})…`);
      const answer = await session.runCommand(step.command, {
        awaitReply: step.awaitReply !== false,
        ...(index === 0 ? { timeoutMs: CLI_FIRST_COMMAND_TIMEOUT_MS } : {}),
      });
      const ok = answer === null || step.tolerateFailure === true || !FAILED_ANSWER.test(answer);
      if (!ok) onStatus?.(`${step.command} → ${answer}`);
      results.push({ command: step.command, answer, ok });
      onProgress?.((index + 1) / commands.length);
    }
  } finally {
    await session.close();
  }
  return results;
}

/**
 * The whole pass: reacquire, probe, send. Null when the role has nothing to configure,
 * so the caller can say so rather than report a no-op as work.
 */
export async function provisionDevice(state, { preferredPort = null, onStatus, onProgress } = {}) {
  const commands = buildProvisionCommands(state);
  if (!commands.length) return null;

  const port = await acquireCliPort({ preferredPort, onStatus });
  try {
    const announced = await waitForBootAnnouncement(port, { onStatus });
    if (announced) onStatus?.(`Device is up — node ${announced.slice(0, 4)}`);
    return {
      port,
      announced,
      results: await sendProvisionCommands(port, commands, { onStatus, onProgress }),
    };
  } catch (error) {
    await closeSerialPortQuietly(port);
    throw error;
  }
}
