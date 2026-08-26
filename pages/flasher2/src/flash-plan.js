// The flash plan. Resolvers produce it, engines consume it — the boundary that keeps both
// ESP32 paths on one executor and both manifests on one resolver.

import {
  PARTITION_TABLE_MAX_BYTES,
  PARTITION_TABLE_OFFSET,
  findSecondAppSlot,
  parsePartitionTable,
} from './partitions.js';
import {
  APP_SLOT_INVALIDATE_BYTES,
  STOCK_ESP32_APP_ADDRESS,
  STOCK_ESP32_MERGED_ADDRESS,
  STOCK_IMAGE_BASE,
  STOCK_RELAY_BASE,
} from './constants.js';

/**
 * @typedef {{ data: ArrayBuffer, address: number }} FlashFile
 *
 * @typedef {object} FlashPlan
 * @property {'esptool'|'dfu'} engine
 * @property {FlashFile[]} files        esptool only; empty for dfu
 * @property {Blob|null} package        dfu only
 * @property {Blob|null} erasePackage   dfu only
 * @property {boolean} eraseAll
 * @property {boolean} preserveFs       esp32 custom only
 * @property {{ sha256: string }|null} verify
 * @property {{ commands: string[] }|null} postFlash
 */

export function createFlashPlan(fields) {
  return {
    engine: fields.engine,
    files: fields.files ?? [],
    package: fields.package ?? null,
    // nRF52 has no merged image: upstream wipes the
    // filesystem with a separate erase package flashed ahead of the firmware.
    erasePackage: fields.erasePackage ?? null,
    eraseAll: fields.eraseAll ?? false,
    preserveFs: fields.preserveFs ?? false,
    // Null means no checksum was supplied, which the UI must state rather than skip.
    // Stock firmware often has none; custom firmware always has its sidecar.
    verify: fields.verify ?? null,
    postFlash: fields.postFlash ?? null,
  };
}

// --- the custom manifest ------------------------------------------------------------
// `boards.json` normalises here and is never written back to. Generated from each build's
// real `partitions.bin`, so ESP32-only.

// The tool's own directory, where the build pipeline commits the vendored builds. The dev
// rig overrides it: until cutover those still live under `/pages/flasher/`.
export const CUSTOM_MANIFEST_BASE = './';

export class ManifestError extends Error {
  constructor(message, options) {
    super(message, options);
    this.name = 'ManifestError';
  }
}

export class FirmwareIntegrityError extends Error {
  constructor(message, options) {
    super(message, options);
    this.name = 'FirmwareIntegrityError';
  }
}

function assetUrl(baseUrl, path) {
  return new URL(path, new URL(baseUrl, location.href));
}

async function fetchAsset(baseUrl, path, label) {
  const res = await fetch(assetUrl(baseUrl, path), { cache: 'no-store' });
  if (!res.ok) throw new ManifestError(`Could not load ${label} (${path}): HTTP ${res.status}.`);
  return new Uint8Array(await res.arrayBuffer());
}

// Offsets ship as hex strings ("0x8000"); anything unparsable would silently become an
// address of NaN and write to offset 0.
function requireOffset(raw, boardKey, name) {
  const value = Number(raw?.[name]);
  if (!Number.isInteger(value) || value < 0) {
    throw new ManifestError(`Board '${boardKey}' has no usable '${name}' offset.`);
  }
  return value;
}

function requirePath(value, boardKey, field) {
  if (typeof value !== 'string' || !value) {
    throw new ManifestError(`Board '${boardKey}' has no '${field}'.`);
  }
  return value;
}

function normaliseBoard(key, raw) {
  const offsets = {};
  for (const name of ['bootloader', 'partitions', 'otadata', 'app0', 'app1', 'appMaxSize']) {
    offsets[name] = requireOffset(raw.offsets, key, name);
  }

  const variants = {};
  for (const [variantKey, variant] of Object.entries(raw.variants ?? {})) {
    variants[variantKey] = {
      key: variantKey,
      label: variant.label ?? variantKey,
      version: variant.version ?? null,
      firmwareFile: requirePath(variant.firmwareFile, key, `variants.${variantKey}.firmwareFile`),
      firmwareShaFile: variant.firmwareShaFile ?? null,
      // Chosen by role, not by path — the variant key is the role.
      postFlashCommands: Array.isArray(variant.postFlashCommands)
        ? variant.postFlashCommands.slice()
        : null,
    };
  }

  return {
    key,
    label: raw.label ?? key,
    connectNote: raw.connectNote ?? null,
    postFlashNote: raw.postFlashNote ?? null,
    offsets,
    bootloaderFile: requirePath(raw.bootloaderFile, key, 'bootloaderFile'),
    partitionsFile: requirePath(raw.partitionsFile, key, 'partitionsFile'),
    bootApp0: requirePath(raw.bootApp0, key, 'bootApp0'),
    variants,
  };
}

// Top level is board keys plus `_generated`/`_version` metadata.
const CUSTOM_DISPLAY_FILE = 'board-display.json';

// Same two the shipped flasher ships; a variant with no icon renders without one.
const CUSTOM_VARIANT_ICONS = {
  repeater: 'icons/repeater_variant.png',
  room_server: 'icons/room_variant.png',
};

export async function loadCustomManifest({ baseUrl = CUSTOM_MANIFEST_BASE } = {}) {
  const res = await fetch(assetUrl(baseUrl, 'boards.json'), { cache: 'no-store' });
  if (!res.ok) throw new ManifestError(`Could not load boards.json: HTTP ${res.status}.`);

  let raw;
  try {
    raw = await res.json();
  } catch (error) {
    throw new ManifestError(`boards.json is not valid JSON (${error.message}).`, { cause: error });
  }

  const boards = {};
  for (const [key, board] of Object.entries(raw)) {
    if (key.startsWith('_')) continue;
    boards[key] = normaliseBoard(key, board);
  }
  if (Object.keys(boards).length === 0) throw new ManifestError('boards.json lists no boards.');

  // Optional, falling back to one tile per board. Tiles may share a board, so the display
  // id is not the board key.
  let displays;
  try {
    const shown = await fetch(assetUrl(baseUrl, CUSTOM_DISPLAY_FILE), { cache: 'no-store' });
    if (!shown.ok) throw new ManifestError('no display list');
    displays = Object.entries(await shown.json()).map(([id, entry]) => ({
      id,
      label: entry.label,
      board: entry.board ?? id,
      icon: entry.icon ? assetUrl(baseUrl, entry.icon) : null,
      // Post-flash icon; falls back to `icon` when left blank, as the donor does.
      icon2: assetUrl(baseUrl, entry.icon2 || entry.icon || '') || null,
    }));
  } catch {
    displays = Object.entries(boards).map(([id, board]) => ({
      id, label: board.label, board: id, icon: null, icon2: null,
    }));
  }

  // Keyed by variant id, which is stable across boards, never by label.
  const variantIcons = Object.fromEntries(
    Object.entries(CUSTOM_VARIANT_ICONS).map(([id, path]) => [id, assetUrl(baseUrl, path)])
  );

  return { baseUrl, version: raw._version ?? null, boards, displays, variantIcons };
}

// Body is `<hash>` or `<hash>:<offset>`; the offset is not ours to use. Absent returns
// null so the UI says unverified; a mismatch raises, since bad bytes must never be written.
async function verifyAgainstSidecar(baseUrl, bytes, path) {
  if (!path) return null;
  const res = await fetch(assetUrl(baseUrl, path), { cache: 'no-store' });
  if (!res.ok) return null;

  const expected = (await res.text()).trim().split(':')[0].toLowerCase();
  const digest = await crypto.subtle.digest('SHA-256', bytes);
  const actual = [...new Uint8Array(digest)].map((b) => b.toString(16).padStart(2, '0')).join('');
  if (actual !== expected) {
    throw new FirmwareIntegrityError(
      `${path.replace(/\.sha256$/, '')} failed checksum verification ` +
        `(expected ${expected}, got ${actual}). Nothing has been written.`
    );
  }
  return actual;
}

// Stage one: everything the network can supply, before the device is touched. The planned
// table has to come first, since the scope answer selects the file list.
export async function loadCustomFirmwareSource(manifest, boardKey, variantKey, { onStatus } = {}) {
  const board = manifest.boards[boardKey];
  if (!board) throw new ManifestError(`boards.json has no board '${boardKey}'.`);
  const variant = board.variants[variantKey];
  if (!variant) throw new ManifestError(`Board '${boardKey}' has no variant '${variantKey}'.`);

  onStatus?.('Loading firmware…');
  // Both scopes' assets are fetched here, including the three only a full layout writes:
  // a fetch failing between the evidence read and the write would strand the device.
  const [firmware, partitions, bootloader, bootApp0] = await Promise.all([
    fetchAsset(manifest.baseUrl, variant.firmwareFile, 'the firmware'),
    fetchAsset(manifest.baseUrl, board.partitionsFile, 'the partition table'),
    fetchAsset(manifest.baseUrl, board.bootloaderFile, 'the bootloader'),
    fetchAsset(manifest.baseUrl, board.bootApp0, 'the boot selector'),
  ]);

  onStatus?.('Verifying firmware…');
  const sha256 = await verifyAgainstSidecar(manifest.baseUrl, firmware, variant.firmwareShaFile);

  return {
    board,
    variant,
    plannedPartitions: parsePartitionTable(partitions),
    assets: { firmware, partitions, bootloader, bootApp0 },
    sha256,
  };
}

// Stage two: pure, so nothing can fail once the device is in download mode. Blanking slot
// B's first sector leaves the bootloader exactly one valid image, untouched otadata.
export function buildCustomFlashPlan(source, scope) {
  const { board, variant, assets, sha256 } = source;
  const { offsets } = board;
  const blankSlotB = new Uint8Array(APP_SLOT_INVALIDATE_BYTES).fill(0xff);

  let files;
  if (scope === 'full-layout') {
    files = [
      { data: assets.bootloader, address: offsets.bootloader },
      { data: assets.partitions, address: offsets.partitions },
      { data: assets.bootApp0, address: offsets.otadata },
      { data: assets.firmware, address: offsets.app0 },
      { data: blankSlotB, address: offsets.app1 },
    ];
  } else if (scope === 'app-slots-only') {
    files = [
      { data: assets.firmware, address: offsets.app0 },
      { data: blankSlotB, address: offsets.app1 },
    ];
  } else {
    // Defaulting an unrecognised scope would quietly pick one of two opposite writes.
    throw new Error(`buildCustomFlashPlan received an unknown write scope '${scope}'.`);
  }

  return createFlashPlan({
    engine: 'esptool',
    files,
    eraseAll: scope === 'full-layout',
    preserveFs: true,
    verify: sha256 ? { sha256 } : null,
    postFlash: variant.postFlashCommands ? { commands: variant.postFlashCommands } : null,
  });
}

// --- the stock manifest -------------------------------------------------------------
// Mirrored same-origin by CI, so only firmware bytes need the relay. Upstream is volatile,
// so nothing validates against a count or fixed set; unusable entries are dropped.

export const STOCK_MANIFEST_FILE = 'mc-config.json';
export const STOCK_RELEASES_FILE = 'mc-releases.json';
export const CATALOGUE_OVERRIDES_FILE = 'catalogue-overrides.json';

// An entry either carries `version` directly or a release stream plus filename patterns.
// Both normalise to one version map, and neither manifest resolves a URL without the other.
function releaseVersions(github, releases) {
  const byVersion = {};
  for (const [fileType, pattern] of Object.entries(github.files ?? {})) {
    let match;
    try {
      match = new RegExp(pattern);
    } catch {
      continue; // an unparsable upstream pattern drops its files, never the entry
    }
    for (const stream of releases) {
      if (stream?.type !== github.type) continue;
      for (const file of stream.files ?? []) {
        if (typeof file?.name !== 'string' || !match.test(file.name)) continue;
        const entry = (byVersion[stream.version] ??= { files: [], notes: stream.notes ?? null });
        // `path` is what the relay is asked for; `name` stays the display filename.
        entry.files.push({ type: fileType, name: file.name, path: String(file.url).replace(/^\/+/, '') });
      }
    }
  }
  return byVersion;
}

function normaliseStockFirmware(raw, releases) {
  const byVersion = {};
  const declared = raw?.version;
  if (declared && typeof declared === 'object' && !Array.isArray(declared)) {
    for (const [version, entry] of Object.entries(declared)) {
      const files = (entry?.files ?? [])
        .filter((file) => typeof file?.name === 'string' && typeof file?.type === 'string')
        // These are served from upstream's flat firmware directory, so the name is the path.
        .map((file) => ({ type: file.type, name: file.name, path: file.name, title: file.title ?? null }));
      if (files.length > 0) byVersion[version] = { files, notes: entry.notes ?? null };
    }
  }
  if (raw?.github?.files) Object.assign(byVersion, releaseVersions(raw.github, releases));

  return {
    class: raw.class ?? null,
    role: raw.role ?? null,
    title: raw.title ?? null,
    notice: raw.notice ?? null,
    // Upstream's own order, newest first. A version with no files is left out of the
    // order so nothing offers it, but the entry itself is kept.
    versionOrder: Object.keys(byVersion).filter((v) => byVersion[v].files.length > 0),
    versions: byVersion,
  };
}

async function loadJson(baseUrl, file) {
  const res = await fetch(assetUrl(baseUrl, file), { cache: 'no-store' });
  if (!res.ok) throw new ManifestError(`Could not load ${file}: HTTP ${res.status}.`);
  try {
    return await res.json();
  } catch (error) {
    throw new ManifestError(`${file} is not valid JSON (${error.message}).`, { cause: error });
  }
}

const TOOLTIP_IMG_SRC = /<img\b[^>]*\bsrc=['"]([^'"]+)['"]/i;

/** The `src` of the image upstream hides in a device's tooltip HTML, or null. */
function tooltipImageSrc(tooltip) {
  return typeof tooltip === 'string' ? (TOOLTIP_IMG_SRC.exec(tooltip)?.[1] ?? null) : null;
}

// Manifest asset paths are root-relative and resolve only against upstream's origin. A
// missing file cannot be detected by status — this SPA host answers 200 with index.html.
function absoluteStockAsset(path) {
  return typeof path === 'string' && path ? new URL(path, STOCK_IMAGE_BASE).href : null;
}

/**
 * Corrections we maintain over upstream's catalogue: maker display names and logos,
 * device artwork, and `hidden`. Absent or unreadable applies nothing — an override file
 * is a nicety and must never cost a user their flash.
 */
async function loadCatalogueOverrides() {
  try {
    const res = await fetch(new URL(`../data/${CATALOGUE_OVERRIDES_FILE}`, import.meta.url),
                            { cache: 'no-store' });
    if (!res.ok) return { base: null, makers: {}, devices: {} };
    const doc = await res.json();
    return {
      // Art is addressed relative to the override file, not to the upstream manifest.
      base: new URL(`../data/${CATALOGUE_OVERRIDES_FILE}`, import.meta.url),
      makers: doc.makers ?? {},
      devices: doc.devices ?? {},
    };
  } catch {
    return { base: null, makers: {}, devices: {} };
  }
}

/** An override's own asset, or null. Left null when there is no file to resolve against. */
function overrideAsset(base, path) {
  return base && path ? new URL(path, base).href : null;
}

// Named rather than free-form CSS: an override file should not be able to put arbitrary
// filter syntax into a style attribute. `brightness(0)` flattens art of any colour to a
// silhouette, which `invert(1)` then turns white.
const IMAGE_FILTERS = {
  invert: 'invert(1)',
  white: 'brightness(0) invert(1)',
  black: 'brightness(0)',
};

/** The CSS for a named filter. An unknown name applies none, as if it were absent. */
function overrideFilter(name) {
  return (typeof name === 'string' && IMAGE_FILTERS[name]) ?? null;
}

export async function loadStockManifest({ baseUrl = CUSTOM_MANIFEST_BASE } = {}) {
  const [raw, releases, overrides] = await Promise.all([
    loadJson(baseUrl, STOCK_MANIFEST_FILE),
    loadJson(baseUrl, STOCK_RELEASES_FILE),
    loadCatalogueOverrides(),
  ]);
  if (!Array.isArray(releases)) {
    throw new ManifestError(`${STOCK_RELEASES_FILE} is not a list of release streams.`);
  }
  const devices = [];
  for (const device of raw.device ?? []) {
    if (typeof device?.name !== 'string') continue;
    // Keyed on the name, the only identity upstream gives a device. A rename upstream
    // drops the override silently; scripts/check-catalogue-overrides.py reports those.
    const override = overrides.devices[device.name] ?? {};
    if (override.hidden) continue;
    const firmware = (device.firmware ?? []).map((f) => normaliseStockFirmware(f, releases));
    devices.push({
      name: device.name,
      label: override.label ?? device.name,
      maker: device.maker ?? null,
      // 'esp32' | 'nrf52' | 'noflash' — kept as-is so a UI can say why a device is not
      // offered, rather than having it silently vanish from the list.
      type: device.type ?? null,
      icon: absoluteStockAsset(device.icon),
      tooltip: device.tooltip ?? null,
      // The device picture is only ever an <img> buried in `tooltip`; pull the src out so
      // no caller has to inject a third party's HTML to show it.
      image: overrideAsset(overrides.base, override.image)
        ?? absoluteStockAsset(tooltipImageSrc(device.tooltip)),
      imageFilter: overrideFilter(override.filter),
      // Upstream art is dimmed to tame its light backgrounds; `"dim": false` opts art we
      // have already corrected out of that.
      imageDim: override.dim !== false,
      // The tile's own gradient plate frames a board photo; a logo usually wants none.
      imagePlate: override.plate !== false,
      erase: device.erase ?? null,
      bootloader: device.bootloader ?? null,
      firmware,
    });
  }
  if (devices.length === 0) throw new ManifestError(`${STOCK_MANIFEST_FILE} lists no devices.`);

  // Five maker keys are absent from upstream's catalogue and would render as the raw key.
  // Capitalise the first letter only — title case would turn `LilyGo` into `Lilygo`.
  const makers = { ...(raw.maker ?? {}) };
  for (const device of devices) {
    const key = device.maker;
    if (!key) continue;
    const override = overrides.makers[key] ?? {};
    makers[key] = { ...(makers[key] ?? {}) };
    // Ours wins outright: upstream's name is what the override exists to correct.
    if (override.name) makers[key].name = override.name;
    makers[key].name ||= key.charAt(0).toUpperCase() + key.slice(1);
    makers[key].icon = overrideAsset(overrides.base, override.icon) ?? null;
    makers[key].iconFilter = overrideFilter(override.filter);
    makers[key].iconDim = override.dim !== false;
    makers[key].iconPlate = override.plate !== false;
  }

  return {
    baseUrl,
    devices,
    notices: raw.notice ?? {},
    roles: raw.role ?? {},
    makers,
    staticPath: raw.staticPath ?? null,
  };
}

// --- OTAFIX bootloader ---------------------------------------------------------------
// 16 nRF52 devices ship a factory bootloader whose OTA DFU is broken or unreliable. Both
// upstream tiers are treated the same: identical roles on non-overlapping devices.
const OTAFIX_NOTICES = new Set(['otafixNeeded', 'otafixRecommended']);

// `get bootloader.ver` answers like "0.9.2-OTAFIX2.3-BP1.4" — only OTAFIX's own numbering
// is compared here, never ours.
const OTAFIX_VERSION_RE = /OTAFIX(\d+(?:\.\d+)*)/i;

/**
 * True when a `get bootloader.ver` answer already carries OTAFIX >= 2.2 — the gate's "skip,
 * already fine" reading. A missing or unparsable answer returns false: "cannot tell" means
 * offer the update, never skip on silence.
 */
export function bootloaderAlreadyCurrent(answer) {
  const match = OTAFIX_VERSION_RE.exec(answer ?? '');
  if (!match) return false;
  const [major, minor = 0] = match[1].split('.').map(Number);
  return major > 2 || (major === 2 && minor >= 2);
}

/**
 * The bootloader UF2 for a device, or null when upstream flags no notice or ships no file
 * (Muzi Works R1 Neo: notice with no bootloader files at all — deliberate null, not a miss).
 * Throws on more than one `.uf2` candidate (Xiao nRF52 WIO's `_ble`/`_ble_sense` pair) rather
 * than guessing, since a wrong pick writes another board's bootloader.
 */
export function resolveBootloaderUpdate(device, entry) {
  if (!OTAFIX_NOTICES.has(entry?.notice)) return null;
  // `.zip` is the DFU route and strands OTAFIX 2.1+ devices in BLE OTA — never write it.
  const files = (device.bootloader ?? []).filter((name) => name.endsWith('.uf2'));
  if (files.length === 0) return null;
  // Settled 2026-08-26: refuse, don't ask. `board` cannot tell the candidates apart —
  // it returns a compile-time constant and upstream ships one build for both variants.
  if (files.length > 1) {
    throw new ManifestError(
      `'${device.name}' ships ${files.length} bootloader files for different board variants ` +
        `(${files.join(', ')}), and nothing the device reports can tell them apart. Writing ` +
        `the wrong one would replace this board's bootloader with another board's, so the ` +
        `bootloader update is not offered here.`
    );
  }
  return { file: files[0], reason: entry.notice };
}

// A role is not unique within a device — eight carry the same role twice — so the entry
// is picked by index into the device's own list, never by role.
function selectStockFile(device, entry, version, wipe) {
  const files = entry.versions[version]?.files;
  if (!files) throw new ManifestError(`'${device.name}' has no version '${version}'.`);

  // ESP32 takes the merged image for a wipe and the bare app for an update, chosen by the
  // user's declaration. nRF52 takes the DFU package; `download` is the manual UF2 route.
  const wanted =
    device.type === 'esp32'
      ? [wipe ? 'flash-wipe' : 'flash-update']
      : ['flash-update', 'flash'];

  for (const type of wanted) {
    const file = files.find((f) => f.type === type);
    if (file) return file;
  }
  throw new ManifestError(
    `'${device.name}' ${version} has no ${wanted.join(' or ')} file (only ` +
      `${files.map((f) => f.type).join(', ')}).`
  );
}

// Stage one, the peer of `loadCustomFirmwareSource`. Bytes come through the relay; there
// is no sidecar, so `verify` stays null and the UI must say the firmware is unverified.
export async function loadStockFirmwareSource(
  manifest,
  { deviceName, firmwareIndex = 0, version, wipe = false },
  { onStatus, relayBase = STOCK_RELAY_BASE } = {}
) {
  const device = manifest.devices.find((d) => d.name === deviceName);
  if (!device) throw new ManifestError(`${STOCK_MANIFEST_FILE} has no device '${deviceName}'.`);
  if (device.type !== 'esp32' && device.type !== 'nrf52') {
    throw new ManifestError(`'${device.name}' is type '${device.type}' and cannot be flashed here.`);
  }
  const entry = device.firmware[firmwareIndex];
  if (!entry) throw new ManifestError(`'${device.name}' has no relay-backed firmware at index ${firmwareIndex}.`);

  const file = selectStockFile(device, entry, version, wipe);

  onStatus?.('Downloading firmware…');
  const res = await fetch(new URL(file.path, relayBase), { cache: 'no-store' });
  if (!res.ok) {
    throw new ManifestError(`Could not download ${file.name} from the relay: HTTP ${res.status}.`);
  }
  const bytes = new Uint8Array(await res.arrayBuffer());

  // Still stage one: a fetch that failed after the erase package was written would leave
  // the device wiped with nothing to boot.
  let eraseBytes = null;
  if (device.type === 'nrf52' && wipe && device.erase) {
    onStatus?.('Downloading the erase package…');
    const eraseRes = await fetch(new URL(device.erase, relayBase), { cache: 'no-store' });
    if (!eraseRes.ok) {
      throw new ManifestError(
        `Could not download ${device.erase} from the relay: HTTP ${eraseRes.status}.`
      );
    }
    eraseBytes = new Uint8Array(await eraseRes.arrayBuffer());
  }

  return { device, entry, version, wipe, file, bytes, eraseBytes };
}

/** Fetches the resolved bootloader UF2's bytes from the relay, for the guided nRF52
 * bootloader step. Null input passes through — there is nothing to fetch. */
export async function loadBootloaderUf2(wanted, { relayBase = STOCK_RELAY_BASE, onStatus } = {}) {
  if (!wanted) return null;
  onStatus?.('Downloading the bootloader update…');
  const res = await fetch(new URL(wanted.file, relayBase), { cache: 'no-store' });
  if (!res.ok) {
    throw new ManifestError(`Could not download ${wanted.file} from the relay: HTTP ${res.status}.`);
  }
  return { ...wanted, bytes: new Uint8Array(await res.arrayBuffer()) };
}

// Stage two. Always one file, and the address follows from which was chosen: the donor
// keeps it in mutable page state, where a wipe then an update writes the app to 0x0.
export function buildStockFlashPlan(source, { partitions = [] } = {}) {
  const { device, bytes, eraseBytes, wipe } = source;

  if (device.type === 'nrf52') {
    return createFlashPlan({
      engine: 'dfu',
      package: new Blob([bytes]),
      // The bootloader is a guided pre-flash step now (nrf52-bootloader-plan.md), never a
      // DFU stage: the .zip route stages every payload in the app region and destroys it.
      // The wipe. `Dfu`'s own `eraseBeforeUpdate` clears the application region the write
      // is about to overwrite anyway, so `eraseAll` stays false and this carries it.
      erasePackage: eraseBytes ? new Blob([eraseBytes]) : null,
      // Filesystem preservation is custom-path-only, and DFU cannot read flash back regardless.
      preserveFs: false,
      verify: null,
      postFlash: null,
    });
  }

  // An update writes app0 only, so otadata may still boot the slot it did not touch.
  // Blanking that slot leaves one valid image; a wipe needs none of this.
  const files = [{ data: bytes, address: wipe ? STOCK_ESP32_MERGED_ADDRESS : STOCK_ESP32_APP_ADDRESS }];
  const slotB = wipe ? null : findSecondAppSlot(partitions);
  if (slotB) {
    files.push({ data: new Uint8Array(APP_SLOT_INVALIDATE_BYTES).fill(0xff), address: slotB.offset });
  }

  return createFlashPlan({
    engine: 'esptool',
    files,
    eraseAll: wipe,
    preserveFs: false,
    verify: null,
    // Post-flash provisioning applies here too, but the role → command-set table does not exist yet and
    // `boards.json`'s commands are ours, not upstream's. Nothing is invented.
    postFlash: null,
  });
}

// --- manual upload -------------------------------------------------------------------
// The third resolver. No manifest, so one stage only: the bytes are already in hand.

export class UnsupportedFirmwareFileError extends Error {
  constructor(message) {
    super(message);
    this.name = 'UnsupportedFirmwareFileError';
  }
}

// `protocol`. ESP-IDF image header magic, first byte of any ESP32 image.
const ESP_IMAGE_MAGIC = 0xe9;

// A merged image carries a partition table at 0x8000 and an app image does not. Both start
// with 0xE9, and size does not separate them — merged has measured *smaller*.
function looksMerged(bytes) {
  if (bytes.length < PARTITION_TABLE_OFFSET + 32) return false;
  try {
    const table = parsePartitionTable(
      bytes.subarray(PARTITION_TABLE_OFFSET, PARTITION_TABLE_OFFSET + PARTITION_TABLE_MAX_BYTES)
    );
    return table.length > 0;
  } catch {
    return false;
  }
}

/**
 * Builds a plan from a user-supplied file. `family` comes from the connected device,
 * never from the file — the file is only checked against it.
 * @param {{ name: string, bytes: Uint8Array, blob: Blob }} file
 */
export function buildManualFlashPlan(file, { family, wipe = false }) {
  if (family === 'nrf52') {
    if (/\.uf2$/i.test(file.name)) {
      throw new UnsupportedFirmwareFileError(
        `${file.name} is a UF2 file. Double-tap reset and copy it to the drive the board ` +
          `presents — it is not flashed over serial.`
      );
    }
    if (!/\.zip$/i.test(file.name)) {
      throw new UnsupportedFirmwareFileError(`${file.name} is not a DFU package (.zip).`);
    }
    return createFlashPlan({ engine: 'dfu', package: file.blob, verify: null, postFlash: null });
  }

  if (file.bytes[0] !== ESP_IMAGE_MAGIC) {
    throw new UnsupportedFirmwareFileError(
      `${file.name} is not an ESP32 firmware image (expected magic 0xE9).`
    );
  }
  const merged = looksMerged(file.bytes);
  // A full erase takes the bootloader and an app image supplies none, so the pair can only
  // produce a device that will not boot. Refused, not downgraded: the declaration is theirs.
  if (wipe && !merged) {
    throw new UnsupportedFirmwareFileError(
      `${file.name} is an application image, not a full one. Erasing first would remove the ` +
        `bootloader it needs, leaving the device unable to boot — choose Upgrade Existing ` +
        `to write it, or supply a merged image.`
    );
  }
  return createFlashPlan({
    engine: 'esptool',
    files: [{ data: file.bytes, address: merged ? STOCK_ESP32_MERGED_ADDRESS : STOCK_ESP32_APP_ADDRESS }],
    eraseAll: wipe,
    // No sidecar and no manifest digest: the UI must say the firmware is unverified.
    verify: null,
    postFlash: null,
  });
}

/** Reads a picked file into the shape `buildManualFlashPlan` takes. */
export async function readUploadedFirmware(file) {
  return { name: file.name, bytes: new Uint8Array(await file.arrayBuffer()), blob: file };
}

/** The nRF52 package must parse before hardware is touched, not mid-transfer. */
/**
 * Nordic legacy DFU update modes. `dfu.js` defines only `application`; the transport
 * underneath takes all four. Unverified on hardware beyond `application`.
 */
export const DFU_UPDATE_MODES = {
  softdevice: 1,
  bootloader: 2,
  softdevice_bootloader: 3,
  application: 4,
};

// Ordered so a combined package is recognised before either half of it.
const DFU_SECTION_ORDER = ['softdevice_bootloader', 'application', 'bootloader', 'softdevice'];

/**
 * Reads a DFU package and returns its one section. Parsed here because `dfuUpdate` cannot
 * read a `softdevice_bootloader` package; `withBytes` adds the payloads the executor needs.
 */
export async function validateDfuPackage(blob, { withBytes = false } = {}) {
  const zip = await import('../vendor/dfu/zip.min.js');
  const reader = new zip.ZipReader(new zip.BlobReader(blob));
  try {
    const entries = await reader.getEntries();
    const names = entries.map((e) => new TextDecoder().decode(e.rawFilename));
    const manifestEntry = entries[names.indexOf('manifest.json')];
    if (!manifestEntry) throw new UnsupportedFirmwareFileError('The package has no manifest.json.');

    const manifest = JSON.parse(await manifestEntry.getData(new zip.TextWriter()))?.manifest ?? {};
    const kind = DFU_SECTION_ORDER.find((name) => manifest[name]);
    if (!kind) {
      throw new UnsupportedFirmwareFileError(
        `The package names no update this tool understands (${Object.keys(manifest).join(', ')}).`
      );
    }

    const section = manifest[kind];
    if (!names.includes(section?.bin_file) || !names.includes(section?.dat_file)) {
      throw new UnsupportedFirmwareFileError('The package is missing the files its manifest names.');
    }

    // Sizes come from the manifest, not the payload: a combined package is one binary
    // whose halves are only distinguishable by the counts declared here.
    const result = {
      kind,
      mode: DFU_UPDATE_MODES[kind],
      binFile: section.bin_file,
      datFile: section.dat_file,
      softdeviceSize: section.sd_size ?? 0,
      bootloaderSize: section.bl_size ?? 0,
      applicationSize: kind === 'application' ? null : 0,
    };
    if (!withBytes) return result;

    result.bin = await entries[names.indexOf(section.bin_file)].getData(new zip.Uint8ArrayWriter());
    result.dat = await entries[names.indexOf(section.dat_file)].getData(new zip.Uint8ArrayWriter());
    // An application package declares no size; the payload is the whole of it.
    if (kind === 'application') result.applicationSize = result.bin.length;
    return result;
  } catch (error) {
    if (error instanceof UnsupportedFirmwareFileError) throw error;
    throw new UnsupportedFirmwareFileError(`The package could not be read (${error.message}).`);
  } finally {
    await reader.close().catch(() => {});
  }
}
