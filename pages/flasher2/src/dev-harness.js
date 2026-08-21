// Local development harness. Not the wizard shell — it exists so the common
// core (§5.1, §5.3) can be exercised against real hardware before any engine is
// written. Replaced by the real UI; nothing else should import it.

import { compatibilityCopy, detectBrowserKind, detectSerialSupport } from './capability.js';
import { CLI_BAUD_RATE } from './constants.js';
import { probeCliVersion } from './cli-session.js';
import { resolveEsp32Mode } from './esp32.js';
import { PortSelectionRequiredError } from './serial-port.js';
import {
  acquireUsableSerialPort,
  listGrantedSerialPorts,
  promptForSerialPort,
} from './serial-port.js';

const logElement = document.querySelector('#log');
const statusElement = document.querySelector('#status');

function log(message) {
  const line = document.createElement('div');
  line.textContent = `${new Date().toLocaleTimeString()}  ${message}`;
  logElement.prepend(line);
}

function describePort(port) {
  const info = port.getInfo();
  const vid = info.usbVendorId?.toString(16).padStart(4, '0') ?? '????';
  const pid = info.usbProductId?.toString(16).padStart(4, '0') ?? '????';
  return `${vid}:${pid}`;
}

let heldPort = null;

function reportCapability() {
  const support = detectSerialSupport();
  const browserKind = detectBrowserKind();
  statusElement.textContent = `serial: ${support} · browser: ${browserKind}`;

  const copy = compatibilityCopy(support, browserKind);
  if (!copy) return true;

  log(`${copy.title} — ${copy.body} ${copy.suggestion}`);
  return false;
}

document.querySelector('#list').addEventListener('click', async () => {
  const granted = await listGrantedSerialPorts();
  log(`granted ports: ${granted.length ? granted.map(describePort).join(', ') : 'none'}`);
});

document.querySelector('#pick').addEventListener('click', async () => {
  try {
    heldPort = await promptForSerialPort();
    log(`picked ${describePort(heldPort)} — held for subsequent steps`);
  } catch (error) {
    log(`picker dismissed: ${error.name}`);
  }
});

document.querySelector('#acquire').addEventListener('click', async () => {
  try {
    heldPort = await acquireUsableSerialPort({
      preferredPort: heldPort,
      prompt: 'Select the board’s serial port to continue.',
      onStatus: log,
    });
    log(`acquired ${describePort(heldPort)}`);
  } catch (error) {
    if (error instanceof PortSelectionRequiredError) {
      log(`selection required: ${error.prompt}`);
      return;
    }
    log(`failed: ${error.name} — ${error.message}`);
  }
});

document.querySelector('#cli').addEventListener('click', async () => {
  try {
    heldPort = await acquireUsableSerialPort({ preferredPort: heldPort, onStatus: log });
    await heldPort.open({ baudRate: CLI_BAUD_RATE });
    try {
      const version = await probeCliVersion(heldPort);
      log(version ? `CLI answered: ${version}` : 'CLI silent — app mode not confirmed (§10.2 state 2 or 3)');
    } finally {
      await heldPort.close().catch(() => {});
    }
  } catch (error) {
    log(`CLI probe failed: ${error.name} — ${error.message}`);
  }
});

document.querySelector('#mode').addEventListener('click', async () => {
  try {
    heldPort = await acquireUsableSerialPort({ preferredPort: heldPort, onStatus: log });
    const { mode, version } = await resolveEsp32Mode(heldPort);
    log(`mode: ${mode}${version ? ` — ${version}` : ''}`);
  } catch (error) {
    log(`mode probe failed: ${error.name} — ${error.message}`);
  }
});

reportCapability();
