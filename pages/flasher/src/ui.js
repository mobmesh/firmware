// The product renderer. flow.js owns sequencing and stays DOM-free; this file owns
// everything visual, including the icons the steps only name.

import * as flowApi from './flow.js';
import { findAvailablePrefix, generateIdentityKeypair, extractPrefix } from './gcm-reg.js';
import { TILE_STAGGER_MS } from './constants.js';
import { PortSelectionRequiredError, promptForSerialPort } from './serial-port.js';
import { ManualEntryRequiredError } from './esp32.js';
import { FilePickerRequiredError, writeBootloaderUf2 } from './nrf52.js';
import { loadBuildInfo } from './build-info.js';

const elements = {
  wizard: document.getElementById('wizard'),
  back: document.getElementById('back'),
};

let flow = null;
// An nRF52 needs a second grant: it re-enumerates into DFU under a different USB id, so
// the picker legitimately reappears. Unlabelled, that reads as the first click failing.
let checkpoints = 0;

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
  // Diagonal corner arrows: the pair reads as one axis of movement, where the bracket
  // corners read as a frame and sat oddly against the map's own square controls.
  expand:
    '<polyline points="15 3 21 3 21 9"/><polyline points="9 21 3 21 3 15"/>' +
    '<line x1="21" y1="3" x2="14" y2="10"/><line x1="3" y1="21" x2="10" y2="14"/>',
  collapse:
    '<polyline points="4 14 10 14 10 20"/><polyline points="20 10 14 10 14 4"/>' +
    '<line x1="14" y1="10" x2="21" y2="3"/><line x1="3" y1="21" x2="10" y2="14"/>',
  // The Gulf Coast Mesh mark, as it appears in mobmesh.org's own footer: five nodes
  // and the links between them.
  gcm:
    '<circle cx="6" cy="6" r="1.6" fill="currentColor" stroke="none"/>' +
    '<circle cx="18" cy="6" r="1.6" fill="currentColor" stroke="none"/>' +
    '<circle cx="12" cy="13" r="1.6" fill="currentColor" stroke="none"/>' +
    '<circle cx="6" cy="20" r="1.6" fill="currentColor" stroke="none"/>' +
    '<circle cx="18" cy="20" r="1.6" fill="currentColor" stroke="none"/>' +
    '<path d="M6 6 12 13M18 6 12 13M12 13 6 20M12 13 18 20"/>',
  // Roles. Upstream ships no artwork for these, so they are drawn here.
  repeater:
    '<circle cx="12" cy="5.5" r="1.75"/><path d="M12 7.5V22"/><path d="m8.5 22 3.5-8 3.5 8"/>' +
    '<path d="M7.9 2.4a7.5 7.5 0 0 0 0 9.2"/><path d="M16.1 2.4a7.5 7.5 0 0 1 0 9.2"/>',
  roomServer:
    '<path d="M21 14.5a2 2 0 0 1-2 2H8l-4 4V5.5a2 2 0 0 1 2-2h13a2 2 0 0 1 2 2Z"/>' +
    '<path d="M8 8.5h8M8 12h5"/>',
  // KISS is a bare packet-radio modem: no node identity, just framed data on the air.
  kissRadio: '<path d="M2 12h3.2l2.4-7 3.9 14 2.9-10 2.3 3H22"/>',
  companionBle: '<path d="m7.5 7.5 9 9-4.5 4.5V3l4.5 4.5-9 9"/>',
  companionUsb:
    '<path d="M12 22v-6"/><path d="M9.5 8V2M14.5 8V2"/><path d="M7 8h10v4.5a5 5 0 0 1-10 0Z"/>',
  gui:
    '<rect x="2.5" y="3.5" width="19" height="13" rx="2"/><path d="M9 20.5h6"/><path d="M12 16.5v4"/>',
  guiSD:
    '<path d="M16.5 2.5H8.5l-4 4v13a2 2 0 0 0 2 2h10a2 2 0 0 0 2-2v-15a2 2 0 0 0-2-2Z"/>' +
    '<path d="M9.5 6.5v2.5M12 5.5v4M14.5 5.5v4"/>',
  device:
    '<rect x="4" y="4" width="16" height="16" rx="2.5"/><rect x="9" y="9" width="6" height="6" rx="1"/>' +
    '<path d="M9 2v2M15 2v2M9 20v2M15 20v2M2 9h2M2 15h2M20 9h2M20 15h2"/>',
  // The USB trident, so "plug it in" reads as the connector rather than the board.
  usb:
    '<circle cx="10" cy="7" r="1"/><circle cx="4" cy="20" r="1"/><path d="M4.7 19.3 19 5"/>' +
    '<path d="m21 3-3 1 2 2Z"/><path d="M9.26 7.68 5 12l2 5"/><path d="m10 14 5 2 3.5-3.5"/>' +
    '<path d="m18 12 1-1 1 1-1 1Z"/>',
  upload:
    '<path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"/><path d="M12 15V3"/><path d="m7.5 7.5 4.5-4.5 4.5 4.5"/>',
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
 * Every step renders into the same three bands, so nothing below the card shifts. The
 * head is rewritten only when the words differ, or shared headings flicker.
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
  step.querySelector('.step-head').classList.remove('is-centred');
  return { body, foot, head: step.querySelector('.step-head') };
}

// The shared sheet's .card::before reads this. Driving it from the step index beats
// the shipped flasher's approach of matching step titles with a MutationObserver.
function setProgress() {
  const steps = flowApi.applicableSteps(flow);
  const pct = (flow.stepIndex / Math.max(steps.length - 1, 1)) * 100;
  elements.wizard.style.setProperty('--step-progress', `${Math.max(pct, 8)}%`);
}

/** A step's title or desc may be a function of the flow, for copy that names a choice. */
function text(value) {
  return typeof value === 'function' ? value(flow) : value;
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
function tileArt(image, filter, dim = true, plate = true) {
  if (!image) return placeholderArt();
  const picture = document.createElement('img');
  picture.src = image;
  picture.alt = '';
  picture.loading = 'lazy';

  // A filter applies to the whole element, background and border included, so filtered
  // art hands the plate to a wrapper the filter cannot reach.
  const slot = filter ? document.createElement('span') : picture;
  if (filter) {
    slot.className = 'board-tile-icon is-framed';
    picture.style.filter = filter;
    slot.append(picture);
  } else {
    slot.className = 'board-tile-icon';
  }
  if (!dim) slot.dataset.nodim = '';
  if (!plate) slot.dataset.noplate = '';

  picture.addEventListener('error', () => slot.replaceWith(placeholderArt()));
  return slot;
}

/** Nothing to choose from. Back is the only move, and the nav already offers it. */
function renderEmptyChoice(step) {
  const { body } = frame({ title: text(step.title), desc: null, centred: true });
  const note = document.createElement('p');
  note.className = 'empty-note';
  note.textContent =
    step.emptyNote ?? 'Nothing here matches what you picked earlier. Go back and change it.';
  body.append(note);
}

/**
 * Caps a tile row so its rows come out even: 6 tiles in a 4-wide card become 3 + 3, not
 * 4 + 2. Measures the real tile and gap rather than assuming the stylesheet's numbers.
 */
function balanceTileRows(list, body) {
  const tile = list.firstElementChild;
  if (!tile) return;
  const bodyStyle = getComputedStyle(body);
  const available = body.clientWidth
    - parseFloat(bodyStyle.paddingLeft) - parseFloat(bodyStyle.paddingRight);
  const gap = parseFloat(getComputedStyle(list).columnGap) || 0;
  const width = tile.getBoundingClientRect().width;
  if (!available || !width) return;

  const perRow = Math.max(1, Math.floor((available + gap) / (width + gap)));
  const count = list.childElementCount;
  if (count <= perRow) {
    staggerTiles(list, perRow);
    return;
  }
  const columns = Math.ceil(count / Math.ceil(count / perRow));
  list.style.maxWidth = `${columns * width + (columns - 1) * gap}px`;
  staggerTiles(list, columns);
}

// A diagonal wave, so the grid resolves left-to-right and top-down at once rather than
// as a typewriter. The column count is only known after the layout above, and it moves
// with the viewport, so the delay is set here rather than from the render loop's index.
function staggerTiles(list, columns) {
  [...list.children].forEach((tile, i) => {
    const wave = Math.floor(i / columns) + (i % columns);
    tile.style.setProperty('--tile-delay', `${wave * TILE_STAGGER_MS}ms`);
  });
}

function renderChoice(step, options) {
  // The catalogue is upstream's and changes between releases; a step that filtered down
  // to nothing must say so rather than paint an empty card with no way on.
  if (!options.length) return renderEmptyChoice(step);
  // `layout` is the step's own declaration; `choice` is the default card pair.
  const layout = step.layout ?? 'choice';
  const centred = layout === 'choice';
  const { body, foot } = frame({ title: text(step.title), desc: text(step.desc), centred });
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
      cell.append(
        tileArt(option.image, option.imageFilter, option.imageDim, option.imagePlate), name);
    } else if (layout === 'row') {
      // Either a real picture (the custom path's PNGs) or a named glyph, never both.
      const glyph = option.icon
        ? `<span class="tile-icon is-glyph">${icon(option.icon, 26)}</span>`
        : '';
      cell.className = option.image || glyph ? 'tile tile-with-icon' : 'tile';
      cell.innerHTML =
        (option.image ? '' : glyph) +
        `<strong>${option.label}</strong>` + (option.note ? `<small>${option.note}</small>` : '');
      if (option.image) {
        const picture = document.createElement('img');
        picture.className = 'tile-icon';
        picture.src = option.image;
        picture.alt = '';
        // Art that fails to decode falls back to the glyph, or to no art at all.
        picture.addEventListener('error', () => {
          picture.remove();
          if (glyph) cell.insertAdjacentHTML('afterbegin', glyph);
          else cell.className = 'tile';
        });
        cell.prepend(picture);
      }
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
  // A board grid staggers as a wave, which needs its column count; the card pairs are
  // one row, and a single column makes the same helper stagger them in order.
  if (layout === 'board') balanceTileRows(list, body);
  else if (layout === 'choice') staggerTiles(list, 1);

  // A function lets a step offer its escape hatch on some branches and not others.
  const aside = typeof step.aside === 'function' ? step.aside(flow) : step.aside;
  if (aside) {
    const link = document.createElement('button');
    link.type = 'button';
    link.className = aside.variant === 'pill' ? 'aside-pill' : 'aside-link';
    link.innerHTML = aside.icon === null ? '<span></span>' : `${icon(aside.icon ?? 'upload', 13)}<span></span>`;
    link.querySelector('span').textContent = aside.label;
    link.addEventListener('click', () => {
      aside.run(flow);
      render();
    });
    foot.append(link);
  }
}

/** Runs on entry and moves on by itself; the user only sees it if it is slow or fails. */
async function renderAction(step) {
  // Progress bar and board picture for the countable steps — the write, and the settings
  // pass when it has commands. Everything else, including an empty pass, gets a spinner.
  const writing =
    step.id === 'flash' ||
    (step.id === 'provision' && flowApi.plannedProvisionCommands(flow).length > 0);
  const { body } = frame({ title: text(step.title), desc: text(step.desc), centred: !writing });
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
    status.textContent = step.id === 'provision' ? 'Waiting for device to reboot…' : 'Starting…';
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
    if (error instanceof FilePickerRequiredError) return renderFilePickerCheckpoint(error, step);
    // The gesture re-enumerates the board, so retry has to re-acquire, not re-probe.
    if (error instanceof ManualEntryRequiredError) {
      return renderError(error, async () => {
        await flowApi.rewindToConnect(flow);
        render();
      });
    }
    renderError(error, () => render());
  }
}

// Only a click may call requestPort(), so the escalation becomes a button here. Reuses
// the step's own text: this is the same screen waiting on a click, not a new one.
function renderCheckpoint(error, step) {
  checkpoints += 1;
  const { body } = frame({ title: text(step.title), desc: text(step.desc), centred: true });
  const art = document.createElement('div');
  art.className = 'connect-art';
  art.innerHTML = icon('usb', 56);
  body.append(art);

  const actions = document.createElement('div');
  actions.className = 'actions';
  actions.style.justifyContent = 'center';
  const pick = document.createElement('button');
  pick.type = 'button';
  pick.className = 'btn btn-primary';
  pick.textContent = checkpoints > 1 ? 'Reconnect' : 'Select device';
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

// Only a click may call showSaveFilePicker(), so the write itself happens here — never
// inside the step's run(), which has no gesture by the time it's called.
function renderFilePickerCheckpoint(error, step) {
  const { body } = frame({ title: text(step.title), desc: error.message, centred: true });
  const art = document.createElement('div');
  art.className = 'connect-art';
  art.innerHTML = icon('usb', 56);
  body.append(art);
  const status = document.createElement('p');
  status.className = 'status-text';
  body.append(status);

  const actions = document.createElement('div');
  actions.className = 'actions';
  actions.style.justifyContent = 'center';
  const save = document.createElement('button');
  save.type = 'button';
  save.className = 'btn btn-primary';
  save.textContent = 'Save to drive';
  save.addEventListener('click', async () => {
    save.disabled = true;
    try {
      flow.state.port = await writeBootloaderUf2(flow.state.port, error.bytes, error.suggestedName, {
        onStatus: (message) => { status.textContent = message; },
      });
      flow.state.bootloaderUpdated = error.suggestedName;
    } catch (writeError) {
      if (writeError?.name === 'AbortError') {
        save.disabled = false;
        return; // Picker dismissed; leave the checkpoint as it was.
      }
      renderError(writeError, () => render());
      return;
    }
    render();
  });
  actions.append(save);
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


// --- location (GCM's node-config screen) --------------------------------------------

// region-map's own basemap: already dark, already attributed, already vendored.
// The advert budget, minus the zone's name prefix. Advisory, not a cap: measured on a
// Heltec v4, the firmware stores 31 bytes either way and only the advert clips.
const NAME_BYTES = 20;
const NAME_STORED_BYTES = 31;

// Stands in for a password that is set but cannot be read back — no `get` exists for it.
// Never sent: the field clears on first touch, and untouched means no command.
const PASSWORD_PLACEHOLDER = '•'.repeat(9);

// CARTO now requires a key on basemap requests. It travels in the style URL, so it is a
// public client-side identifier, not a credential -- restrict it by domain at CARTO.
const MAP_KEY = 'cb1_2x0t_1_75c947c7bc617ea02acd8b7a';
// The vector style, not raster tiles: its label layers are individually addressable, which
// is the only way to make street names arrive earlier and carry our own palette.
const MAP_STYLE = `https://basemaps.cartocdn.com/gl/dark-matter-gl-style/style.json?key=${MAP_KEY}`;
// Dark Matter hides street names until they are nearly all that is left -- roadname_major
// at z13, _minor not until z16. Someone siting a node reads streets well before that.
const MAP_EARLY_ZOOM = 3;
// MapLibre zooms continuously where Leaflet snapped to whole levels, so its defaults read
// as slow next to the old map. Rates from maplibre's own ScrollZoomHandler.
const MAP_WHEEL_RATE = 1 / 120;   // default 1/450
const MAP_ZOOM_RATE = 1 / 60;     // default 1/100
// Mobile, as `pages/shared/data/gc-locations.json` gives it. Held here rather than read
// from that file: it is only the opening view, and a failed fetch would need a
// hardcoded fallback anyway. The previous value sat offshore in the Gulf.
const MAP_HOME = [30.6954, -88.0399];
const MAP_HOME_ZOOM = 6;

/**
 * Mines a keypair the registry will accept. Read-only: `reservePrefix` creates a real
 * public record and is never called here, and a silent registry is not an error.
 */
async function resolveIdentity(onStatus) {
  onStatus('Generating a node identity…');
  const found = await findAvailablePrefix(generateIdentityKeypair, {
    onProgress: (attempt, total) =>
      attempt > 1 && onStatus(`Prefix taken — trying another (${attempt} of ${total})…`),
  });
  if (found) return { identity: found.keypair, status: 'available' };

  // Either every attempt collided or the registry is unreachable; keep the key either way.
  const identity = await generateIdentityKeypair();
  return { identity, status: 'unchecked' };
}

// Nested zones are drawn only under the pointer; states are always on.
const STATE_STYLE = { color: '#2dd1bd', weight: 1, opacity: 0.6, fillColor: '#2dd1bd', fillOpacity: 0.04 };
const ZONE_SHOWN = { color: '#f9a228', weight: 2, fillColor: '#f9a228', opacity: 1, fillOpacity: 0.04 };
// Derived, because `setStyle` merges: anything omitted here would survive the change back.
const ZONE_HIDDEN = { ...ZONE_SHOWN, opacity: 0, fillOpacity: 0 };
// Matches no feature, so the lit-zone layers draw nothing until one is named.
const NO_ZONE = '\u0000none';

/** `{ zone, zoneSettings }` for a point, in the shape the step's draft carries. */
function readZone(zones, lat, lon) {
  const hit = zoneAt(zones, lat, lon);
  return { zone: hit?.id ?? null, zoneSettings: hit?.settings ?? null };
}

// Set while the map is expanded, so Escape has something to close and nothing leaks.
let expandedMap = null;
document.addEventListener('keydown', (event) => {
  if (event.key === 'Escape') expandedMap?.();
});

// Subscription zones (data/zones.geojson). Loaded once, on the step that shows them —
// the flash path never touches this.
let zonesPromise = null;
function loadZones() {
  zonesPromise ??= fetch(new URL('../data/zones.geojson', import.meta.url), { cache: 'no-store' })
    .then((res) => (res.ok ? res.json() : Promise.reject(new Error(`HTTP ${res.status}`))))
    .catch((error) => {
      // Cosmetic and informational only, so a missing file must not break the step.
      console.warn('[ui] Could not load the zone overlay:', error);
      return null;
    });
  return zonesPromise;
}

// Ray casting against a feature's outer rings. Holes are ignored: none of these shapes
// has one, and a wrong answer inside a hole would still be a neighbouring zone.
function featureContains(feature, lat, lon) {
  const { type, coordinates } = feature.geometry;
  const polygons = type === 'Polygon' ? [coordinates] : coordinates;
  return polygons.some((polygon) => {
    const ring = polygon[0];
    let inside = false;
    for (let i = 0, j = ring.length - 1; i < ring.length; j = i++) {
      const [xi, yi] = ring[i];
      const [xj, yj] = ring[j];
      if ((yi > lat) !== (yj > lat) && lon < ((xj - xi) * (lat - yi)) / (yj - yi) + xi) inside = !inside;
    }
    return inside;
  });
}

/** The home area containing a point: a zone if one covers it, otherwise the state around it. */
function zoneAt(zones, lat, lon) {
  if (!zones) return null;
  const hits = zones.features.filter((feature) => featureContains(feature, lat, lon));
  if (!hits.length) return null;
  const pick = hits.find((feature) => feature.properties.kind === 'zone') ?? hits[0];
  return { id: pick.properties.id, settings: pick.properties.settings ?? null };
}

function renderLocation(step) {
  const { body, foot, head } = frame({ title: text(step.title), desc: text(step.desc) });
  head.classList.add('is-centred');
  const s = flow.state;
  const isUpgrade = s.install === flowApi.INSTALL.UPDATE;
  // Read at arm while the application still answered. Pre-filling is what makes an
  // Upgrade safe to walk through: continuing unchanged sends nothing.
  const existing = s.existingConfig ?? null;
  const draft = {
    name: s.nodeName || existing?.name || '',
    latitude: s.latitude ?? existing?.latitude ?? null,
    longitude: s.longitude ?? existing?.longitude ?? null,
    heightFt: s.heightFt || '',
    email: s.email || '',
    adminPassword: s.adminPassword || '',
    identity: s.identity,
    identityStatus: s.identityStatus,
  };

  const roleName = flowApi.selectedRoleName(flow) ?? 'Repeater';
  const noun = roleName.toLowerCase();
  const form = document.createElement('div');
  form.className = 'location-form';
  form.innerHTML =
    '<div class="name-wrap">' +
    `<input type="text" class="field" id="node-name" placeholder="${roleName} name" maxlength="31" />` +
    '<span class="name-count" id="name-count" aria-hidden="true"></span>' +
    '</div>' +
    '<p class="field-note is-quiet" id="name-keeps-id" hidden>Renaming is safe (existing mesh ID preserved)</p>' +
    '<p class="field-note is-warn" id="name-warn" hidden>fyi: this name will be clipped on radio broadcasts</p>' +
    '<div class="map-wrap">' +
    '<div class="map-frame" id="map"></div>' +
    `<button type="button" class="map-expand" id="map-expand" title="Expand map"` +
    ` aria-label="Expand map">${icon('expand', 16)}</button>` +
    '<span class="map-hint">Drag to pan · scroll or +/\u2212 to zoom · click to place</span>' +
    `<div class="map-empty" id="map-empty"><span>Click map to pin ${noun} location</span></div>` +
    '</div>' +
    '<div class="coord-row" id="coords" hidden>' +
    '<label class="coord">Latitude<input type="number" step="0.000001" class="field" id="lat" /></label>' +
    '<label class="coord">Longitude<input type="number" step="0.000001" class="field" id="lon" /></label>' +
    '</div>' +
    '<div class="field-row">' +
    '<input type="number" class="field" id="height" placeholder="Height (ft)" />' +
    '<input type="email" class="field" id="email" placeholder="Contact email" required />' +
    '</div>' +
    // Deliberately not a password field: this is a value being *set* on a device, not a
    // credential being recalled. A masked typo here is only discovered on the next login.
    `<input type="text" class="field" id="admin-password" placeholder="Admin password" ` +
    `autocomplete="off" spellcheck="false" />` +
    `<p class="field-note" id="password-note">Set your ${noun}\u2019s admin password</p>`;
  body.append(form);

  // Assigned once the button exists; every field calls it so the button's state and the
  // hint under it are derived from the draft rather than kept in step by hand.
  let refreshContinue = () => {};
  const nameField = form.querySelector('#node-name');
  const latField = form.querySelector('#lat');
  const lonField = form.querySelector('#lon');
  // A status, not a field: it lives in the footer so the map cannot push it below the fold.
  for (const [id, key] of [['#height', 'heightFt'], ['#email', 'email'], ['#admin-password', 'adminPassword']]) {
    const field = form.querySelector(id);
    field.value = draft[key];
    field.addEventListener('input', () => { draft[key] = field.value; refreshContinue(); });
  }

  // There is no `get` for the admin password, so an Upgrade cannot show the real one and
  // cannot tell whether it changed. A placeholder says one is set; touching the field
  // clears it, and leaving it alone sends no `password` command at all.
  const passwordField = form.querySelector('#admin-password');
  const passwordNote = form.querySelector('#password-note');
  // The map is not a form field, so its expand control is what a validation message
  // can focus to put the pointer in the right place.
  const mapExpand = form.querySelector('#map-expand');
  let passwordUntouched = false;
  if (isUpgrade && !draft.adminPassword) {
    passwordField.value = PASSWORD_PLACEHOLDER;
    passwordUntouched = true;
    // While the placeholder stands, a password is already set and the note names the
    // field rather than instructing. The role is named twice above this line already.
    passwordNote.textContent = 'Admin password';
    const clearOnce = () => {
      if (!passwordUntouched) return;
      passwordUntouched = false;
      passwordField.value = '';
      draft.adminPassword = '';
      // Cleared: setting one is required again, so the note asks for it.
      passwordNote.textContent = 'Set admin password';
      refreshContinue();
    };
    passwordField.addEventListener('focus', clearOnce);
    passwordField.addEventListener('pointerdown', clearOnce);
  }

  const coordRow = form.querySelector('#coords');
  const emptyNote = form.querySelector('#map-empty');
  const identityLine = document.createElement('p');
  identityLine.className = 'identity-line';
  identityLine.textContent = 'Generating identity…';
  foot.append(identityLine);

  // The site mark stands in for naming the registry; the prefix is the only part worth
  // reading, so the line stays a mark plus four characters.
  const showIdentity = (prefix, status) => {
    identityLine.replaceChildren();
    const mark = document.createElement('span');
    mark.className = 'identity-mark';
    mark.innerHTML = icon('gcm', 16);
    identityLine.append(mark, `${prefix} ${status === 'available' ? 'Verified' : 'Unverified'}`);
  };
  nameField.value = draft.name;

  // Counted encoded -- an emoji is four bytes, so characters would overcount.
  const encoder = new TextEncoder();
  const counter = form.querySelector('#name-count');
  const nameWarn = form.querySelector('#name-warn');
  // Answers the question renaming provokes, at the field that provokes it. Upgrade only:
  // a new device has no identity to keep.
  const nameKeepsId = form.querySelector('#name-keeps-id');
  const countName = () => {
    const used = encoder.encode(nameField.value).length;
    counter.textContent = `${used}/${NAME_BYTES}`;
    counter.classList.toggle('is-full', used === NAME_BYTES);
    counter.classList.toggle('is-over', used > NAME_BYTES);
    const clipping = used >= NAME_BYTES + 2;
    nameWarn.hidden = !clipping;
    // One note at a time: a warning about what was just typed outranks a standing fact.
    nameKeepsId.hidden = !isUpgrade || clipping;
  };
  nameField.addEventListener('input', () => {
    while (encoder.encode(nameField.value).length > NAME_STORED_BYTES) {
      nameField.value = [...nameField.value].slice(0, -1).join('');
    }
    draft.name = nameField.value;
    countName();
    refreshContinue();
  });
  countName();

  // GL zoom is one level off Leaflet's for the same scale, hence the -1 throughout.
  const startCentre = draft.latitude != null ? [draft.longitude, draft.latitude]
                                             : [MAP_HOME[1], MAP_HOME[0]];
  const map = new maplibregl.Map({
    container: form.querySelector('#map'),
    style: MAP_STYLE,
    center: startCentre,
    zoom: (draft.latitude != null ? 13 : MAP_HOME_ZOOM) - 1,
    maxZoom: 19,
    attributionControl: { compact: false },
    // Labels cross-fade over 300ms by default, which is most of why a zoom reads as still
    // settling after the old map's had landed.
    fadeDuration: 0,
  });
  map.addControl(new maplibregl.NavigationControl({ showCompass: false }), 'top-left');
  map.scrollZoom.setWheelZoomRate(MAP_WHEEL_RATE);
  map.scrollZoom.setZoomRate(MAP_ZOOM_RATE);
  // Nothing here is a 3D map, and a stray two-finger drag that tilts it cannot be undone
  // from the keyboard.
  map.dragRotate.disable();
  map.touchZoomRotate.disableRotation();

  // Dark Matter's own ground is a warm grey that reads washed out against the sheet.
  // Only the wide, flat areas are repainted: roads and labels carry the detail.
  const GROUND = {
    background: '#061320',                       // --bg-1
    landcover: 'rgba(45, 209, 189, 0.03)',
    landuse: 'rgba(255, 255, 255, 0.02)',
    landuse_residential: 'rgba(255, 255, 255, 0.02)',
    park_national_park: 'rgba(45, 209, 189, 0.05)',
    park_nature_reserve: 'rgba(45, 209, 189, 0.05)',
    water: '#0a2033',
    water_shadow: '#08192a',
    building: 'rgba(255, 255, 255, 0.04)',
    'building-top': 'rgba(255, 255, 255, 0.05)',
  };

  // The style ships 27 symbol layers. Each is restyled into our palette, and the street
  // classes are brought forward so they appear before the map is nearly useless.
  map.on('style.load', () => {
    for (const [id, colour] of Object.entries(GROUND)) {
      const layer = map.getLayer(id);
      if (!layer) continue;
      map.setPaintProperty(id, layer.type === 'background' ? 'background-color' : 'fill-color', colour);
    }
    // Dark Matter draws roads for a light ground and they read as heavy here. The casings
    // are the outlines under each road; dropping them alone recovers most of the weight.
    for (const layer of map.getStyle().layers) {
      if (layer.type !== 'line') continue;
      if (/_case$/.test(layer.id)) map.setPaintProperty(layer.id, 'line-opacity', 0.25);
      else if (/^road_|^tunnel_|^bridge_/.test(layer.id)) {
        map.setPaintProperty(layer.id, 'line-opacity', 0.55);
      }
    }
    for (const layer of map.getStyle().layers) {
      if (layer.type !== 'symbol') continue;
      // Water names and house numbers are clutter at every zoom this step uses.
      if (/water|housenumber/i.test(layer.id)) {
        map.setLayoutProperty(layer.id, 'visibility', 'none');
        continue;
      }
      map.setPaintProperty(layer.id, 'text-color', '#e8f0f8');
      map.setPaintProperty(layer.id, 'text-halo-color', 'rgba(3, 8, 15, 0.9)');
      map.setPaintProperty(layer.id, 'text-halo-width', 1.6);
      if (layer.id.startsWith('roadname_') || layer.id.startsWith('poi_')) {
        map.setLayerZoomRange(layer.id, Math.max(0, (layer.minzoom ?? 0) - MAP_EARLY_ZOOM),
                              layer.maxzoom ?? 24);
      }
    }
  });
  let pin = null;
  let zones = null;

  // No labels or hover: this step picks a home area to size the settings against, not a
  // site. The fills stack where a zone sits on its state, so both are kept very low.
  loadZones().then((loaded) => {
    zones = loaded;
    if (!zones) return;
    const addZoneLayers = () => {
      if (map.getSource('zones')) return;
      map.addSource('zones', { type: 'geojson', data: zones });
      // States, drawn faintly and always.
      map.addLayer({
        id: 'zones-state-fill', type: 'fill', source: 'zones',
        filter: ['!=', ['get', 'kind'], 'zone'],
        paint: { 'fill-color': STATE_STYLE.fillColor, 'fill-opacity': STATE_STYLE.fillOpacity },
      });
      map.addLayer({
        id: 'zones-state-line', type: 'line', source: 'zones',
        filter: ['!=', ['get', 'kind'], 'zone'],
        paint: { 'line-color': STATE_STYLE.color, 'line-width': STATE_STYLE.weight,
                 'line-opacity': STATE_STYLE.opacity },
      });
      // Nested zones stay invisible until the pointer is inside one — the outlines are
      // only useful while you are choosing, and drawn always they crowd a 256px map. The
      // filter names which single zone is lit; NO_ZONE matches nothing.
      map.addLayer({
        id: 'zone-lit-fill', type: 'fill', source: 'zones',
        filter: ['==', ['get', 'id'], NO_ZONE],
        paint: { 'fill-color': ZONE_SHOWN.fillColor, 'fill-opacity': ZONE_SHOWN.fillOpacity },
      });
      map.addLayer({
        id: 'zone-lit-line', type: 'line', source: 'zones',
        filter: ['==', ['get', 'id'], NO_ZONE],
        paint: { 'line-color': ZONE_SHOWN.color, 'line-width': ZONE_SHOWN.weight },
      });
    };
    if (map.isStyleLoaded()) addZoneLayers();
    else map.on('load', addZoneLayers);

    let lit = NO_ZONE;
    const light = (code) => {
      if (code === lit) return;
      lit = code;
      if (!map.getLayer('zone-lit-fill')) return;
      map.setFilter('zone-lit-fill', ['==', ['get', 'id'], code]);
      map.setFilter('zone-lit-line', ['==', ['get', 'id'], code]);
    };
    // The same ray casting the pin uses, rather than queryRenderedFeatures: a zone whose
    // fill is filtered out is not rendered, so the GPU cannot report it.
    map.on('mousemove', (event) => {
      const hit = zones.features.find(
        (feature) => feature.properties.kind === 'zone'
          && featureContains(feature, event.lngLat.lat, event.lngLat.lng)
      );
      light(hit?.properties.id ?? NO_ZONE);
    });
    map.on('mouseout', () => light(NO_ZONE));

    // A pin restored from an earlier visit predates the fetch.
    if (draft.latitude != null) {
      Object.assign(draft, readZone(zones, draft.latitude, draft.longitude));
    }
  });

  function setPoint(lat, lon, recentre) {
    draft.latitude = Number(lat.toFixed(6));
    draft.longitude = Number(lon.toFixed(6));
    latField.value = draft.latitude;
    lonField.value = draft.longitude;
    if (pin) pin.setLngLat([draft.longitude, draft.latitude]);
    else {
      pin = new maplibregl.Marker({ draggable: true, color: '#f9a228' })
        .setLngLat([draft.longitude, draft.latitude])
        .addTo(map);
      pin.on('dragend', () => { const p = pin.getLngLat(); setPoint(p.lat, p.lng, false); });
    }
    Object.assign(draft, readZone(zones, draft.latitude, draft.longitude));
    if (recentre) {
      map.jumpTo({ center: [draft.longitude, draft.latitude], zoom: Math.max(map.getZoom(), 12) });
    }
    // Coordinates appear only once there is something to show, as GCM's does.
    coordRow.hidden = false;
    emptyNote.hidden = true;
    refreshContinue();
  }

  // Expanding hides the fields and grows the frame; the map has to be told the box moved.
  const stepEl = body.closest('.step');
  const toggle = form.querySelector('#map-expand');
  const setExpanded = (on) => {
    stepEl.classList.toggle('is-map-expanded', on);
    toggle.innerHTML = icon(on ? 'collapse' : 'expand', 16);
    const label = on ? 'Restore map size' : 'Expand map';
    toggle.title = label;
    toggle.setAttribute('aria-label', label);
    expandedMap = on ? () => setExpanded(false) : null;
    map.resize();
  };
  toggle.addEventListener('click', () => setExpanded(!stepEl.classList.contains('is-map-expanded')));
  // A step left expanded is gone with its DOM; drop the stale closer.
  expandedMap = null;

  if (draft.latitude != null) setPoint(draft.latitude, draft.longitude, false);
  map.on('click', (event) => {
    const { lat, lng } = event.lngLat;
    setPoint(lat, lng, false);
    // Placing the pin is the point of expanding, so hand the form back. Recentre with it:
    // the centre is held while the container shrinks, which can strand an edge-of-map pin
    // outside the small one.
    if (stepEl.classList.contains('is-map-expanded')) {
      setExpanded(false);
      map.jumpTo({ center: [lng, lat], zoom: map.getZoom() });
    }
  });
  // The container is measured on creation; inside a step that was just built it is still
  // zero-height, so the canvas comes back empty without this.
  setTimeout(() => map.resize(), 0);

  const readCoords = () => {
    const lat = Number.parseFloat(latField.value);
    const lon = Number.parseFloat(lonField.value);
    // ±90 / ±180; the firmware refuses anything outside and there is no reason to send it.
    if (Number.isFinite(lat) && Number.isFinite(lon) && Math.abs(lat) <= 90 && Math.abs(lon) <= 180) {
      setPoint(lat, lon, true);
    }
  };
  latField.addEventListener('change', readCoords);
  lonField.addEventListener('change', readCoords);

  const actions = document.createElement('div');
  actions.className = 'actions';
  const next = document.createElement('button');
  next.type = 'button';
  next.className = 'btn btn-primary';
  // A new device is joining the mesh, and this is the last screen before it does; an
  // Upgrade is mid-flow and 'Continue' is still what happens.
  next.textContent = isUpgrade ? 'Continue' : 'Join the mesh';
  // Both are required for MeshCore operation, so an empty one is never "leave it alone".
  // The password counts as set when its placeholder is still untouched on an Upgrade.
  const invalidField = () => {
    if (!draft.name.trim()) return [nameField, `Give this ${noun} a name.`];
    // The position is what selects the regional settings: no pin, no zone, no radio
    // defaults to apply. The map is the field here, so the message points at it.
    if (draft.latitude == null || draft.longitude == null) {
      return [mapExpand, 'Click the map to set a location — it selects the regional settings.'];
    }
    if (!passwordUntouched && !draft.adminPassword.trim()) {
      return [passwordField, 'Set an admin password.'];
    }
    return null;
  };

  // Guidance, not an error: the button is disabled while this is showing, so nothing has
  // gone wrong yet. Quiet enough to read as a caption rather than a scolding.
  const validationNote = document.createElement('p');
  validationNote.className = 'field-note is-quiet';
  validationNote.hidden = true;
  foot.append(validationNote);

  // A disabled Continue with no reason beside it is the confusing case; the note names
  // whatever is still outstanding, and both come from the same check.
  refreshContinue = () => {
    const problem = invalidField();
    next.disabled = Boolean(problem);
    validationNote.hidden = !problem;
    if (problem) validationNote.textContent = problem[1];
  };
  refreshContinue();

  next.addEventListener('click', async () => {
    if (invalidField()) return;
    // The placeholder is not a password — send nothing rather than the bullets.
    await step.apply(flow, { ...draft, adminPassword: passwordUntouched ? '' : draft.adminPassword });
    advance();
  });
  actions.append(next);
  foot.append(actions);

  // Tab order is name -> map -> height -> email -> password -> Continue, which the DOM
  // already gives; everything Leaflet adds inside the map is taken back out of it.
  for (const skipped of form.querySelectorAll(
    '.maplibregl-ctrl button, #map-expand, #lat, #lon'
  )) {
    skipped.tabIndex = -1;
  }
  nameField.focus();

  // An Upgrade keeps the identity the node already has. Mining a new one here would only
  // produce a key that `set prv.key` then writes over a working node's address. The note
  // under the name field already says so, where the question is actually provoked.
  if (isUpgrade) {
    identityLine.hidden = true;
  } else if (draft.identity) {
    showIdentity(draft.identity.prefix, draft.identityStatus);
  } else {
    resolveIdentity((message) => { identityLine.textContent = message; }).then((result) => {
      draft.identity = result.identity;
      draft.identityStatus = result.status;
      showIdentity(extractPrefix(result.identity.publicKeyHex), result.status);
    });
  }
}


// The shipped flasher's closing screen: the board's own picture with the checkmark laid
// over it. Both images and every box style already live in the shared sheet.
function renderDone(step) {
  const s = flow.state;
  const { body, foot } = frame({ title: text(step.title), desc: null, centred: true });

  const success = document.createElement('div');
  success.className = 'success-box';
  success.textContent = s.result?.dryRun
    ? 'Dry run complete — nothing was written.'
    : 'Firmware written successfully.';
  body.append(success);

  const art = s.deviceIcon;
  if (art) {
    const stack = document.createElement('div');
    stack.className = 'done-icon-stack';

    const board = document.createElement('img');
    board.className = 'flashing-icon';
    board.src = art;
    board.alt = '';
    // A missing board image would leave the checkmark floating on its own.
    board.addEventListener('error', () => stack.remove());

    const tick = document.createElement('img');
    tick.className = 'done-icon-checkmark';
    tick.src = `${flow.manifestBase}icons/checkmark.png`;
    tick.alt = '';
    tick.addEventListener('error', () => tick.remove());

    stack.append(board, tick);
    body.append(stack);
  }

  // The write succeeded either way -- this says only that the settings pass did not, which
  // is the one thing the user has to finish by hand.
  const rejected = s.provision?.results?.filter((result) => !result.ok) ?? [];
  const FINISH_BY_HAND =
    'Connect over usb serial or bluetooth to finish device setup with MeshCore apps.';
  const warning = s.provision?.error
    ? `The firmware is written, but the settings could not be applied (${s.provision.error}). ` +
      FINISH_BY_HAND
    : rejected.length
      ? `The firmware is written, but ${rejected.length} setting(s) were rejected by the device. ` +
        FINISH_BY_HAND
      : null;
  if (warning) {
    const box = document.createElement('div');
    box.className = 'warning-box';
    box.textContent = warning;
    body.append(box);
  }

  const actions = document.createElement('div');
  actions.className = 'actions is-centred';
  const again = document.createElement('button');
  again.type = 'button';
  again.className = 'btn btn-primary';
  again.textContent = 'Flash another device';
  again.addEventListener('click', async () => {
    again.disabled = true;
    // Hand the port back before starting over, or the next run's connect fails as
    // selection-required with the grant still intact.
    await flowApi.disposeFlow(flow);
    flow = flowApi.createFlow({
      manifestBase: flow.manifestBase,
      relayBase: params.get('relay') ?? undefined,
    });
    render();
  });
  actions.append(again);
  foot.append(actions);
}

// `step.apply` validates a picked file, so a wrong image is refused here rather than
// several steps later with the device already in programming mode.
function renderFile(step) {
  const { body, foot } = frame({ title: text(step.title), desc: text(step.desc) });
  const accept = step.accept(flow);

  const zone = document.createElement('div');
  zone.className = 'drop-zone';
  zone.innerHTML =
    `<span class="drop-icon">${icon('upload', 26)}</span>` +
    '<strong>Drag a file here</strong>' +
    `<small>or click to browse — ${accept} for this device.</small>`;

  const input = document.createElement('input');
  input.type = 'file';
  input.accept = accept;
  input.hidden = true;

  const problem = document.createElement('p');
  problem.className = 'field-error';
  problem.hidden = true;

  async function take(picked) {
    if (!picked) return;
    problem.hidden = true;
    zone.classList.add('is-busy');
    try {
      await step.apply(flow, picked);
      advance();
    } catch (error) {
      // A rejected file is the user's to fix, not a failure of the tool — stay put and
      // say why, rather than routing to the error screen.
      zone.classList.remove('is-busy');
      problem.textContent = error.message;
      problem.hidden = false;
      input.value = '';
    }
  }

  zone.addEventListener('click', () => input.click());
  input.addEventListener('change', () => take(input.files?.[0]));
  zone.addEventListener('dragover', (event) => {
    event.preventDefault();
    zone.classList.add('is-over');
  });
  zone.addEventListener('dragleave', () => zone.classList.remove('is-over'));
  zone.addEventListener('drop', (event) => {
    event.preventDefault();
    zone.classList.remove('is-over');
    take(event.dataTransfer?.files?.[0]);
  });

  body.append(zone, input);
  foot.append(problem);
}

// --- driver -----------------------------------------------------------------------------

async function render() {
  const step = flowApi.currentStep(flow);
  setProgress();
  syncBack();
  if (!step) return;

  if (step.kind === 'action') return renderAction(step);
  if (step.kind === 'location') return renderLocation(step);
  if (step.kind === 'file') return renderFile(step);

  if (step.kind === 'choice') {
    let options;
    try {
      options = await step.options(flow);
    } catch (error) {
      return renderError(error, () => render());
    }
    return renderChoice(step, options);
  }

  if (step.kind === 'terminal') return renderDone(step);

  // Everything past the binary choice is still the wireframe's job.
  frame({ title: text(step.title), desc: `This step has no interface yet (${step.kind}).`, centred: true });
}

elements.back.addEventListener('click', () => {
  flowApi.goBack(flow);
  render();
});

const params = new URLSearchParams(location.search);

flow = flowApi.createFlow({
  // Relative: `pages/` is the Pages artifact root, so an absolute '/pages/flasher/'
  // addresses nothing there.
  manifestBase: './',
  relayBase: params.get('relay') ?? undefined,
});

render();
loadBuildInfo(flow.manifestBase);

// Exposed so a rig script can render a step without clicking, as wireframe.js does.
window.__ui = { get flow() { return flow; }, api: flowApi, render };
