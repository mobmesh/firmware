// The flash plan — §4.1. Every path resolves to one of these before hardware is touched.
// Resolvers produce it, engines consume it: the boundary that keeps the custom and stock
// ESP32 paths on one executor (C3) and the two manifests on one resolver (C6).

import { parsePartitionTable } from './partitions.js';

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
// `boards.json` normalises here and is never written back to (C6). It is generated from
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
