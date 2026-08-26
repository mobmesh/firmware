// The step order, made executable: every delivery path sequenced into the decisions a user
// actually makes. DOM-free, so the order outlives whatever renders it.

import { compatibilityCopy, detectBrowserKind, detectSerialSupport, fileSystemAccessCopy } from './capability.js';
import { acquireUsableSerialPort, closeSerialPortQuietly, deviceFamily } from './serial-port.js';
import * as esp32 from './esp32.js';
import * as esptool from './esptool.js';
import * as nrf52 from './nrf52.js';
import * as plans from './flash-plan.js';
import { partitionTablesMatch } from './partitions.js';
import { buildProvisionCommands, provisionDevice } from './provision.js';
import { readNodeConfig } from './cli-session.js';
import { CLI_BAUD_RATE, STOCK_RELAY_BASE } from './constants.js';

/** Which half of the mesh a node is for. Chosen early so the role list stays short. */
export const USAGE = { INFRASTRUCTURE: 'infrastructure', CLIENT: 'client' };

// Every role upstream ships, bucketed by what the node is for rather than what a board can
// run — only 7 exist and they are near-universal, so the split is stable.
const ROLE_USAGE = {
  repeater: USAGE.INFRASTRUCTURE,
  roomServer: USAGE.INFRASTRUCTURE,
  room_server: USAGE.INFRASTRUCTURE,
  kissRadio: USAGE.INFRASTRUCTURE,
  companionBle: USAGE.CLIENT,
  companionUsb: USAGE.CLIENT,
  gui: USAGE.CLIENT,
  guiSD: USAGE.CLIENT,
};

// Glyph names, resolved by the renderer. Upstream ships no role artwork, unlike the
// custom path, which has its own PNGs.
const ROLE_ICONS = {
  repeater: 'repeater',
  roomServer: 'roomServer',
  kissRadio: 'kissRadio',
  companionBle: 'companionBle',
  companionUsb: 'companionUsb',
  gui: 'gui',
  guiSD: 'guiSD',
};

// Only these two carry a name, a position and a registry identity. A companion is
// configured from the phone app, and a KISS modem has no node identity at all.
const LOCATION_ROLES = new Set(['repeater', 'roomServer', 'room_server']);

/** The role key chosen for either source, or null when none has been picked yet. */
function selectedRole(flow) {
  const s = flow.state;
  if (s.source === SOURCE.ENHANCED) return s.variantKey ?? null;
  if (s.source === SOURCE.STOCK && s.firmwareIndex != null) {
    return selectedStockDevice(flow)?.firmware[s.firmwareIndex]?.role ?? null;
  }
  return null;
}

/** Display name for the chosen role, for copy that names it. Null when none applies. */
export function selectedRoleName(flow) {
  const role = selectedRole(flow);
  if (role === 'repeater') return 'Repeater';
  if (role === 'roomServer' || role === 'room_server') return 'Room Server';
  return null;
}

/** Unknown roles are shown rather than hidden — a vanished option is a worse failure. */
function matchesUsage(role, usage) {
  const bucket = ROLE_USAGE[role];
  return !usage || !bucket || bucket === usage;
}

// A device the role step would offer nothing for: same two conditions that step filters
// on, so listing a device here guarantees it has at least one role to pick.
function servesUsage(device, usage) {
  return device.firmware.some(
    (entry) => entry.versionOrder.length > 0 && matchesUsage(entry.role, usage)
  );
}

export const SOURCE = { ENHANCED: 'enhanced', STOCK: 'stock', MANUAL: 'manual' };
export const INSTALL = { NEW: 'new', UPDATE: 'update' };

/** The device is unreachable or the browser cannot talk to it; the flow cannot start. */
export class FlowBlockedError extends Error {
  constructor(message) {
    super(message);
    this.name = 'FlowBlockedError';
  }
}

export function createFlow({
  dryRun = false,
  family = null,
  manifestBase = plans.CUSTOM_MANIFEST_BASE,
  relayBase = STOCK_RELAY_BASE,
} = {}) {
  return {
    dryRun,
    manifestBase,
    relayBase,
    stepIndex: 0,
    state: {
      port: null,
      session: null,
      // Step 1 reads this off the PID. A dry run has no device, so the harness supplies it.
      family,
      mode: null,
      // `get bootloader.ver`'s raw answer, read at arm before any DFU transition. Null
      // means no answer, not "no bootloader" — the OTAFIX gate offers on silence.
      // { name, latitude, longitude } read at arm while the app still answered, or null.
      // Pre-fills the Upgrade path so a write does not silently replace what is there.
      existingConfig: null,
      bootloaderVersion: null,
      bootloaderUpdated: null,
      // Cached between a FilePickerRequiredError and the checkpoint's retry, so the UF2
      // isn't re-fetched on every re-entry.
      pendingBootloaderUf2: null,
      devicePartitions: null,
      plannedPartitions: null,
      evidence: null,
      install: null,
      usage: null,
      source: null,
      maker: null,
      deviceName: null,
      boardKey: null,
      boardDisplayKey: null,
      // Board artwork for the writing screen; icon2 is the post-flash reset image.
      deviceIcon: null,
      deviceIcon2: null,
      variantKey: null,
      firmwareIndex: null,
      version: null,
      file: null,
      // Baseline node settings gathered before the write; nothing sends them yet.
      nodeName: '',
      latitude: null,
      longitude: null,
      // Collected on the node-config step. Height and email feed the registry reservation,
      // which is never called; the password is baseline config, still unscripted.
      heightFt: '',
      email: '',
      adminPassword: '',
      // { privateKeyHex, publicKeyHex, prefix } — generated here, never transmitted.
      identity: null,
      identityStatus: null,
      customManifest: null,
      stockManifest: null,
      plan: null,
      planNotes: [],
      result: null,
    },
  };
}

// --- manifest access, loaded once per flow -------------------------------------------

async function customManifest(flow) {
  flow.state.customManifest ??= await plans.loadCustomManifest({ baseUrl: flow.manifestBase });
  return flow.state.customManifest;
}

async function stockManifest(flow) {
  flow.state.stockManifest ??= await plans.loadStockManifest({ baseUrl: flow.manifestBase });
  return flow.state.stockManifest;
}

function selectedStockDevice(flow) {
  const { stockManifest: manifest, deviceName } = flow.state;
  return manifest?.devices.find((device) => device.name === deviceName) ?? null;
}

/** The device and firmware entry the bootloader gate reads, or null before both are chosen. */
function bootloaderTarget(flow) {
  const device = selectedStockDevice(flow);
  const entry = device?.firmware?.[flow.state.firmwareIndex] ?? null;
  return device && entry ? { device, entry } : null;
}

// Connect and arm share one heading on purpose: the device is armed while the user is
// still reading the same screen, and swapping words there reads as a flicker.
const CONNECT_HEAD = {
  title: 'Plug in your device',
  desc: 'Connect your device to this computer via USB to get started.',
};

// --- step 1: connect and arm ----------------------------------------------------------

/**
 * Read what the node is already set to, while the application still answers. This is the
 * only window: the ESP32 enters download mode moments later and DFU serves no CLI at all,
 * so a value not captured here cannot be recovered before the write.
 *
 * Never fails the flow — a silent device (a T1, a factory-fresh board, Meshtastic) leaves
 * the fields blank, which is what the Upgrade path did for every device before this.
 */
async function readExistingConfig(flow, onStatus) {
  const s = flow.state;
  onStatus('Reading the current settings…');
  try {
    await s.port.open({ baudRate: CLI_BAUD_RATE });
    try {
      s.existingConfig = await readNodeConfig(s.port);
    } finally {
      await closeSerialPortQuietly(s.port);
    }
  } catch (error) {
    // An unopenable port is "cannot tell", the same as silence. The engine's own entry
    // is what reports a device that genuinely cannot be reached.
    console.warn('[flow] Could not read existing settings:', error);
    s.existingConfig = null;
    return;
  }
  const { name, latitude, longitude } = s.existingConfig ?? {};
  onStatus(name ? `current name: ${name}` : 'no existing settings answered');
  if (latitude !== null && longitude !== null) onStatus(`current position: ${latitude}, ${longitude}`);
}

async function armEsp32(flow, onStatus) {
  const s = flow.state;
  const { mode, version } = await esp32.resolveEsp32Mode(s.port);
  s.mode = mode;
  onStatus(`found in ${mode}${version ? ` — ${version}` : ''}`);

  // Before download mode, not after: the CLI is the only source for these and it is about
  // to be gone. Skipped unless the application is actually running.
  if (mode === esp32.ESP32_MODE.APP) await readExistingConfig(flow, onStatus);

  // State 3 included: both probes are passive, so "unknown" is also what a boot loop looks
  // like. Entry is not destructive; the erase decision still runs on evidence downstream.
  if (mode !== esp32.ESP32_MODE.BOOTLOADER) {
    s.port = await esp32.enterDownloadMode(s.port, {
      manualInstruction:
        'This device could not be put into flash mode automatically. Do it by hand: hold ' +
        'the PGM/BOOT button, tap RST, then release — and try again. Nothing has been ' +
        'written or erased.',
      onStatus,
    });
  }

  s.session = await esptool.openEsptoolSession(s.port);
  // Only the cheap table read belongs here. The evidence chain compares against the
  // firmware's own partition table, which is not known until the source step has run.
  s.devicePartitions = await esp32.readPartitionTable(s.session);
  onStatus(`partition table: ${s.devicePartitions.length} entries`);
}

// Was the `review` step's prepare(). The review screen is gone; the plan is still
// built from the same inputs, immediately before the write.
async function buildPlan(flow, onStatus) {
  const s = flow.state;
  const wipe = s.install === INSTALL.NEW;
  s.planNotes = [];

  if (s.source === SOURCE.ENHANCED) {
    const manifest = await customManifest(flow);
    const source = await plans.loadCustomFirmwareSource(manifest, s.boardKey, s.variantKey, { onStatus });
    // The restore is sized against the layout being *written*, not the one read
    // off the device — those differ exactly when the restore has to rebuild.
    s.plannedPartitions = source.plannedPartitions;
    let scope = 'full-layout';
    let reason = 'you chose New, so the whole layout is written';
    if (!wipe) {
      if (flow.dryRun) {
        reason = 'dry run — the real scope needs Step 1\u2019s table read';
      } else if (partitionTablesMatch(s.devicePartitions, source.plannedPartitions)) {
        // The fork is the table comparison alone: a matching layout never touches the
        // filesystem, so a content check could only risk what was asked to be kept.
        scope = 'app-slots-only';
        reason = 'the partition layout already matches, so your settings are left alone';
      } else {
        // The one branch that erases the filesystem, and so the only one that backs it up.
        s.evidence = await esp32.readFlashEvidence(s.session, source.plannedPartitions, { onStatus });
        reason = 'the partition layout differs — settings are backed up and rebuilt';
      }
    }
    s.planNotes.push(`write scope: ${scope} — ${reason}`);
    s.plan = plans.buildCustomFlashPlan(source, scope);
  } else if (s.source === SOURCE.STOCK) {
    const manifest = await stockManifest(flow);
    const source = await plans.loadStockFirmwareSource(
      manifest,
      { deviceName: s.deviceName, firmwareIndex: s.firmwareIndex, version: s.version, wipe },
      { onStatus, relayBase: flow.relayBase }
    );
    s.planNotes.push(`file: ${source.file.name} (${source.bytes.length} bytes)`);
    s.plan = plans.buildStockFlashPlan(source, { partitions: s.devicePartitions ?? [] });
  } else {
    s.plan = plans.buildManualFlashPlan(s.file, { family: s.family, wipe });
    s.planNotes.push(`file: ${s.file.name} (${s.file.bytes.length} bytes)`);
  }

  // The page states the integrity position rather than letting verify no-op.
  s.planNotes.push(
    s.plan.verify
      ? `checksum verified: ${s.plan.verify.sha256.slice(0, 16)}…`
      : 'UNVERIFIED — no checksum accompanies this firmware'
  );
  // Say what the plan does, not what was asked for: on nRF52 the wipe is a separate
  // erase package, and a manifest entry without one cannot honour the declaration.
  if (wipe) {
    s.planNotes.push(
      s.plan.eraseAll || s.plan.erasePackage
        ? 'This erases the identity and settings on the device.'
        : 'You chose New, but this firmware carries no erase step — existing data stays.'
    );
  }
}

// --- steps -----------------------------------------------------------------------------

export const STEPS = [
  {
    id: 'connect',
    ...CONNECT_HEAD,
    kind: 'action',
    applies: (flow) => !flow.dryRun,
    async run(flow, { onStatus }) {
      const copy = compatibilityCopy(detectSerialSupport(), detectBrowserKind());
      if (copy) throw new FlowBlockedError(`${copy.title} — ${copy.body} ${copy.suggestion}`);

      const s = flow.state;
      // A port the user picked at the checkpoint arrives here as the preferred one.
      s.port = await acquireUsableSerialPort({
        preferredPort: s.port,
        prompt: 'Select the device you want to flash.',
        onStatus,
      });
      s.family = deviceFamily(s.port);
      onStatus(`USB vendor says: ${s.family}`);
    },
  },

  {
    id: 'arm',
    ...CONNECT_HEAD,
    kind: 'action',
    applies: (flow) => !flow.dryRun,
    async run(flow, { onStatus }) {
      const s = flow.state;
      // Everything below the two supported families is refused here, because the branch
      // that follows treats anything not 'esp32' as Nordic.
      if (s.family === 'rp2040') {
        throw new FlowBlockedError(
          'This is an RP2040 board. It is flashed by copying a UF2 file to the drive it ' +
            'exposes, which this tool does not do. It has not been touched.'
        );
      }
      // Every board this tool targets is Espressif or Nordic; a bridge chip hides the MCU
      // and there is no second signal to fall back on.
      if (s.family === 'unknown') {
        throw new FlowBlockedError(
          'This device reports a USB vendor that does not identify the chip, so the ' +
            'firmware family cannot be determined. It has not been touched.'
        );
      }
      if (s.family === 'esp32') return armEsp32(flow, onStatus);
      // Stays in application mode here — the bootloader gate (nrf52-bootloader-plan.md)
      // needs to read it before any DFU transition, since DFU serves no CLI at all. DFU
      // entry now happens just before the write, in the `flash` step.
      onStatus('Reading the bootloader version…');
      s.bootloaderVersion = await nrf52.readBootloaderVersion(s.port);
      s.mode = 'app';
      // Same window as the ESP32 side, and this family keeps it open longer — DFU entry
      // is deferred to the write. Read here anyway so both families behave alike.
      await readExistingConfig(flow, onStatus);
      // No recon beyond that: nothing else device-side feeds the erase decision, and the
      // declaration in the next step is the only input for it.
    },
  },

  {
    id: 'install',
    title: 'What are you setting up?',
    // Optional helper line under any step's title. Every step may carry one; the
    // renderer reserves the space either way so the card's body never shifts.
    desc: 'Choose below between a fresh new install or performing an upgrade.',
    kind: 'choice',
    applies: () => true,
    // `icon` is a name, not markup — this module stays DOM-free.
    options: () => [
      {
        value: INSTALL.NEW,
        label: 'New device',
        note: 'Set up a fresh device with a clean configuration.',
        icon: 'new-device',
      },
      {
        value: INSTALL.UPDATE,
        label: 'Upgrade existing',
        note: 'Install the latest firmware and keep your existing data where possible.',
        icon: 'upgrade',
      },
    ],
    apply: (flow, value) => {
      flow.state.install = value;
    },
  },

  {
    id: 'usage',
    title: 'What type of node are you setting up?',
    desc: 'Pick the appropriate role for your deployment.',
    kind: 'choice',
    applies: () => true,
    // The escape hatch, named but not given equal weight to the two real choices. The
    // renderer decides what a "link" looks like; this only says one belongs here.
    aside: { label: 'Upload your own firmware', run: (flow) => chooseUploadYourOwn(flow) },
    options: () => [
      {
        value: USAGE.INFRASTRUCTURE,
        label: 'Set up infrastructure',
        note: 'Fixed location device that extends the mesh from a high roof, tower, etc.',
        icon: 'infrastructure',
      },
      {
        value: USAGE.CLIENT,
        label: 'Set up a client',
        note: 'Portable device paired with your phone or PC over Bluetooth or USB.',
        icon: 'client',
      },
    ],
    apply: (flow, value) => {
      flow.state.usage = value;
      // Both inputs to the source list are known now, so resolve it here rather than
      // leaving a skipped step to be inferred later. Re-runs on a back-and-change.
      flow.state.source = defaultSource(flow);
    },
  },

  {
    id: 'source',
    title: 'Choose a source for your firmware image.',
    desc: "MobMesh's firmware flasher can use firmware from multiple sources",
    kind: 'choice',
    // A step with one answer is not a question: where the enhanced build does not exist,
    // `usage.apply` has already set standard. Upload arrives by its own link.
    applies: (flow) => flow.state.source !== SOURCE.MANUAL && sourceChoices(flow).length > 1,
    options: sourceChoices,
    apply: (flow, value) => {
      flow.state.source = value;
    },
  },

  {
    id: 'maker',
    title: 'Who makes your device?',
    desc: "Choose your device's manufacturer from the choices below.",
    // Same square picture tiles as the device step. Upstream ships no maker artwork, so
    // the icon slot renders empty until the placeholder lands.
    layout: 'board',
    kind: 'choice',
    applies: (flow) => flow.state.source === SOURCE.STOCK,
    async options(flow) {
      const manifest = await stockManifest(flow);
      const makers = new Set();
      for (const device of manifest.devices) {
        if (device.type === flow.state.family && servesUsage(device, flow.state.usage)) {
          makers.add(device.maker ?? 'Other');
        }
      }
      return [...makers]
        .map((maker) => ({
          value: maker,
          label: manifest.makers[maker]?.name ?? maker,
          image: manifest.makers[maker]?.icon ?? null,
          imageFilter: manifest.makers[maker]?.iconFilter ?? null,
          imageDim: manifest.makers[maker]?.iconDim !== false,
          imagePlate: manifest.makers[maker]?.iconPlate !== false,
        }))
        // By display name, not key: sorting keys put the one capitalised key, `Ikoka`,
        // ahead of every lowercase one.
        .sort((a, b) => a.label.localeCompare(b.label, undefined, { numeric: true }));
    },
    apply: (flow, value) => {
      flow.state.maker = value;
    },
  },

  {
    id: 'device',
    title: 'Which device do you have?',
    desc: "The following devices are supported.",
    // Square picture tiles, the shape the shipped flasher uses for boards.
    layout: 'board',
    kind: 'choice',
    applies: (flow) => flow.state.source !== SOURCE.MANUAL,
    // Only the curated list can come up short; the stock catalogue is already everything.
    aside: (flow) =>
      flow.state.source === SOURCE.ENHANCED
        ? {
            label: "Use MeshCore Standard",
            variant: 'pill',
            icon: null,
            run: (f) => chooseStockCatalogue(f),
          }
        : null,
    async options(flow) {
      if (flow.state.source === SOURCE.ENHANCED) {
        const manifest = await customManifest(flow);
        // Value is the display id, not the board: two tiles can share one board.
        return manifest.displays.map((entry) => ({
          value: entry.id,
          label: entry.label,
          image: entry.icon,
        }));
      }
      const manifest = await stockManifest(flow);
      return manifest.devices
        .filter((d) => d.type === flow.state.family && (d.maker ?? 'Other') === flow.state.maker)
        // A screen-first board ships only a `gui` build, so under Infrastructure it would
        // reach the role step with nothing to offer.
        .filter((d) => servesUsage(d, flow.state.usage))
        // `tooltip` is upstream's picture as raw HTML and nothing else — the normaliser has
        // already pulled the src out, so a renderer never injects a third party's markup.
        // Value stays the upstream name — it keys the manifest — while the label is the
        // override's when one renames a device.
        .map((d) => ({
          value: d.name,
          label: d.label ?? d.name,
          image: d.image,
          imageFilter: d.imageFilter ?? null,
          imageDim: d.imageDim !== false,
          imagePlate: d.imagePlate !== false,
        }))
        // Manifest order is upstream's own and shuffles between releases. `numeric` keeps
        // T3 ahead of T10 rather than sorting them as strings.
        .sort((a, b) => a.label.localeCompare(b.label, undefined, { numeric: true }));
    },
    apply: (flow, value) => {
      if (flow.state.source !== SOURCE.ENHANCED) {
        flow.state.deviceName = value;
        flow.state.deviceIcon = selectedStockDevice(flow)?.image ?? null;
        flow.state.deviceIcon2 = null;
        return;
      }
      const entry = flow.state.customManifest.displays.find((d) => d.id === value);
      flow.state.boardDisplayKey = value;
      flow.state.boardKey = entry?.board ?? value;
      flow.state.deviceIcon = entry?.icon ?? null;
      flow.state.deviceIcon2 = entry?.icon2 ?? null;
    },
  },

  {
    id: 'role',
    title: 'Which role do you want to set up?',
    desc: "MeshCore supports the following roles for your device.",
    // Full-width rows with a small icon, the shipped flasher's variant shape.
    layout: 'row',
    kind: 'choice',
    applies: (flow) => flow.state.source !== SOURCE.MANUAL,
    async options(flow) {
      if (flow.state.source === SOURCE.ENHANCED) {
        const manifest = await customManifest(flow);
        const board = manifest.boards[flow.state.boardKey];
        return Object.entries(board.variants)
          .filter(([key]) => matchesUsage(key, flow.state.usage))
          .map(([key, variant]) => ({
            value: key,
            label: variant.label ?? key,
            image: manifest.variantIcons[key] ?? null,
          }));
      }
      const device = selectedStockDevice(flow);
      const catalogue = flow.state.stockManifest.roles;
      // A role is not unique within a device, so the value is the index into its own
      // firmware list — never the role name.
      return device.firmware
        .map((entry, index) => ({ entry, index }))
        .filter(({ entry }) => entry.versionOrder.length > 0)
        .filter(({ entry }) => matchesUsage(entry.role, flow.state.usage))
        .map(({ entry, index }) => {
          const known = catalogue[entry.role];
          // The entry's own title wins: it is what distinguishes two entries sharing a role.
          const name = entry.title ?? known?.title ?? entry.role ?? `firmware ${index}`;
          return {
            value: index,
            label: known?.subTitle && !entry.title ? `${name} — ${known.subTitle}` : name,
            note: known?.tooltip ?? null,
            icon: ROLE_ICONS[entry.role] ?? null,
          };
        });
    },
    apply: (flow, value) => {
      if (flow.state.source === SOURCE.ENHANCED) flow.state.variantKey = value;
      else {
        flow.state.firmwareIndex = value;
        // Version selection is skipped, so pin the newest here: versionOrder is
        // sorted newest first.
        flow.state.version = selectedStockDevice(flow).firmware[value].versionOrder[0];
      }
    },
  },

  {
    id: 'version',
    title: 'Which version?',
    kind: 'choice',
    // Skipped: everyone gets the newest build, pinned when the role is chosen. Kept
    // whole so restoring the choice is a one-line change to this predicate.
    applies: () => false,
    options(flow) {
      const entry = selectedStockDevice(flow).firmware[flow.state.firmwareIndex];
      return entry.versionOrder.map((version, index) => ({
        value: version,
        label: version,
        note: index === 0 ? 'newest' : null,
      }));
    },
    apply: (flow, value) => {
      flow.state.version = value;
    },
  },

  {
    id: 'file',
    title: 'Choose a firmware file',
    // Nothing accompanies a user's own file, so it is written unverified and the page
    // has to say so rather than let the absent check pass silently.
    desc: 'Your own image, written as supplied. Nothing checks it against a manifest.',
    kind: 'file',
    applies: (flow) => flow.state.source === SOURCE.MANUAL,
    accept: (flow) => (flow.state.family === 'nrf52' ? '.zip' : '.bin'),
    async apply(flow, picked) {
      const file = await plans.readUploadedFirmware(picked);
      const wipe = flow.state.install === INSTALL.NEW;

      // Build the plan and throw it away; the checks inside it are the point. Refuse while
      // the picker is on screen, not once the device is in programming mode.
      plans.buildManualFlashPlan(file, { family: flow.state.family, wipe });

      if (flow.state.family === 'nrf52') {
        // Must parse before hardware is touched: `executeDfuPlan` parses it too, but by
        // then a bad zip strands the board in DFU.
        await plans.validateDfuPackage(file.blob);

        // A wipe here is upstream's separate erase package, which a user's zip lacks.
        // Refused rather than writing an update while the UI says New.
        if (wipe) {
          throw new plans.UnsupportedFirmwareFileError(
            `${file.name} carries no erase step, so New Device cannot be honoured — the ` +
              `existing identity and settings would survive it. Choose Upgrade Existing, ` +
              `or use a stock build to erase the device.`
          );
        }
      }

      flow.state.file = file;
    },
  },

  {
    id: 'bootloader',
    title: 'Update the bootloader',
    desc: (flow) =>
      bootloaderTarget(flow)?.entry?.notice === 'otafixNeeded'
        ? "This device's factory bootloader cannot update over Bluetooth at all — updating " +
          'it now is strongly recommended.'
        : "This device's factory bootloader updates over Bluetooth unreliably.",
    kind: 'action',
    // New Device only (nrf52-bootloader-plan.md): needs a double-tap and a Chromium file
    // picker, both New-device-grade asks, and a half-finished write costs nothing only
    // because the next stages erase and reflash anyway.
    applies: (flow) => {
      // The dry-run harness calls an action step's run() with no hardware or window
      // present; this step needs both, so it never applies there — same as connect/arm.
      if (flow.dryRun) return false;
      if (flow.state.install !== INSTALL.NEW) return false;
      if (flow.state.family !== 'nrf52') return false;
      if (flow.state.source !== SOURCE.STOCK) return false;
      if (plans.bootloaderAlreadyCurrent(flow.state.bootloaderVersion)) return false;
      const target = bootloaderTarget(flow);
      if (!target) return false;
      try {
        return Boolean(plans.resolveBootloaderUpdate(target.device, target.entry));
      } catch {
        // Ambiguous file set (Xiao nRF52 WIO's `_ble`/`_ble_sense` pair): shown so run()
        // can explain why it cannot proceed, rather than silently vanishing.
        return true;
      }
    },
    async run(flow, { onStatus }) {
      const s = flow.state;
      // Re-entered after the checkpoint's click already did the write: nothing left to do.
      if (s.bootloaderUpdated) return;

      // Checked here, not at connect: this is the first point we know the step is needed,
      // and failing before the fetch means the user reads this instead of double-tapping
      // for a picker that was never going to open (Brave ships this off by default).
      const copy = fileSystemAccessCopy();
      if (copy) throw new FlowBlockedError(`${copy.title} — ${copy.body} ${copy.suggestion}`);

      const target = bootloaderTarget(flow);
      const wanted = plans.resolveBootloaderUpdate(target.device, target.entry);
      // Cached so a re-entry (dismissed picker, a prior failure) doesn't re-fetch.
      s.pendingBootloaderUf2 ??= await plans.loadBootloaderUf2(wanted, { relayBase: flow.relayBase, onStatus });

      // showSaveFilePicker() needs a fresh click, which run() never has — the UI answers
      // this with a checkpoint button, whose click is what actually writes the file.
      throw new nrf52.FilePickerRequiredError(
        'Double-tap the reset button so the UF2 drive mounts, then save the file there.',
        { bytes: s.pendingBootloaderUf2.bytes, suggestedName: s.pendingBootloaderUf2.file }
      );
    },
  },

  {
    id: 'location',
    // By this point the role is known, so the screen names it. Falls back to Repeater
    // rather than a generic word: they are the overwhelming majority of this path.
    title: (flow) => `${selectedRoleName(flow) ?? 'Repeater'} config`,
    desc: (flow) =>
      `These are basic settings for your ${(selectedRoleName(flow) ?? 'Repeater').toLowerCase()}.`,
    // The renderer owns this one: a map and three fields are not a list of options.
    kind: 'location',
    // Shown on both paths now: the arm-time read means an Upgrade arrives pre-filled with
    // what is on the device, so continuing changes nothing unless the user edits a field.
    applies: (flow) => LOCATION_ROLES.has(selectedRole(flow)),
    async apply(flow, { name, latitude, longitude, heightFt, email, adminPassword, identity, identityStatus, zone, zoneSettings }) {
      const s = flow.state;
      s.nodeName = name ?? '';
      s.latitude = latitude ?? null;
      s.longitude = longitude ?? null;
      s.heightFt = heightFt ?? '';
      s.email = email ?? '';
      s.adminPassword = adminPassword ?? '';
      // Never on an Upgrade: the device keeps the identity it already has, and `set
      // prv.key` would replace a working node's address with a freshly minted one.
      s.identity = s.install === INSTALL.UPDATE ? null : (identity ?? null);
      s.identityStatus = s.install === INSTALL.UPDATE ? null : (identityStatus ?? null);
      // The zone the pin fell in, and its settings. Fetched here so the provision step
      // stays synchronous about what it will send.
      s.zone = zone ?? null;
      s.zoneCommands = await loadZoneCommands(zoneSettings);
    },
  },

  {
    id: 'flash',
    title: 'Writing',
    kind: 'action',
    applies: () => true,
    async run(flow, { onStatus, onProgress }) {
      const s = flow.state;
      await buildPlan(flow, onStatus);
      if (flow.dryRun) {
        onStatus(`dry run — would write a ${s.plan.engine} plan, eraseAll=${s.plan.eraseAll}`);
        s.result = { dryRun: true };
        return;
      }

      if (s.plan.engine === 'esptool') {
        const startedAt = performance.now();
        const written = await esp32.executeFlashPlan(s.session, s.plan, { onProgress, onStatus });
        onStatus(`write took ${((performance.now() - startedAt) / 1000).toFixed(1)}s`);
        if (s.plan.preserveFs && s.evidence) {
          const restored = await esp32.restoreFilesystem(
            s.session,
            { evidence: s.evidence, plannedPartitions: s.plannedPartitions, eraseAll: s.plan.eraseAll },
            { onStatus, onProgress }
          );
          onStatus(`settings: ${restored.action} — ${restored.reason}`);
        }
        await esp32.returnToApplication(s.session, { onStatus });
        s.result = written;
        return;
      }

      // DFU entry waits until here: the bootloader step and its `get bootloader.ver` gate
      // both need the application still running, which is what arm() now leaves in place.
      if (s.mode !== 'dfu') {
        onStatus('Entering DFU mode…');
        s.port = await nrf52.enterDfuMode(s.port, { onStatus });
        s.mode = 'dfu';
      }
      const written = await nrf52.executeDfuPlan(s.port, s.plan, { onProgress, onStatus });
      const exit = await nrf52.returnToApplication(written.port ?? s.port, { onStatus });
      s.port = exit.port;
      s.result = { bytesWritten: written.bytesWritten, exit: exit.method };
    },
  },

  {
    id: 'provision',
    title: 'Post-flash setup',
    kind: 'action',
    applies: () => true,
    // Never fails the flash: the bytes are already on the device, so an unreachable
    // settings pass is a warning the user can act on by hand.
    async run(flow, { onStatus, onProgress }) {
      const s = flow.state;
      const commands = buildProvisionCommands(s);
      if (!commands.length) {
        onStatus('no settings to send for this role');
        return;
      }

      if (flow.dryRun) {
        onStatus(`dry run — would send ${commands.length} command(s)`);
        s.provision = { dryRun: true, commands: commands.map((step) => step.command) };
        return;
      }

      // The write left the esptool session holding the port; it has to go before the
      // device can be reopened at CLI baud.
      if (s.session) {
        await esptool.closeEsptoolSession(s.session).catch(() => {});
        s.session = null;
      }
      await closeSerialPortQuietly(s.port);

      try {
        const done = await provisionDevice(s, { preferredPort: s.port, onStatus, onProgress });
        s.port = done.port;
        s.provision = { results: done.results };
        const failed = done.results.filter((result) => !result.ok);
        onStatus(
          failed.length
            ? `${failed.length} of ${done.results.length} settings were rejected`
            : `applied ${done.results.length} setting(s)`
        );
      } catch (error) {
        console.warn('[provision] post-flash setup failed:', error);
        s.provision = { error: error.message };
        onStatus(`could not finish setup over serial — ${error.message}`);
      }
    },
  },

  {
    id: 'done',
    title: 'Finished',
    kind: 'terminal',
    applies: () => true,
  },
];

/**
 * What the provision step will send, for a renderer choosing a progress bar. Empty until
 * the flash step has built the plan.
 */
export function plannedProvisionCommands(flow) {
  return buildProvisionCommands(flow.state);
}

/**
 * Hand back everything a flow owns. Without it the esptool session keeps a reader on the
 * port and the next `connect` fails with the grant intact. Measured.
 */
export async function disposeFlow(flow) {
  if (!flow) return;
  const { session, port } = flow.state;
  flow.state.session = null;
  flow.state.port = null;
  if (session) await esptool.closeEsptoolSession(session).catch(() => {});
  if (port) await closeSerialPortQuietly(port);
}

/**
 * Where firmware can come from. The enhanced build is ESP32-and-infrastructure-only, so it
 * is absent rather than disabled; upload is an escape hatch, so the step can vanish.
 */
function sourceChoices(flow) {
  const choices = [];
  if (flow.state.family === 'esp32' && flow.state.usage !== USAGE.CLIENT) {
    choices.push({
      value: SOURCE.ENHANCED,
      label: 'MeshCore Enhanced+',
      note: 'Adds advanced features like fully remote firmware updates, added reliability and more. The one you want if your device is supported.',
      icon: 'enhanced',
    });
  }
  choices.push({
    value: SOURCE.STOCK,
    label: 'MeshCore Standard',
    note: 'Upstream MeshCore, with no added features. Choose this one if your device is missing from the enhanced device list.',
    icon: 'stock',
  });
  return choices;
}

/** The source when the step will not be shown, or null when it is a real choice. */
export function defaultSource(flow) {
  const choices = sourceChoices(flow);
  return choices.length === 1 ? choices[0].value : null;
}

/**
 * Take the upload escape hatch: sets the source and jumps to the picker, walking through
 * neither the source step nor any manifest.
 */
export function chooseUploadYourOwn(flow) {
  flow.state.source = SOURCE.MANUAL;
  const index = applicableSteps(flow).findIndex((step) => step.id === 'file');
  if (index >= 0) flow.stepIndex = index;
}

/**
 * Leave the curated list for the full MeshCore catalogue. Every enhanced-only pick is
 * cleared: the two paths key their device off different manifests.
 */
export function chooseStockCatalogue(flow) {
  const s = flow.state;
  s.source = SOURCE.STOCK;
  s.maker = null;
  s.deviceName = null;
  s.deviceIcon = null;
  s.deviceIcon2 = null;
  s.boardKey = null;
  s.boardDisplayKey = null;
  s.variantKey = null;
  s.firmwareIndex = null;
  s.version = null;
  const index = applicableSteps(flow).findIndex((step) => step.id === 'maker');
  if (index >= 0) flow.stepIndex = index;
}

/**
 * Regional settings from the file the zone names. Absent or unreadable applies nothing, since
 * a zone with no file yet must never fail a flash. A leading underscore parks a line.
 */
async function loadZoneCommands(file) {
  if (!file) return [];
  try {
    const res = await fetch(new URL(`../data/${file}`, import.meta.url), { cache: 'no-store' });
    if (!res.ok) return [];
    const doc = await res.json();
    return (doc.commands ?? []).filter(
      (command) => typeof command === 'string' && !command.startsWith('_')
    );
  } catch (error) {
    console.warn(`[flow] No regional settings in ${file}:`, error);
    return [];
  }
}

/** Re-run acquisition: the manual gesture re-enumerates the board, so the held port dies. */
export async function rewindToConnect(flow) {
  const { session, port } = flow.state;
  flow.state.session = null;
  flow.state.mode = null;
  if (session) await esptool.closeEsptoolSession(session).catch(() => {});
  if (port) await closeSerialPortQuietly(port);
  flow.stepIndex = Math.max(applicableSteps(flow).findIndex((step) => step.id === 'connect'), 0);
}

// --- navigation -------------------------------------------------------------------------

export function applicableSteps(flow) {
  return STEPS.filter((step) => step.applies(flow));
}

export function currentStep(flow) {
  return applicableSteps(flow)[flow.stepIndex] ?? null;
}

export function advance(flow) {
  flow.stepIndex = Math.min(flow.stepIndex + 1, applicableSteps(flow).length - 1);
}

// Only selection steps go back; past that, "back" is a hardware operation. Adding a step
// without listing it here silently breaks Back on it and the one after — `usage` lost it.
const REVERSIBLE = new Set([
  'install', 'usage', 'source', 'maker', 'device', 'role', 'version', 'file', 'location',
]);

export function canGoBack(flow) {
  const steps = applicableSteps(flow);
  const here = steps[flow.stepIndex];
  return Boolean(here && REVERSIBLE.has(here.id) && REVERSIBLE.has(steps[flow.stepIndex - 1]?.id));
}

export function goBack(flow) {
  if (canGoBack(flow)) flow.stepIndex -= 1;
}
