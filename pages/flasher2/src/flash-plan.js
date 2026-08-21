// The flash plan — §4.1. Every path resolves to one of these before hardware is touched.
// Resolvers produce it, engines consume it: the boundary that keeps the custom and stock
// ESP32 paths on one executor (C3) and the two manifests on one resolver (C6).

import { parsePartitionTable } from './partitions.js';
import {
  STOCK_ESP32_APP_ADDRESS,
  STOCK_ESP32_MERGED_ADDRESS,
  STOCK_RELAY_BASE,
} from './constants.js';

/**
 * @typedef {{ data: ArrayBuffer, address: number }} FlashFile
 *
 * @typedef {object} FlashPlan
 * @property {'esptool'|'dfu'} engine
 * @property {FlashFile[]} files        esptool only; empty for dfu
 * @property {Blob|null} package        dfu only
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
    eraseAll: fields.eraseAll ?? false,
    preserveFs: fields.preserveFs ?? false,
    // Null means "no checksum was supplied", which the UI must state rather than
    // silently skip: stock firmware arrives through the relay without one unless
    // the manifest carries it, custom firmware always has its sidecar (C7).
    verify: fields.verify ?? null,
    postFlash: fields.postFlash ?? null,
  };
}

// --- §8: the custom manifest -------------------------------------------------------
// `auto_boards.json` normalises here and is never written back to (C6). It is generated from
// each build's real `partitions.bin`, so it is ESP32-only; the stock resolver (§9) is the
// peer that feeds the nRF52 engine.

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
      // Chosen by role, not by path (§10.7) — the variant key is the role.
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
export async function loadCustomManifest({ baseUrl = CUSTOM_MANIFEST_BASE } = {}) {
  const res = await fetch(assetUrl(baseUrl, 'auto_boards.json'), { cache: 'no-store' });
  if (!res.ok) throw new ManifestError(`Could not load auto_boards.json: HTTP ${res.status}.`);

  let raw;
  try {
    raw = await res.json();
  } catch (error) {
    throw new ManifestError(`auto_boards.json is not valid JSON (${error.message}).`, { cause: error });
  }

  const boards = {};
  for (const [key, board] of Object.entries(raw)) {
    if (key.startsWith('_')) continue;
    boards[key] = normaliseBoard(key, board);
  }
  if (Object.keys(boards).length === 0) throw new ManifestError('auto_boards.json lists no boards.');

  return { baseUrl, version: raw._version ?? null, boards };
}

// Sidecar body is `<hash>` or `<hash>:<offset>`; the offset is not this tool's to use.
// An absent sidecar returns null so the UI states the firmware is unverified (C7); a
// mismatch raises, because bad bytes must never reach the device.
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

// Stage one of two: everything the network can supply, before the device is touched.
// `plannedPartitions` is what §10.5's evidence read compares against, and its scope
// answer is what selects the file list — so the table has to come first.
export async function loadCustomFirmwareSource(manifest, boardKey, variantKey, { onStatus } = {}) {
  const board = manifest.boards[boardKey];
  if (!board) throw new ManifestError(`auto_boards.json has no board '${boardKey}'.`);
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

// Stage two: pure, so no failure is possible once the device is in download mode.
// Slot B always takes the uniform 0xFF buffer, leaving the bootloader exactly one valid
// image without otadata being touched (§10.5).
export function buildCustomFlashPlan(source, scope) {
  const { board, variant, assets, sha256 } = source;
  const { offsets } = board;
  const blankSlotB = new Uint8Array(offsets.appMaxSize).fill(0xff);

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

// --- §9: the stock manifest ---------------------------------------------------------
// `mc_config.json` is mirrored same-origin by CI, so only the firmware bytes cross an
// origin and need the relay. It normalises here and is never written back to (C6).
// Upstream is volatile — device and role counts have moved 57 → 64 in a fortnight — so
// nothing here validates against a count or a fixed set; unusable entries are dropped.

export const STOCK_MANIFEST_FILE = 'mc_config.json';
export const STOCK_RELEASES_FILE = 'mc_releases.json';

// A firmware entry names its files one of two ways, and both end up here as the same
// version map. Either it carries `version` directly, or it carries a release stream plus
// filename patterns — the versions, build hashes and real names for those live only in
// the releases manifest, so neither file resolves a URL without the other.
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
    // Upstream's own order, newest first; the keys are version strings, not a list.
    // A version with no files is one this board had no build in — it is left out of the
    // order so nothing offers it, but the entry itself is always kept.
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

export async function loadStockManifest({ baseUrl = CUSTOM_MANIFEST_BASE } = {}) {
  const [raw, releases] = await Promise.all([
    loadJson(baseUrl, STOCK_MANIFEST_FILE),
    loadJson(baseUrl, STOCK_RELEASES_FILE),
  ]);
  if (!Array.isArray(releases)) {
    throw new ManifestError(`${STOCK_RELEASES_FILE} is not a list of release streams.`);
  }

  const devices = [];
  for (const device of raw.device ?? []) {
    if (typeof device?.name !== 'string') continue;
    const firmware = (device.firmware ?? []).map((f) => normaliseStockFirmware(f, releases));
    devices.push({
      name: device.name,
      maker: device.maker ?? null,
      // 'esp32' | 'nrf52' | 'noflash' — kept as-is so a UI can say why a device is not
      // offered, rather than having it silently vanish from the list.
      type: device.type ?? null,
      icon: device.icon ?? null,
      tooltip: device.tooltip ?? null,
      erase: device.erase ?? null,
      bootloader: device.bootloader ?? null,
      firmware,
    });
  }
  if (devices.length === 0) throw new ManifestError(`${STOCK_MANIFEST_FILE} lists no devices.`);

  return { baseUrl, devices };
}

// Upstream device names are unique, but a role is not unique within a device — eight
// devices carry the same role twice under different classes — so the entry is picked by
// index into the device's own relay-backed list, not by role.
function selectStockFile(device, entry, version, wipe) {
  const files = entry.versions[version]?.files;
  if (!files) throw new ManifestError(`'${device.name}' has no version '${version}'.`);

  // ESP32: the merged image for a wipe, the bare app image for an update. Which one is
  // the user's New/Update declaration, never the device's state (workflow Step 2).
  // nRF52: the DFU package. `download` is the manual UF2 route and is never flashed here.
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
// is no sidecar, so `verify` stays null and the UI must say the firmware is unverified (C7).
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

  return { device, entry, version, wipe, file, bytes };
}

// Stage two. One file, always: upstream's merged image already carries the bootloader and
// partition table, and its update image is the app alone. The address follows from the
// file that was chosen — the donor keeps it in mutable page state, where a wipe followed
// by an update writes the app image to 0x0 (§12.3-class defect; do not reproduce).
export function buildStockFlashPlan(source) {
  const { device, bytes, wipe } = source;

  if (device.type === 'nrf52') {
    return createFlashPlan({
      engine: 'dfu',
      package: new Blob([bytes]),
      // §10.6 is custom-path-only (C5), and DFU cannot read flash back regardless.
      preserveFs: false,
      verify: null,
      postFlash: null,
    });
  }

  return createFlashPlan({
    engine: 'esptool',
    files: [{ data: bytes, address: wipe ? STOCK_ESP32_MERGED_ADDRESS : STOCK_ESP32_APP_ADDRESS }],
    eraseAll: wipe,
    preserveFs: false,
    verify: null,
    // §10.7 applies here too, but the role → command-set table does not exist yet and
    // `auto_boards.json`'s commands are ours, not upstream's. Nothing is invented.
    postFlash: null,
  });
}
