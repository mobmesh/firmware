// workflow.md's step order, made executable — rewrite_code.md §8/§9/§9A/§11 sequenced into
// the decisions a user actually makes. Deliberately DOM-free: what is under test here is the
// order of those decisions and what each one needs, which must outlive whatever renders it.
//
// Steps 1-3 are the workflow doc. `review` onward is what the flow turned out to still need
// and is not yet written up there.

import { compatibilityCopy, detectBrowserKind, detectSerialSupport } from './capability.js';
import { acquireUsableSerialPort, deviceFamily } from './serial-port.js';
import * as esp32 from './esp32.js';
import * as esptool from './esptool.js';
import * as nrf52 from './nrf52.js';
import * as plans from './flash-plan.js';
import { partitionTablesMatch } from './partitions.js';
import { STOCK_RELAY_BASE } from './constants.js';

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
  verifyWrite = true,
  manifestBase = plans.CUSTOM_MANIFEST_BASE,
  relayBase = STOCK_RELAY_BASE,
} = {}) {
  return {
    dryRun,
    verifyWrite,
    manifestBase,
    relayBase,
    stepIndex: 0,
    state: {
      port: null,
      session: null,
      // Step 1 reads this off the PID. A dry run has no device, so the harness supplies it.
      family,
      mode: null,
      devicePartitions: null,
      plannedPartitions: null,
      evidence: null,
      install: null,
      source: null,
      maker: null,
      deviceName: null,
      boardKey: null,
      variantKey: null,
      firmwareIndex: null,
      version: null,
      file: null,
      region: null,
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

// --- step 1: connect and arm ----------------------------------------------------------

async function armEsp32(flow, onStatus) {
  const s = flow.state;
  const { mode, version } = await esp32.resolveEsp32Mode(s.port);
  s.mode = mode;
  onStatus(`found in ${mode}${version ? ` — ${version}` : ''}`);

  if (mode === esp32.ESP32_MODE.APP) {
    s.port = await esp32.enterDownloadMode(s.port, {
      manualInstruction: 'hold the PGM/BOOT button and tap RST, then release',
      onStatus,
    });
  } else if (mode !== esp32.ESP32_MODE.BOOTLOADER) {
    throw new FlowBlockedError(
      'This device answers neither the CLI nor the bootloader, so what is on it is unknown. ' +
        'It will not be erased (§10.2 state 3).'
    );
  }

  s.session = await esptool.openEsptoolSession(s.port);
  // Only the cheap table read belongs here. §10.5's evidence chain compares against the
  // firmware's own partition table, which is not known until the source step has run.
  s.devicePartitions = await esp32.readPartitionTable(s.session);
  onStatus(`partition table: ${s.devicePartitions.length} entries`);
}

// --- steps -----------------------------------------------------------------------------

export const STEPS = [
  {
    id: 'connect',
    title: 'Connect the device',
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
    title: 'Enter programming mode',
    kind: 'action',
    applies: (flow) => !flow.dryRun,
    async run(flow, { onStatus }) {
      const s = flow.state;
      // Every board this tool targets is Espressif or Nordic; a bridge chip hides the MCU
      // and there is no second signal to fall back on.
      if (s.family === 'unknown') {
        throw new FlowBlockedError(
          'This device reports a USB vendor that does not identify the chip, so the ' +
            'firmware family cannot be determined. It has not been touched.'
        );
      }
      if (s.family === 'esp32') return armEsp32(flow, onStatus);
      s.port = await nrf52.enterDfuMode(s.port, { onStatus });
      s.mode = 'dfu';
      // No recon on this side: DFU cannot read flash back (C5), so nothing device-side
      // feeds the erase decision and the declaration in the next step is the only input.
    },
  },

  {
    id: 'install',
    title: 'Is this a new device or an existing one?',
    kind: 'choice',
    applies: () => true,
    options: () => [
      { value: INSTALL.NEW, label: 'New — erase everything', note: 'Identity and settings are lost.' },
      { value: INSTALL.UPDATE, label: 'Update — keep settings where possible' },
    ],
    apply: (flow, value) => {
      flow.state.install = value;
    },
  },

  {
    id: 'source',
    title: 'Where does the firmware come from?',
    kind: 'choice',
    applies: () => true,
    options: (flow) => {
      const choices = [];
      // boards.json is generated from a built partitions.bin, so the custom path is
      // structurally ESP32-only — it is absent here, not disabled.
      if (flow.state.family === 'esp32') {
        choices.push({ value: SOURCE.ENHANCED, label: 'Enhanced — MobMesh' });
      }
      choices.push({ value: SOURCE.STOCK, label: 'Stock — upstream MeshCore' });
      choices.push({ value: SOURCE.MANUAL, label: 'Manual upload — your own file' });
      return choices;
    },
    apply: (flow, value) => {
      flow.state.source = value;
    },
  },

  {
    id: 'maker',
    title: 'Who makes it?',
    kind: 'choice',
    applies: (flow) => flow.state.source === SOURCE.STOCK,
    async options(flow) {
      const manifest = await stockManifest(flow);
      const makers = new Set();
      for (const device of manifest.devices) {
        if (device.type === flow.state.family) makers.add(device.maker ?? 'Other');
      }
      return [...makers]
        .sort()
        .map((maker) => ({ value: maker, label: manifest.makers[maker]?.name ?? maker }));
    },
    apply: (flow, value) => {
      flow.state.maker = value;
    },
  },

  {
    id: 'device',
    title: 'Which device?',
    kind: 'choice',
    applies: (flow) => flow.state.source !== SOURCE.MANUAL,
    async options(flow) {
      if (flow.state.source === SOURCE.ENHANCED) {
        const manifest = await customManifest(flow);
        return Object.entries(manifest.boards).map(([key, board]) => ({
          value: key,
          label: board.label,
        }));
      }
      const manifest = await stockManifest(flow);
      return manifest.devices
        .filter((d) => d.type === flow.state.family && (d.maker ?? 'Other') === flow.state.maker)
        .map((d) => ({ value: d.name, label: d.name, note: d.tooltip ?? null }));
    },
    apply: (flow, value) => {
      if (flow.state.source === SOURCE.ENHANCED) flow.state.boardKey = value;
      else flow.state.deviceName = value;
    },
  },

  {
    id: 'role',
    title: 'What is it for?',
    kind: 'choice',
    applies: (flow) => flow.state.source !== SOURCE.MANUAL,
    async options(flow) {
      if (flow.state.source === SOURCE.ENHANCED) {
        const manifest = await customManifest(flow);
        const board = manifest.boards[flow.state.boardKey];
        return Object.entries(board.variants).map(([key, variant]) => ({
          value: key,
          label: variant.label ?? key,
        }));
      }
      const device = selectedStockDevice(flow);
      const catalogue = flow.state.stockManifest.roles;
      // A role is not unique within a device, so the value is the index into its own
      // firmware list — never the role name.
      return device.firmware
        .map((entry, index) => ({ entry, index }))
        .filter(({ entry }) => entry.versionOrder.length > 0)
        .map(({ entry, index }) => {
          const known = catalogue[entry.role];
          // The entry's own title wins: it is what distinguishes two entries sharing a role.
          const name = entry.title ?? known?.title ?? entry.role ?? `firmware ${index}`;
          return {
            value: index,
            label: known?.subTitle && !entry.title ? `${name} — ${known.subTitle}` : name,
            note: known?.tooltip ?? null,
          };
        });
    },
    apply: (flow, value) => {
      if (flow.state.source === SOURCE.ENHANCED) flow.state.variantKey = value;
      else flow.state.firmwareIndex = value;
    },
  },

  {
    id: 'version',
    title: 'Which version?',
    kind: 'choice',
    // §13.3 is undecided (list them all vs pin to latest); the wireframe lists them so the
    // real count is visible rather than assumed.
    applies: (flow) => flow.state.source === SOURCE.STOCK,
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
    kind: 'file',
    applies: (flow) => flow.state.source === SOURCE.MANUAL,
    accept: (flow) => (flow.state.family === 'nrf52' ? '.zip' : '.bin'),
    async apply(flow, picked) {
      flow.state.file = await plans.readUploadedFirmware(picked);
    },
  },

  {
    id: 'region',
    title: 'Operating region',
    kind: 'choice',
    applies: () => true,
    // §10.7 has no role → command table and boards.json carries no region parameters, so
    // there is nothing to offer yet. The step is here because the flow needs the slot.
    unspecified: true,
    options: () => [{ value: null, label: 'Not specified yet — §10.7 has no command table' }],
    apply: (flow, value) => {
      flow.state.region = value;
    },
  },

  {
    id: 'review',
    title: 'Review before writing',
    kind: 'confirm',
    applies: () => true,
    async prepare(flow, { onStatus }) {
      const s = flow.state;
      const wipe = s.install === INSTALL.NEW;
      s.planNotes = [];

      if (s.source === SOURCE.ENHANCED) {
        const manifest = await customManifest(flow);
        const source = await plans.loadCustomFirmwareSource(manifest, s.boardKey, s.variantKey, { onStatus });
        // §10.6 sizes the restore against the layout being *written*, not the one read
        // off the device — those differ exactly when the restore has to rebuild.
        s.plannedPartitions = source.plannedPartitions;
        let scope = 'full-layout';
        let reason = 'you chose New, so the whole layout is written';
        if (!wipe) {
          if (flow.dryRun) {
            reason = 'dry run — the real scope needs Step 1\u2019s table read';
          } else if (partitionTablesMatch(s.devicePartitions, source.plannedPartitions)) {
            // Step 2: the fork is the table comparison alone. A matching layout never
            // touches the filesystem, so there is nothing to read back and nothing to
            // restore — the content check would only risk erasing what was asked to be kept.
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

      if (s.plan.bootloaderPackage) {
        const because =
          s.plan.bootloaderReason === 'otafixNeeded'
            ? 'its factory bootloader cannot update over Bluetooth at all'
            : 'its factory bootloader updates over Bluetooth unreliably';
        s.planNotes.push(
          `This device also gets the OTAFIX bootloader (${s.plan.bootloaderPackage.size} bytes), ` +
            `because ${because}. It is written first and erases the application, which the ` +
            `firmware below then replaces.`
        );
      }

      // C7 — the page states the integrity position rather than letting verify no-op.
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
    },
    confirmLabel: 'Write it',
  },

  {
    id: 'flash',
    title: 'Writing',
    kind: 'action',
    applies: () => true,
    async run(flow, { onStatus, onProgress }) {
      const s = flow.state;
      if (flow.dryRun) {
        onStatus(`dry run — would write a ${s.plan.engine} plan, eraseAll=${s.plan.eraseAll}`);
        s.result = { dryRun: true };
        return;
      }

      if (s.plan.engine === 'esptool') {
        const startedAt = performance.now();
        const written = await esp32.executeFlashPlan(s.session, s.plan, {
          onProgress,
          onStatus,
          verifyWrite: flow.verifyWrite,
        });
        const seconds = ((performance.now() - startedAt) / 1000).toFixed(1);
        onStatus(`write took ${seconds}s (verify ${flow.verifyWrite ? 'on' : 'OFF'})`);
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
    async run(flow, { onStatus }) {
      const commands = flow.state.plan?.postFlash?.commands ?? null;
      if (!commands) {
        onStatus('nothing to send — §10.7 role → command table does not exist yet');
        return;
      }
      onStatus(`would send ${commands.length} command(s): ${commands.join(', ')}`);
    },
  },

  {
    id: 'done',
    title: 'Finished',
    kind: 'terminal',
    applies: () => true,
  },
];

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

// Only the selection steps go back. Once the device is in programming mode or bytes are
// moving, "back" is a hardware operation, not a UI one.
const REVERSIBLE = new Set(['install', 'source', 'maker', 'device', 'role', 'version', 'file', 'region', 'review']);

export function canGoBack(flow) {
  const steps = applicableSteps(flow);
  const here = steps[flow.stepIndex];
  return Boolean(here && REVERSIBLE.has(here.id) && REVERSIBLE.has(steps[flow.stepIndex - 1]?.id));
}

export function goBack(flow) {
  if (canGoBack(flow)) flow.stepIndex -= 1;
}
