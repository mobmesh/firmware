// Deliberately ugly renderer for flow.js. Plain buttons and a text log: the point is to
// read the sequence of decisions, not to look at anything. Throw this away when the real
// UI is built — flow.js is the part that stays.

import * as flowApi from './flow.js';
import {
  PortSelectionRequiredError,
  closeSerialPortQuietly,
  promptForSerialPort,
} from './serial-port.js';
import { closeEsptoolSession } from './esptool.js';

const elements = {
  steps: document.querySelector('#steps'),
  panel: document.querySelector('#panel'),
  state: document.querySelector('#state'),
  log: document.querySelector('#log'),
  serial: document.querySelector('#serial-log'),
  serialPanel: document.querySelector('#serial-panel'),
};

// --- serial log ---------------------------------------------------------------------------
// Everything, in arrival order, for the whole session: status lines, esptool's own wire
// chatter, CLI traffic, and any warning a module raises. Capped so a long flash cannot grow
// the DOM without bound.
const SERIAL_LOG_MAX_LINES = 4000;

function serialLine(tag, text) {
  if (!elements.serial) return;
  const atBottom =
    elements.serial.scrollHeight - elements.serial.scrollTop - elements.serial.clientHeight < 40;
  const row = document.createElement('div');
  row.className = `s-${tag}`;
  row.textContent = `${new Date().toLocaleTimeString('en-GB', { hour12: false })}.${String(
    Date.now() % 1000
  ).padStart(3, '0')}  ${text}`;
  elements.serial.append(row);
  while (elements.serial.childElementCount > SERIAL_LOG_MAX_LINES) {
    elements.serial.firstElementChild.remove();
  }
  if (atBottom) elements.serial.scrollTop = elements.serial.scrollHeight;
}

function describeArg(value) {
  if (typeof value === 'string') return value;
  if (value instanceof Error) return `${value.name}: ${value.message}`;
  try {
    return JSON.stringify(value);
  } catch {
    return String(value);
  }
}

// Patched at module load, before any engine is imported, so nothing a lazily loaded bundle
// prints can arrive before the capture is in place.
for (const level of ['log', 'info', 'warn', 'error']) {
  const original = console[level].bind(console);
  console[level] = (...args) => {
    original(...args);
    serialLine(level, args.map(describeArg).join(' '));
  };
}

window.addEventListener('unhandledrejection', (event) =>
  serialLine('error', `unhandled rejection: ${describeArg(event.reason)}`)
);

let flow = null;

function log(message) {
  const line = document.createElement('div');
  line.textContent = `${new Date().toLocaleTimeString()}  ${message}`;
  elements.log.prepend(line);
  serialLine('status', message);
}

function clear(node) {
  node.replaceChildren();
}

function button(label, onClick, { image = null } = {}) {
  const element = document.createElement('button');
  if (image) {
    const picture = document.createElement('img');
    picture.src = image;
    picture.alt = '';
    picture.loading = 'lazy';
    // Upstream's SPA host answers 200 with index.html for a missing file, so a failed
    // decode is the only signal there is no artwork; drop it, don't show a broken glyph.
    picture.addEventListener('error', () => picture.remove());
    element.append(picture);
  }
  const text = document.createElement('span');
  text.textContent = label;
  element.append(text);
  element.addEventListener('click', onClick);
  return element;
}

function line(text, className) {
  const element = document.createElement('div');
  element.textContent = text;
  if (className) element.className = className;
  return element;
}

// --- readouts ---------------------------------------------------------------------------

function renderStepList() {
  clear(elements.steps);
  const steps = flowApi.applicableSteps(flow);
  steps.forEach((step, index) => {
    const done = index < flow.stepIndex;
    const marker = index === flow.stepIndex ? '▶' : done ? '✓' : '·';
    elements.steps.append(
      line(`${marker} ${index + 1}. ${step.title}`, index === flow.stepIndex ? 'now' : done ? 'done' : 'past')
    );
  });
}

function renderState() {
  const { port, session, customManifest, stockManifest, evidence, file, plan, ...rest } = flow.state;
  const shown = { ...rest, hasPort: Boolean(port), hasSession: Boolean(session), file: file?.name ?? null };
  elements.state.textContent = JSON.stringify(shown, null, 1);
}

function redraw() {
  renderStepList();
  renderState();
}

// --- step rendering ---------------------------------------------------------------------

function next() {
  flowApi.advance(flow);
  renderStep();
}

function withBack(node) {
  if (flowApi.canGoBack(flow)) node.append(button('← Back', () => { flowApi.goBack(flow); renderStep(); }));
  return node;
}

function describePort(port) {
  const { usbVendorId: vid, usbProductId: pid } = port.getInfo();
  const hex = (n) => (n ?? 0).toString(16).padStart(4, '0');
  return `${hex(vid)}:${hex(pid)}`;
}

// §11.4. The port module raises rather than prompting; only a click can call requestPort(),
// so the escalation has to surface here as a button.
function checkpoint(error, retry) {
  clear(elements.panel);
  elements.panel.append(line(error.prompt, 'title'));
  elements.panel.append(button('Select device…', async () => {
    try {
      flow.state.port = await promptForSerialPort();
      log(`picked ${describePort(flow.state.port)}`);
    } catch (dismissed) {
      log(`picker dismissed: ${dismissed.name}`);
      return;
    }
    retry();
  }));
  elements.panel.append(button('Start over', () => start({ dryRun: flow.dryRun })));
  redraw();
}

function fail(error, retry) {
  if (error instanceof PortSelectionRequiredError && retry) return checkpoint(error, retry);
  log(`${error.name}: ${error.message}`);
  clear(elements.panel);
  elements.panel.append(line(`Stopped: ${error.message}`, 'err'));
  if (retry) elements.panel.append(button('Retry', retry));
  elements.panel.append(button('Start over', () => start({ dryRun: flow.dryRun })));
  redraw();
}

const progress = { onStatus: log, onProgress: (f) => { if (f === 1) log('…100%'); } };

async function renderStep() {
  const step = flowApi.currentStep(flow);
  redraw();
  clear(elements.panel);
  if (!step) return;

  elements.panel.append(line(step.title, 'title'));

  if (step.kind === 'action') {
    // Runs on entry. Nothing here needs a gesture — the picker is reached through §11.4's
    // checkpoint, not from run() — so the flow stops only at decisions and failures.
    elements.panel.append(line('Running…'));
    try {
      await step.run(flow, progress);
      next();
    } catch (error) {
      fail(error, () => renderStep());
    }
    return;
  }

  if (step.kind === 'choice') {
    if (step.unspecified) elements.panel.append(line('This step has no specified content yet.', 'err'));
    let options;
    try {
      options = await step.options(flow);
    } catch (error) {
      fail(error, () => renderStep());
      return;
    }
    if (options.length === 0) {
      elements.panel.append(line('No options — the flow dead-ends here.', 'err'));
    }
    for (const option of options) {
      const label = option.note ? `${option.label}  (${option.note})` : option.label;
      elements.panel.append(button(label, async () => {
        await step.apply(flow, option.value);
        log(`${step.id} = ${JSON.stringify(option.value)}`);
        next();
      }, { image: option.image ?? null }));
    }
    withBack(elements.panel);
    return;
  }

  if (step.kind === 'file') {
    const input = document.createElement('input');
    input.type = 'file';
    input.accept = step.accept(flow);
    input.addEventListener('change', async () => {
      if (!input.files?.length) return;
      try {
        await step.apply(flow, input.files[0]);
        log(`${step.id} = ${input.files[0].name}`);
        next();
      } catch (error) {
        fail(error, () => renderStep());
      }
    });
    elements.panel.append(input);
    withBack(elements.panel);
    return;
  }

  if (step.kind === 'confirm') {
    elements.panel.append(line('Resolving firmware…'));
    try {
      await step.prepare(flow, progress);
    } catch (error) {
      fail(error, () => renderStep());
      return;
    }
    redraw();
    clear(elements.panel);
    elements.panel.append(line(step.title, 'title'));
    const plan = flow.state.plan;
    const summary = plan.engine === 'dfu'
      ? `dfu package ${plan.package.size} B${plan.erasePackage ? ` + erase ${plan.erasePackage.size} B` : ''}`
      : plan.files.map((f) => `0x${f.address.toString(16)} (${f.data.length} B)`).join(', ');
    elements.panel.append(line(`engine: ${plan.engine}`));
    elements.panel.append(line(summary));
    elements.panel.append(line(`eraseAll=${plan.eraseAll}  preserveFs=${plan.preserveFs}`));
    for (const note of flow.state.planNotes) {
      elements.panel.append(line(note, note.startsWith('UNVERIFIED') || note.startsWith('This erases') ? 'err' : null));
    }
    elements.panel.append(button(step.confirmLabel, () => next()));
    withBack(elements.panel);
    return;
  }

  elements.panel.append(line(JSON.stringify(flow.state.result ?? {}, null, 1)));
  elements.panel.append(button('Start over', () => start({ dryRun: flow.dryRun })));
}

// --- entry ------------------------------------------------------------------------------

async function start({ dryRun, family = null }) {
  // Restarting while the old flow still holds the port is what made a device in DFU read
  // as unrecognised: its session keeps a reader locked and `connect` never gets to probe.
  await flowApi.disposeFlow(flow);
  // The manifests live under the shipped tool until cutover (§8's baseUrl note), and the
  // deployed relay rejects a localhost origin — hence the override.
  const relayBase = new URLSearchParams(location.search).get('relay') ?? undefined;
  flow = flowApi.createFlow({ dryRun, family, manifestBase: '/pages/flasher/', relayBase });
  log(dryRun ? `--- dry run (${family}): no hardware is touched ---` : '--- live run ---');
  renderStep();
}

// A dry run has no device to read the family off, so the buttons stand in for the PID.
document.querySelector('#dry-esp32').addEventListener('click', () => start({ dryRun: true, family: 'esp32' }));
document.querySelector('#dry-nrf52').addEventListener('click', () => start({ dryRun: true, family: 'nrf52' }));
document.querySelector('#live').addEventListener('click', () => start({ dryRun: false }));

// Hands the port back to the OS so another tool can open it. The esptool transport keeps a
// reader locked, so it has to go first or the close does nothing.
async function releasePort() {
  const held = flow?.state;
  if (!held?.port && !held?.session) {
    log('nothing to release — no port is held');
    return;
  }
  if (held.session) {
    await closeEsptoolSession(held.session);
    held.session = null;
    log('esptool session closed');
  }
  if (held.port) {
    await closeSerialPortQuietly(held.port);
    log(`port released (${describePort(held.port)}) — the grant is unaffected`);
  }
  redraw();
}

document.querySelector('#release').addEventListener('click', releasePort);

const serialPanel = elements.serialPanel;
document.querySelector('#serial-toggle').addEventListener('click', () => {
  serialPanel.hidden = !serialPanel.hidden;
  if (!serialPanel.hidden) elements.serial.scrollTop = elements.serial.scrollHeight;
});
document.querySelector('#serial-close').addEventListener('click', () => { serialPanel.hidden = true; });
document.querySelector('#serial-clear').addEventListener('click', () => clear(elements.serial));
document.querySelector('#serial-copy').addEventListener('click', () => {
  navigator.clipboard?.writeText(elements.serial.innerText).catch(() => {});
});

// Exposed so a rig script can drive the flow without clicking.
window.__flow = { get flow() { return flow; }, api: flowApi, start, renderStep };
