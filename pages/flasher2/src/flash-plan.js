// The flash plan — §4.1. Every path resolves to one of these before hardware is touched.
// Resolvers produce it, engines consume it: the boundary that keeps the custom and stock
// ESP32 paths on one executor (C3) and the two manifests on one resolver (C6).

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
 * @property {Blob|null} erasePackage   dfu only; flashed after the bootloader when present
 * @property {Blob|null} bootloaderPackage  dfu only; §9.4 OTAFIX, flashed before everything
 * @property {string|null} bootloaderReason which upstream notice triggered it
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
    // Deviation from §4.1's shape. nRF52 has no merged image: upstream wipes the
    // filesystem with a separate erase package flashed ahead of the firmware.
    erasePackage: fields.erasePackage ?? null,
    // dfu only; §9.4's OTAFIX bootloader, flashed ahead of everything else when upstream
    // flags the device. Null on every other path.
    bootloaderPackage: fields.bootloaderPackage ?? null,
    bootloaderReason: fields.bootloaderReason ?? null,
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
const CUSTOM_DISPLAY_FILE = 'board-display.json';

// Same two the shipped flasher ships; a variant with no icon renders without one.
const CUSTOM_VARIANT_ICONS = {
  repeater: 'icons/repeater_variant.png',
  room_server: 'icons/room_variant.png',
};

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

  // Display list is optional and falls back to one tile per board, matching the shipped
  // flasher. Tiles may share a board -- `grumpy_board` and `xiao_c3` are one image apiece
  // over the same firmware -- so the display id is not the board key.
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
// Slot B's first sector is blanked, which invalidates its image header and leaves the
// bootloader exactly one valid image without otadata being touched (§10.5).
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
      icon: absoluteStockAsset(device.icon),
      tooltip: device.tooltip ?? null,
      // The device picture is only ever an <img> buried in `tooltip`; pull the src out so
      // no caller has to inject a third party's HTML to show it.
      image: absoluteStockAsset(tooltipImageSrc(device.tooltip)),
      erase: device.erase ?? null,
      bootloader: device.bootloader ?? null,
      firmware,
    });
  }
  if (devices.length === 0) throw new ManifestError(`${STOCK_MANIFEST_FILE} lists no devices.`);

  // Four catalogues sit beside `device` and were previously dropped: `notice` keys the
  // warning text a firmware entry names, `role` and `maker` carry upstream's own display
  // names, and `staticPath` is where its web pages link bootloader files (not our path —
  // the relay serves them from the same flat directory as everything else).
  // Five maker keys (uniteng, gat-iot, Ikoka, keepteen, muziworks) are absent from the
  // catalogue and would render as the raw key. Capitalise the first letter and nothing
  // else — title case would turn the catalogue's own `LilyGo` into `Lilygo`.
  const makers = { ...(raw.maker ?? {}) };
  for (const device of devices) {
    const key = device.maker;
    if (!key) continue;
    makers[key] = { ...(makers[key] ?? {}) };
    makers[key].name ||= key.charAt(0).toUpperCase() + key.slice(1);
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

// --- OTAFIX bootloader (§9.4) -------------------------------------------------------
// 16 nRF52 devices ship a factory bootloader whose OTA DFU is broken or unreliable, and
// upstream flags the affected entries by notice. Both tiers are treated the same: they
// carry identical roles (repeater/roomServer only) on non-overlapping devices, and
// upstream renders both in the same container, so the distinction is invisible anyway.
const OTAFIX_NOTICES = new Set(['otafixNeeded', 'otafixRecommended']);

/** The bootloader DFU package for a device, or null when upstream does not flag one. */
export function resolveBootloaderUpdate(device, entry) {
  if (!OTAFIX_NOTICES.has(entry?.notice)) return null;
  // `.uf2` is §11.5's drag-and-drop route and cannot be written over serial.
  const file = (device.bootloader ?? []).find((name) => name.endsWith('.zip'));
  return file ? { file, reason: entry.notice } : null;
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

  // Fetched here for the same reason as the erase package: failing after the bootloader
  // was written would leave a device with no application and nothing to write back.
  let bootloader = null;
  const wanted = resolveBootloaderUpdate(device, entry);
  if (wanted) {
    onStatus?.('Downloading the bootloader update…');
    const blRes = await fetch(new URL(wanted.file, relayBase), { cache: 'no-store' });
    if (!blRes.ok) {
      throw new ManifestError(
        `Could not download ${wanted.file} from the relay: HTTP ${blRes.status}.`
      );
    }
    bootloader = { ...wanted, bytes: new Uint8Array(await blRes.arrayBuffer()) };
  }

  return { device, entry, version, wipe, file, bytes, eraseBytes, bootloader };
}

// Stage two. One file, always: upstream's merged image already carries the bootloader and
// partition table, and its update image is the app alone. The address follows from the
// file that was chosen — the donor keeps it in mutable page state, where a wipe followed
// by an update writes the app image to 0x0 (§12.3-class defect; do not reproduce).
export function buildStockFlashPlan(source, { partitions = [] } = {}) {
  const { device, bytes, eraseBytes, wipe, bootloader } = source;

  if (device.type === 'nrf52') {
    return createFlashPlan({
      engine: 'dfu',
      package: new Blob([bytes]),
      // Written first: it erases the application and leaves the device in DFU, which is
      // where the stages after it need to start. A failure there has cost nothing yet.
      bootloaderPackage: bootloader ? new Blob([bootloader.bytes]) : null,
      bootloaderReason: bootloader?.reason ?? null,
      // The wipe. `Dfu`'s own `eraseBeforeUpdate` clears the application region the write
      // is about to overwrite anyway, so `eraseAll` stays false and this carries it.
      erasePackage: eraseBytes ? new Blob([eraseBytes]) : null,
      // §10.6 is custom-path-only (C5), and DFU cannot read flash back regardless.
      preserveFs: false,
      verify: null,
      postFlash: null,
    });
  }

  // An update writes app0 only, so otadata may still prefer the slot it did not touch and
  // boot the firmware that was just replaced. Blanking that slot leaves the bootloader one
  // valid image. A wipe erases everything and needs none of this.
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
    // §10.7 applies here too, but the role → command-set table does not exist yet and
    // `auto_boards.json`'s commands are ours, not upstream's. Nothing is invented.
    postFlash: null,
  });
}

// --- §9A: manual upload ------------------------------------------------------------
// The third resolver, beside the other two (C6). No manifest, so there is only one
// stage: the bytes are already in hand and nothing is fetched.

export class UnsupportedFirmwareFileError extends Error {
  constructor(message) {
    super(message);
    this.name = 'UnsupportedFirmwareFileError';
  }
}

// `protocol`. ESP-IDF image header magic, first byte of any ESP32 image.
const ESP_IMAGE_MAGIC = 0xe9;

// A merged image carries a partition table at 0x8000 and an application image does not.
// Both start with 0xE9 and size does not separate them — an upstream merged image measured
// smaller than the application image for the same board.
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
 * §9A. Builds a plan from a user-supplied file. `family` comes from the connected device
 * (§10.2), never from the file — the file is only checked against it.
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
  return createFlashPlan({
    engine: 'esptool',
    files: [{ data: file.bytes, address: merged ? STOCK_ESP32_MERGED_ADDRESS : STOCK_ESP32_APP_ADDRESS }],
    eraseAll: wipe,
    // No sidecar and no manifest digest: the UI must say the firmware is unverified (C7).
    verify: null,
    postFlash: null,
  });
}

/** Reads a picked file into the shape `buildManualFlashPlan` takes. */
export async function readUploadedFirmware(file) {
  return { name: file.name, bytes: new Uint8Array(await file.arrayBuffer()), blob: file };
}

/** The nRF52 package must parse before hardware is touched, not mid-transfer (§9A). */
/**
 * Nordic legacy DFU update modes. `dfu.js` defines only the application one, because its
 * own zip reader only ever produces application packages — the transport underneath takes
 * all four. Sourced from the protocol, `protocol`-tagged, and unverified on hardware for
 * anything but `application`.
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
 * Reads a DFU package and returns the one section it carries, whichever kind that is.
 * Bootloader packages name `softdevice_bootloader`, which `dfu.js` cannot read — this is
 * why the tool parses the zip itself rather than calling `dfuUpdate`.
 *
 * `withBytes` also extracts the payloads, which the executor needs and a validity check
 * does not.
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
