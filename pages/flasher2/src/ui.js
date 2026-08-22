// The product renderer. Drives flow.js, which owns sequencing and stays DOM-free —
// this file owns everything visual, including the icons the steps only name.
//
// wireframe.js is kept beside it as the workflow test reference and shares nothing.

import * as flowApi from './flow.js';
import { PortSelectionRequiredError, promptForSerialPort } from './serial-port.js';

const elements = {
  wizard: document.getElementById('wizard'),
  back: document.getElementById('back'),
};

let flow = null;

// Named by flow.js so the step definitions carry no markup. Stroke icons at 24px,
// matching the header badge already in the shared sheet.
const ICONS = {
  'new-device':
    '<path d="M5 3h9a2 2 0 0 1 2 2v5"/><path d="M16 14v5a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2"/><path d="M19 3v6"/><path d="M22 6h-6"/>',
  upgrade:
    '<path d="M12 20V8"/><path d="m7 13 5-5 5 5"/><path d="M4 4h16"/>',
  infrastructure:
    '<circle cx="12" cy="5.5" r="1.75"/><path d="M12 7.5V22"/><path d="m8.5 22 3.5-8 3.5 8"/>' +
    '<path d="M7.9 2.4a7.5 7.5 0 0 0 0 9.2"/><path d="M16.1 2.4a7.5 7.5 0 0 1 0 9.2"/>',
  client:
    '<rect x="6" y="2" width="12" height="20" rx="2.5"/><path d="M10.75 18.25h2.5"/>',
  enhanced:
    '<path d="M12 3.5 13.7 8 18 9.7 13.7 11.4 12 15.9 10.3 11.4 6 9.7 10.3 8Z"/>' +
    '<path d="M18.5 15.5 19.3 17.4 21 18.2 19.3 19 18.5 21 17.7 19 16 18.2 17.7 17.4Z"/>',
  stock:
    '<path d="M21 8v8a2 2 0 0 1-1 1.73l-7 4a2 2 0 0 1-2 0l-7-4A2 2 0 0 1 3 16V8a2 2 0 0 1 1-1.73l7-4a2 2 0 0 1 2 0l7 4A2 2 0 0 1 21 8Z"/>' +
    '<path d="m3.3 7 8.7 5 8.7-5"/><path d="M12 22V12"/>',
  upload:
    '<path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"/><path d="m17 8-5-5-5 5"/><path d="M12 3v12"/>',
  device:
    '<rect x="4" y="4" width="16" height="16" rx="2.5"/><rect x="9" y="9" width="6" height="6" rx="1"/>' +
    '<path d="M9 2v2M15 2v2M9 20v2M15 20v2M2 9h2M2 15h2M20 9h2M20 15h2"/>',
};

function icon(name, size = 24) {
  const glyph = ICONS[name];
  if (!glyph) return '';
  return (
    `<svg width="${size}" height="${size}" viewBox="0 0 24 24" fill="none" stroke="currentColor" ` +
    `stroke-width="1.75" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">${glyph}</svg>`
  );
}

// --- shell ------------------------------------------------------------------------------

/**
 * Every step renders into the same three bands, so nothing below the card can shift.
 * The head is written in place and only when the words actually differ — rebuilding it
 * each step repaints identical text, which reads as a flicker between steps that share
 * a heading.
 */
function frame({ title, desc, centred = false }) {
  let step = elements.wizard.querySelector('.step');
  if (!step) {
    step = document.createElement('div');
    step.className = 'step';
    step.innerHTML =
      '<div class="step-head"><h2 class="step-title"></h2><p class="step-desc"></p></div>' +
      '<div class="step-body"></div><div class="step-foot"></div>';
    elements.wizard.replaceChildren(step);
  }

  const heading = step.querySelector('.step-title');
  const helper = step.querySelector('.step-desc');
  if (heading.textContent !== title) heading.textContent = title;
  if (helper.textContent !== (desc ?? '')) helper.textContent = desc ?? '';

  const body = step.querySelector('.step-body');
  const foot = step.querySelector('.step-foot');
  body.className = `step-body${centred ? ' is-centred' : ''}`;
  body.replaceChildren();
  foot.replaceChildren();
  return { body, foot };
}

// The shared sheet's .card::before reads this. Driving it from the step index beats
// the shipped flasher's approach of matching step titles with a MutationObserver.
function setProgress() {
  const steps = flowApi.applicableSteps(flow);
  const pct = (flow.stepIndex / Math.max(steps.length - 1, 1)) * 100;
  elements.wizard.style.setProperty('--step-progress', `${Math.max(pct, 8)}%`);
}

function syncBack() {
  elements.back.hidden = !flowApi.canGoBack(flow);
}

function advance() {
  flowApi.advance(flow);
  render();
}

// --- step kinds -------------------------------------------------------------------------

/** The generic chip, standing in for a vendor with no logo or a device with no photo. */
function placeholderArt() {
  const slot = document.createElement('span');
  slot.className = 'board-tile-icon is-placeholder';
  slot.innerHTML = icon('device', 34);
  return slot;
}

// Upstream's SPA host answers 200 with index.html for a file it does not have, so a
// missing image only ever announces itself by failing to decode — never by status.
function tileArt(image) {
  if (!image) return placeholderArt();
  const picture = document.createElement('img');
  picture.className = 'board-tile-icon';
  picture.src = image;
  picture.alt = '';
  picture.loading = 'lazy';
  picture.addEventListener('error', () => picture.replaceWith(placeholderArt()));
  return picture;
}

function renderChoice(step, options) {
  // `layout` is the step's own declaration; `choice` is the default card pair.
  const layout = step.layout ?? 'choice';
  const centred = layout === 'choice';
  const { body } = frame({ title: step.title, desc: step.desc, centred });
  const list = document.createElement('div');

  if (layout === 'board') {
    list.className = 'board-tiles';
    // Upstream artwork is drawn for a light background and is styled down; the custom
    // path's own PNGs are not.
    list.dataset.source = flow.state.source ?? '';
  }
  else if (layout === 'row') list.className = 'tiles';
  // Two options sit side by side; three would leave an orphan on a second row, so
  // anything past a pair stacks and the card turns horizontal instead.
  else list.className = `choice-grid${options.length > 2 ? ' is-stacked' : ''}`;

  for (const option of options) {
    const cell = document.createElement('button');
    cell.type = 'button';
    if (layout === 'board') {
      cell.className = 'board-tile';
      const name = document.createElement('strong');
      name.textContent = option.label;
      cell.append(tileArt(option.image), name);
    } else if (layout === 'row') {
      cell.className = option.image ? 'tile tile-with-icon' : 'tile';
      cell.innerHTML =
        (option.image ? `<img class="tile-icon" src="${option.image}" alt="" />` : '') +
        `<strong>${option.label}</strong>` +
        (option.note ? `<small>${option.note}</small>` : '');
    } else {
      cell.className = 'choice';
      cell.innerHTML =
        `<span class="choice-icon">${icon(option.icon, 20)}</span>` +
        `<span class="choice-text">` +
        `<span class="choice-title">${option.label}</span>` +
        (option.note ? `<span class="choice-desc">${option.note}</span>` : '') +
        `</span>`;
    }
    // The choice is the navigation — no confirm button on a reversible step.
    cell.addEventListener('click', async () => {
      await step.apply(flow, option.value);
      advance();
    });
    list.append(cell);
  }
  body.append(list);
}

/** Runs on entry and moves on by itself; the user only sees it if it is slow or fails. */
async function renderAction(step) {
  // The write gets the shipped flasher's screen: a real progress bar, the board's own
  // picture floating above the status line. Everything else is a spinner.
  const writing = step.id === 'flash';
  const { body } = frame({ title: step.title, desc: step.desc, centred: !writing });
  const status = document.createElement('p');
  status.className = 'status-text';
  let fill = null;

  if (writing) {
    const track = document.createElement('div');
    track.className = 'progress-track';
    fill = document.createElement('div');
    fill.className = 'progress-fill';
    track.append(fill);
    body.append(track);

    const art = flow.state.deviceIcon;
    if (art) {
      const picture = document.createElement('img');
      picture.className = 'flashing-icon';
      picture.src = art;
      picture.alt = '';
      picture.addEventListener('error', () => picture.remove());
      body.append(picture);
    }
    status.textContent = 'Starting…';
  } else {
    status.innerHTML = '<span class="spinner"></span>Working…';
  }
  body.append(status);

  const io = {
    onStatus: (message) => {
      if (writing) status.textContent = message;
      else status.innerHTML = `<span class="spinner"></span>${message}`;
    },
    onProgress: (fraction) => {
      if (fill) fill.style.width = `${Math.round(fraction * 100)}%`;
    },
  };

  try {
    await step.run(flow, io);
    advance();
  } catch (error) {
    if (error instanceof PortSelectionRequiredError) return renderCheckpoint(error, step);
    renderError(error, () => render());
  }
}

// §11.4. Only a click may call requestPort(), so the port module raises and the
// escalation has to become a button here. Reuses the step's own title and helper
// text: the checkpoint is the same screen waiting on a click, not a new one.
function renderCheckpoint(error, step) {
  const { body } = frame({ title: step.title, desc: step.desc, centred: true });
  const art = document.createElement('div');
  art.className = 'connect-art';
  art.innerHTML = icon('device', 56);
  body.append(art);

  const actions = document.createElement('div');
  actions.className = 'actions';
  actions.style.justifyContent = 'center';
  const pick = document.createElement('button');
  pick.type = 'button';
  pick.className = 'btn btn-primary';
  pick.textContent = 'Select device';
  pick.addEventListener('click', async () => {
    try {
      flow.state.port = await promptForSerialPort();
    } catch {
      return; // Picker dismissed; leave the step as it was.
    }
    render();
  });
  actions.append(pick);
  body.append(actions);
}

function renderError(error, retry) {
  const { body, foot } = frame({ title: 'Something went wrong', centred: true });
  const box = document.createElement('div');
  box.className = 'error-box';
  box.textContent = error.message;
  body.append(box);

  const actions = document.createElement('div');
  actions.className = 'actions';
  actions.style.justifyContent = 'center';
  const again = document.createElement('button');
  again.type = 'button';
  again.className = 'btn btn-secondary';
  again.textContent = 'Try again';
  again.addEventListener('click', retry);
  actions.append(again);
  foot.append(actions);
}

// --- driver -----------------------------------------------------------------------------

async function render() {
  const step = flowApi.currentStep(flow);
  setProgress();
  syncBack();
  if (!step) return;

  if (step.kind === 'action') return renderAction(step);

  if (step.kind === 'choice') {
    let options;
    try {
      options = await step.options(flow);
    } catch (error) {
      return renderError(error, () => render());
    }
    return renderChoice(step, options);
  }

  // Everything past the binary choice is still the wireframe's job.
  frame({ title: step.title, desc: `This step has no interface yet (${step.kind}).`, centred: true });
}

elements.back.addEventListener('click', () => {
  flowApi.goBack(flow);
  render();
});

flow = flowApi.createFlow({
  manifestBase: '/pages/flasher/',
  relayBase: new URLSearchParams(location.search).get('relay') ?? undefined,
});
render();

// Exposed so a rig script can render a step without clicking, as wireframe.js does.
window.__ui = { get flow() { return flow; }, api: flowApi, render };
