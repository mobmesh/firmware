import { connectSerial, createCliSession, getAuthorizedPorts, requestFilteredPort, ESPRESSIF_VENDOR_ID } from "./serial-utils.js";
import { readSpiffsFiles, buildSpiffsImage, spiffsUsedSize } from "./spiffs.js";

const REPO = "mobmesh/firmware";
const ESPTOOL_JS_URL = "./vendor/esptool-js/bundle.js";   // vendored, not CDN-loaded
const CLI_BAUD_RATE = 115200;
const BOOTLOADER_POLL_INTERVAL_MS = 500;
const BOOTLOADER_POLL_ATTEMPTS = 40; // ~20s -- re-enumeration after the reboot gesture has been observed taking several seconds

// ROM bootloader PID (0x1001). No longer distinct from app mode: upstream
// v1.17.0 moved heltec_v4 to ARDUINO_USB_MODE=1, and xiao_c3 never had the
// TinyUSB 0x2 mode at all, so a running node enumerates identically.
// Used only to pick the reset path; erase-vs-preserve is decided in runFlash()
// from the chip's own partition table and filesystem.
const BOARD_BOOTLOADER_USB_IDS = {
  heltec_v4: { usbVendorId: ESPRESSIF_VENDOR_ID, usbProductId: 0x1001 },
  xiao_c3: { usbVendorId: ESPRESSIF_VENDOR_ID, usbProductId: 0x1001 },
};

// Web Serial defaults to 255 bytes, which overruns on any main-thread pause and
// surfaces as SLIP framing errors. macOS is less forgiving than Linux.
const SERIAL_READ_BUFFER_SIZE = 64 * 1024;

const wizard = document.getElementById("wizard");

const state = {
  mode: null,      // "new" | "update"
  boardId: null,
  variantId: null,
  slot: null,      // "A" | "B"
  // Already in flash mode before any menu asked, so renderVariant() skips the
  // reboot gesture.
  alreadyInFlashMode: false,
  // The tile's own icon, not the board's -- tiles can share a board
  boardIcon: null,
  // Post-flash icon; falls back to boardIcon when left blank
  boardIcon2: null,
};

let boards = null;
// Tiles for renderBoard(), separate from generated boards.json so several can
// point at one board and labels can be overridden. { id, label, board }
let boardDisplay = null;
// { <location key>: <settings definition URL> }, from member-config-urls.json.
let memberConfigUrls = null;
let esploaderApi = null;
let connection = null; // { port, transport, esploader }
let earlyPort = null;  // paired during renderPlugIn, carried forward into renderConnect
let pairingPollTimer = null;

function hex(v) {
  return Number(v);
}

// Erased flash reads as 0xFF, not 0x00
function blankBuffer(size) {
  return new Uint8Array(size).fill(0xff);
}

// Errors reaching here can be Error objects, DOMExceptions or bare strings.
function errMsg(err) {
  return (err && err.message) || String(err);
}

// Closing a port that's already gone is routine, not a failure.
function closeQuietly(port) {
  return port ? port.close().catch(() => {}) : Promise.resolve();
}

// "keep" leaves each image's own flash-config header alone; PlatformIO bakes
// correct values in at compile time.
const FLASH_WRITE_OPTS = {
  flashMode: "keep",
  flashFreq: "keep",
  flashSize: "keep",
  compress: true,
};

// ESP-IDF partition table: 32-byte entries from 0x8000, each
// magic(2)+type(1)+subtype(1)+offset(4)+size(4)+label(16)+flags(4),
// little-endian. Ends at the first non-matching magic.
const PARTITION_ENTRY_SIZE = 32;
const PARTITION_MAGIC = 0x50aa;
const PARTITION_DATA_TYPE = 0x01;
const PARTITION_SUBTYPE_SPIFFS = 0x82;
const PARTITION_SUBTYPE_LITTLEFS = 0x83;

function parsePartitionTable(bytes) {
  const partitions = [];
  for (let off = 0; off + PARTITION_ENTRY_SIZE <= bytes.length; off += PARTITION_ENTRY_SIZE) {
    const view = new DataView(bytes.buffer, bytes.byteOffset + off, PARTITION_ENTRY_SIZE);
    if (view.getUint16(0, true) !== PARTITION_MAGIC) break;
    const label = new TextDecoder()
      .decode(bytes.subarray(off + 12, off + 28))
      .replace(/\0.*$/s, "");
    partitions.push({
      type: view.getUint8(2),
      subtype: view.getUint8(3),
      offset: view.getUint32(4, true),
      size: view.getUint32(8, true),
      label,
    });
  }
  return partitions;
}

// Finds the first SPIFFS or LittleFS data partition in a parsed table, if any.
function findFilesystemPartition(partitions) {
  return partitions.find(
    (p) =>
      p.type === PARTITION_DATA_TYPE &&
      (p.subtype === PARTITION_SUBTYPE_SPIFFS || p.subtype === PARTITION_SUBTYPE_LITTLEFS)
  );
}

// Only the first two appear in every release back to v1.0.0c. The rest arrived
// later (/com_prefs v1.4.1, /s_contacts v1.9.0, /regions2 v1.10.0, /prefs.json
// v1.17.0), so dropping them would stop recognising older devices.
// A v1.17 device flashed fresh may carry only /identity/_main.id and
// /prefs.json -- an upgraded one still has the legacy files too.
const MESHCORE_FILES = ["/identity/_main.id", "/node_prefs", "/com_prefs", "/prefs.json", "/regions2", "/s_contacts"];

// An empty or unreadable filesystem answers no, which is correct either way
function looksLikeMeshCore(files) {
  const bare = (name) => name.replace(/^\/+/, "");
  const present = new Set(files.map((f) => bare(f.name)));
  return MESHCORE_FILES.some((name) => present.has(bare(name)));
}

// Entry by entry, not raw bytes -- the image carries an MD5 entry and padding
// that differ without the layout differing.
function partitionTablesMatch(a, b) {
  if (a.length !== b.length) return false;
  return a.every(
    (p, i) =>
      p.type === b[i].type &&
      p.subtype === b[i].subtype &&
      p.offset === b[i].offset &&
      p.size === b[i].size &&
      p.label === b[i].label
  );
}

// SPIFFS parsing/building lives in spiffs.js

// Reads until the stream closes, which signals the device re-enumerating.
// Never throws -- a port that won't open just resolves.
async function waitForPortDrop(port, baudRate) {
  try {
    if (!port.readable) await port.open({ baudRate });
    const decoder = new TextDecoderStream();
    const readableClosed = port.readable.pipeTo(decoder.writable);
    const reader = decoder.readable.getReader();
    try {
      // eslint-disable-next-line no-constant-condition
      while (true) {
        const { done } = await reader.read();
        if (done) break;
      }
    } catch {
      // connection loss -- expected once the device disconnects/resets
    }
    await readableClosed.catch(() => {});
  } catch {
    // port never opened -- fall through to the caller's reconnect attempt anyway
  }
}

function currentBoard() {
  return boards[state.boardId];
}

function currentVariant() {
  return currentBoard().variants[state.variantId];
}

function currentBootloaderUsbId() {
  return BOARD_BOOTLOADER_USB_IDS[state.boardId] || null;
}

function matchesBootloaderUsbId(port) {
  const target = currentBootloaderUsbId();
  // Unknown board answers no -- yes would erase on a missing table entry alone
  if (!target) return false;
  const info = port.getInfo();
  return info.usbVendorId === target.usbVendorId && info.usbProductId === target.usbProductId;
}

// Polls getPorts() for the bootloader PID, ~10s. Null on timeout.
async function waitForBootloaderPort() {
  const target = currentBootloaderUsbId();
  if (!target) return null;

  for (let i = 0; i < BOOTLOADER_POLL_ATTEMPTS; i++) {
    const ports = await getAuthorizedPorts();
    const match = ports.find((p) => {
      const info = p.getInfo();
      return info.usbVendorId === target.usbVendorId && info.usbProductId === target.usbProductId;
    });
    if (match) {
      if (!match.readable) await match.open({ baudRate: CLI_BAUD_RATE }).catch(() => {});
      await new Promise((resolve) => setTimeout(resolve, 2000));
      await closeQuietly(match);
      return match;
    }
    await new Promise((resolve) => setTimeout(resolve, BOOTLOADER_POLL_INTERVAL_MS));
  }
  return null;
}

// Native-USB boards have no RTS/DTR-to-EN wiring; the running app watches for
// this gesture and calls esp_restart(). Both transitions required. Entering
// bootloader mode only -- manual PGM+RST is the fallback.
async function rebootToBootloaderMode(port) {
  try {
    if (!port.readable) await port.open({ baudRate: CLI_BAUD_RATE });
    await port.setSignals({ dataTerminalReady: false });
    await port.setSignals({ requestToSend: true });
    await new Promise((resolve) => setTimeout(resolve, 100));
    await port.setSignals({ dataTerminalReady: true });
    await port.setSignals({ requestToSend: false });
  } catch {
    // Unsupported or already closed -- the manual-reset flow still applies
  } finally {
    // The device is about to disconnect and re-enumerate under a new PID
    // (or already has) -- this port object is done either way, and
    // getAuthorizedPorts()'s poll in waitForBootloaderPort() needs to see
    // it as closed rather than still holding it open.
    await closeQuietly(port);
  }
}

async function loadBoards() {
  const res = await fetch("./boards.json", { cache: "no-store" });
  if (!res.ok) throw new Error(`Could not load boards.json (${res.status})`);
  boards = await res.json();
}

// Optional -- falls back to one tile per boards.json entry
async function loadBoardDisplay() {
  try {
    const res = await fetch("./board-display.json", { cache: "no-store" });
    if (!res.ok) throw new Error("missing");
    const entries = await res.json();
    boardDisplay = Object.entries(entries).map(([id, e]) => ({
      id,
      label: e.label,
      board: e.board,
      icon: e.icon || "",
      // Falls back to `icon` when left blank -- see board-display.json.
      icon2: e.icon2 || e.icon || "",
    }));
  } catch {
    boardDisplay = Object.entries(boards)
      .filter(([id]) => !id.startsWith("_"))
      .map(([id, b]) => ({ id, label: b.label, board: id, icon: "", icon2: "" }));
  }
}

// Location key to settings URL. Off-site entries need CORS for this origin.
async function loadMemberConfigUrls() {
  try {
    const res = await fetch("./member-config-urls.json", { cache: "no-store" });
    if (!res.ok) throw new Error("missing");
    memberConfigUrls = await res.json();
  } catch {
    memberConfigUrls = {};
  }
}

// Lives in the page header, outside #wizard, so it's read off the DOM
function selectedMemberConfigKey() {
  const selected = document.querySelector("#location-menu li[aria-selected='true']");
  return selected ? selected.dataset.value : null;
}

// Hidden and checked by default -- exposed as a real control once there's a
// reason to turn it off.
function regionalSettingsEnabled() {
  const box = document.getElementById("set-regional-settings");
  return box ? box.checked : true;
}

// Returns [] on any failure -- an unreachable definition must not block a flash
async function loadMemberCommands() {
  if (!regionalSettingsEnabled()) return [];
  const key = selectedMemberConfigKey();
  const url = key && memberConfigUrls ? memberConfigUrls[key] : null;
  if (!url) return [];
  try {
    const res = await fetch(url, { cache: "no-store" });
    if (!res.ok) throw new Error(`HTTP ${res.status}`);
    const config = await res.json();
    // Underscore keys are notes to whoever maintains the file.
    const commands = config && !Array.isArray(config) ? config.commands : null;
    if (!Array.isArray(commands)) return [];
    return commands.filter((c) => typeof c === "string" && c.trim() && !c.startsWith("_"));
  } catch (err) {
    console.warn(`[config] Could not load settings for '${key}':`, err);
    return [];
  }
}

async function verifySha256(data, shaPath, label) {
  const res = await fetch(`./${shaPath}`, { cache: "no-store" });
  if (!res.ok) return; // no sidecar committed for this build yet -- proceed unverified
  const body = (await res.text()).trim();
  const expected = body.split(":")[0].toLowerCase();   // "<hash>" or "<hash>:<offset>"
  const digest = await crypto.subtle.digest("SHA-256", data);
  const actual = [...new Uint8Array(digest)].map((b) => b.toString(16).padStart(2, "0")).join("");
  if (actual !== expected) {
    throw new Error(`${label} failed checksum verification (expected ${expected}, got ${actual}).`);
  }
}

async function loadLocalBinary(path) {
  const res = await fetch(`./${path}`, { cache: "no-store" });
  if (!res.ok) throw new Error(`Could not load ${path}`);
  return new Uint8Array(await res.arrayBuffer());
}

function render(html) {
  wizard.innerHTML = html;
}

function webSerialSupported() {
  return "serial" in navigator;
}

// ---- Steps -----------------------------------------------------------

// Pairs the board first. Polls getPorts(), falling back to a Connect button;
// the port carries forward as `earlyPort` so later steps don't re-prompt.
function renderPlugIn() {
  if (!webSerialSupported()) {
    render(`
      <h2 class="step-title">Browser not supported</h2>
      <div class="error-box">
        This tool needs the Web Serial API, which is only available in
        Chromium-based browsers (Chrome, Edge, Opera) over HTTPS. Please
        reopen this page in one of those browsers.
      </div>
    `);
    return;
  }

  render(`
    <h2 class="step-title">Plug in your device</h2>
    <p class="step-desc">Connect your device to this computer via USB to get started.</p>
    <div class="step-focus">
      <div class="actions" style="justify-content: center;">
        <button class="btn btn-primary" id="connect" style="display: none;">Connect via USB</button>
      </div>
      <p class="status-text" id="status">Waiting for device...</p>
    </div>
  `);

  const status = wizard.querySelector("#status");
  const button = wizard.querySelector("#connect");

  const advance = (port) => {
    if (pairingPollTimer) {
      clearInterval(pairingPollTimer);
      pairingPollTimer = null;
    }
    earlyPort = port;
    renderBoard();
  };

  const tryAuthorizedPort = async () => {
    const ports = await getAuthorizedPorts();
    if (ports.length === 0) return null;
    if (ports[0].readable) return ports[0];
    try {
      await ports[0].open({ baudRate: CLI_BAUD_RATE });
      return ports[0];
    } catch {
      return null;
    }
  };

  const poll = async () => {
    const port = await tryAuthorizedPort();
    if (port) {
      status.textContent = "Device detected -- continuing...";
      advance(port);
      return;
    }
    button.style.display = "";
  };

  pairingPollTimer = setInterval(poll, 700);
  poll();

  button.addEventListener("click", async () => {
    button.disabled = true;
    status.textContent = "Waiting for port selection...";
    try {
      const port = await requestFilteredPort();
      await port.open({ baudRate: CLI_BAUD_RATE });
      advance(port);
    } catch (err) {
      button.disabled = false;
      status.textContent = errMsg(err) || "Could not connect.";
    }
  });
}

function renderBoard() {
  render(`
    <h2 class="step-title">Select your hardware</h2>
    <p class="step-desc">These ESP32 based devices are currently supported by our firmware mods.</p>
    <div class="board-tiles">
      ${boardDisplay
        .map(
          (entry) =>
            `<button class="board-tile" data-board="${entry.board}" data-display-id="${entry.id}" data-icon-src="${entry.icon || ""}" data-icon2-src="${entry.icon2 || ""}">
              ${
                entry.icon
                  ? `<img class="board-tile-icon" src="${entry.icon}" alt="" />`
                  : `<span class="board-tile-icon" data-icon="${entry.id}"></span>`
              }
              <strong>${entry.label}</strong>
            </button>`
        )
        .join("")}
    </div>
    <div class="actions actions-footer">
      <button class="btn btn-secondary" id="back" style="display: none;">&lt; Back</button>
    </div>
  `);

  wizard.querySelectorAll("[data-board]").forEach((el) => {
    el.addEventListener("click", () => {
      state.boardId = el.dataset.board;
      state.boardIcon = el.dataset.iconSrc || null;
      state.boardIcon2 = el.dataset.icon2Src || null;
      // Reset path only -- says nothing about what is already on the chip.
      state.alreadyInFlashMode = Boolean(earlyPort && matchesBootloaderUsbId(earlyPort));
      const info = earlyPort ? earlyPort.getInfo() : {};
      const asHex = (v) => (typeof v === "number" ? `0x${v.toString(16)}` : "unknown");
      console.info(
        `[flash] Device enumerated as VID ${asHex(info.usbVendorId)} PID ${asHex(info.usbProductId)} -- ` +
          `${state.alreadyInFlashMode ? "skipping the reboot gesture" : "will send the reboot gesture"}`
      );
      renderVariant();
    });
  });
  wizard.querySelector("#back").addEventListener("click", renderPlugIn);
}

// Display-only. variant.label comes from release_title, which names GitHub
// releases too, so it isn't ours to rename.
const VARIANT_LABEL_OVERRIDES = {
  Room: "Room Server",
};

// Keyed by variant id, which is stable across boards, not by label
const VARIANT_ICONS = {
  repeater: "icons/repeater_variant.png",
  room_server: "icons/room_variant.png",
};

function renderVariant() {
  const board = currentBoard();
  const entries = Object.entries(board.variants);
  render(`
    <h2 class="step-title">Select firmware</h2>
    <p class="step-desc">The latest published release for the chosen role will be used.</p>
    <div class="tiles">
      ${entries
        .map(
          ([id, v]) =>
            `<button class="tile tile-with-icon" data-variant="${id}">
              ${VARIANT_ICONS[id] ? `<img class="tile-icon" src="${VARIANT_ICONS[id]}" alt="" />` : ""}
              <strong>${VARIANT_LABEL_OVERRIDES[v.label] || v.label}</strong>
            </button>`
        )
        .join("")}
    </div>
    <div class="actions actions-footer">
      <button class="btn btn-secondary" id="back">&lt; Back</button>
    </div>
  `);

  wizard.querySelectorAll("[data-variant]").forEach((el) => {
    el.addEventListener("click", async () => {
      state.variantId = el.dataset.variant;
      if (state.alreadyInFlashMode) {
        // Already in bootloader mode, so no reboot gesture is needed --
        // straight to the esptool-js handoff.
        renderConnect();
        return;
      }
      // Only from here -- firing it in bootloaderAssist() too raced setSignals()
      if (earlyPort) await rebootToBootloaderMode(earlyPort);
      renderConnect();
    });
  });
  // Board selection is the only screen between plug-in and here
  wizard.querySelector("#back").addEventListener("click", renderBoard);
}

async function renderConnect() {
  const board = currentBoard();
  render(`
    <h2 class="step-title">Connect your device</h2>
    <p class="step-desc" id="connect-desc">Switching your device into flash mode automatically...</p>
    <div class="step-focus">
      <div class="actions" style="justify-content: center;">
        <button class="btn btn-secondary" id="back" style="display: none;">&lt; Back</button>
        <button class="btn btn-primary" id="connect" style="display: none;">Connect via USB</button>
      </div>
      <p class="status-text" id="status">Connecting...</p>
    </div>
  `);

  wizard.querySelector("#back").addEventListener("click", renderVariant);

  const status = wizard.querySelector("#status");
  const button = wizard.querySelector("#connect");

  async function connectWithEsploader(port) {
    if (!esploaderApi) esploaderApi = await import(ESPTOOL_JS_URL);
    const { ESPLoader, Transport } = esploaderApi;
    // tracing=true hex-dumps every packet via console.log, synchronously, on
    // the thread that has to keep acking. Only for wire-protocol debugging.
    const transport = new Transport(port, false);
    // esptool's chatter stays off the page and goes to devtools only
    const terminal = {
      clean() {},
      writeLine(line) {
        console.log(`[esptool] ${line}`);
      },
      write() {},
    };
    // Nominal on native USB-Serial/JTAG, not a clock constraint. main()
    // renegotiates to it once the stub is up.
    const esploader = new ESPLoader({
      transport,
      baudrate: 2000000,
      terminal,
      debugLogging: false,
      serialOptions: { bufferSize: SERIAL_READ_BUFFER_SIZE },
    });
    status.textContent = "Detecting chip...";
    let chip;
    try {
      chip = await esploader.main();
    } catch (err) {
      // `connection` isn't set yet, so release esptool's hold here
      await transport.disconnect().catch(() => {});
      throw err;
    }
    connection = { port, transport, esploader };
    status.textContent = `Connected: ${chip}`;
    renderFlashing();
  }

  // Fallback for boards whose auto-reset esptool-js can't detect. Polls for
  // the bootloader PID rather than watching for a disconnect, which may
  // already have happened. Hands off only on a confirmed PID.
  async function bootloaderAssist(port) {
    status.textContent = "Waiting for device to enter bootloader mode...";
    button.style.display = "none";

    // Poll-and-wait only. renderVariant() fires the gesture; firing it here
    // too raced two setSignals() sequences on the same port.
    status.textContent = "Confirming bootloader mode...";
    const bootPort = await waitForBootloaderPort();
    if (!bootPort) {
      // Often not a real failure: Web Serial authorizes per USB interface, so
      // a bootloader interface never paired on this machine is invisible to
      // the poll. Offer the picker, then the same connectAndFlash() path.
      status.textContent = "Couldn't detect bootloader mode automatically.";
      // Only now -- the manual PGM+RST gesture is a fallback for when the
      // automatic reboot-to-bootloader attempt didn't pan out, not
      // something the user should be told to do while that attempt is
      // still in progress.
      wizard.querySelector("#connect-desc").textContent = board.connectNote;
      button.textContent = "Reconnect via USB";
      button.style.display = "";
      button.disabled = false;
      // Alone and centered -- there's nothing to go back to mid-wait
      wizard.querySelector("#back").style.display = "none";
      button.closest(".actions").style.justifyContent = "center";
      button.onclick = pickPortAndFlash;
      return;
    }
    status.textContent = "Bootloader mode detected -- connecting...";
    try {
      await connectWithEsploader(bootPort);
    } catch (err) {
      status.textContent = "";
      button.style.display = "";
      button.disabled = false;
      renderError(err, renderConnect);
    }
  }

  // Only hands off to esptool-js once the port is confirmed to already be
  // enumerating under the board's bootloader USB id; otherwise routes
  // through bootloaderAssist() to wait for and confirm that positively via
  // getPorts() first.
  async function connectAndFlash(port) {
    if (matchesBootloaderUsbId(port)) {
      try {
        await closeQuietly(port);
        await connectWithEsploader(port);
        return;
      } catch (err) {
        console.log(`[flasher] connect failed: ${errMsg(err)}`);
      }
    }
    await bootloaderAssist(port);
  }

  // Picker, then the same confirm-and-handoff path as a paired port
  async function pickPortAndFlash() {
    button.disabled = true;
    status.textContent = "Waiting for port selection...";
    try {
      const { port } = await connectSerial({ skipOpen: true });
      await connectAndFlash(port);
    } catch (err) {
      button.disabled = false;
      status.textContent = "";
      renderError(err, renderConnect);
    }
  }

  if (earlyPort) {
    connectAndFlash(earlyPort);
  } else {
    button.style.display = "";
    button.addEventListener("click", pickPortAndFlash);
  }
}

// The single source of truth for "what's happening right now": reports
// `label` to the status line, then runs fn, prefixing any error with the
// same label. One string per action instead of a status update and an
// error-label kept in sync by hand.
async function step(onStatus, label, fn) {
  onStatus(`${label}...`);
  try {
    return await fn();
  } catch (err) {
    const msg = errMsg(err);
    throw new Error(`${label}: ${msg}`);
  }
}

// Gives up esptool's hold so a retry can reopen the port. The paired
// SerialPort is kept, so no picker prompt.
async function releaseConnection() {
  const previous = connection;
  connection = null;
  if (previous && previous.transport) await previous.transport.disconnect().catch(() => {});
}

// Chunk, retry, then reopen the port. One packet lost mid-read otherwise costs
// the whole partition, which on a lossy host is close to a coin flip.
const FLASH_READ_CHUNK_SIZE = 0x40 * 0x1000; // 256 KB, matching esp32tool
const FLASH_READ_ATTEMPTS = 3; // per chunk, before reopening the port
const FLASH_READ_MAX_RECOVERIES = 4; // reopens per read, before giving up

// Reopens the port with the stub still running, so a read can resume mid-
// partition. The caller must re-issue whatever was in flight.
async function reopenSerialPort() {
  const esploader = connection.esploader;
  const transport = esploader.transport;
  await transport.disconnect().catch(() => {});
  await new Promise((resolve) => setTimeout(resolve, 100));
  await transport.connect(esploader.baudrate, esploader.serialOptions || {});
  if (transport.flushInput) transport.flushInput();
}

async function readFlashChunked(offset, size, onProgress, onNotice) {
  const report = (fraction) => {
    if (onProgress) onProgress(Math.min(1, Math.max(0, fraction)));
  };
  // Surfaces retries, which otherwise look like a hang
  const notice = (text) => {
    if (onNotice) onNotice(text);
  };

  const out = new Uint8Array(size);
  let done = 0;
  let recoveries = 0;

  while (done < size) {
    const want = Math.min(FLASH_READ_CHUNK_SIZE, size - done);
    const base = done;
    let chunk = null;
    let lastErr = null;

    for (let attempt = 1; attempt <= FLASH_READ_ATTEMPTS && chunk === null; attempt++) {
      const startedAt = performance.now();
      try {
        const transport = connection.esploader.transport;
        // A failed attempt's leftovers would head this one's reply
        if (attempt > 1 && transport && transport.flushInput) transport.flushInput();
        chunk = await connection.esploader.readFlash(offset + base, want, (_packet, read, total) =>
          report((base + (total > 0 ? read / total : 1) * want) / size)
        );
      } catch (err) {
        lastErr = err;
        console.warn(
          `[backup] Read of ${want}B at 0x${(offset + base).toString(16)} failed after ` +
            `${Math.round(performance.now() - startedAt)}ms (attempt ${attempt}/${FLASH_READ_ATTEMPTS})`,
          err
        );
        if (attempt < FLASH_READ_ATTEMPTS) {
          notice(`Read interrupted, retrying (${attempt + 1} of ${FLASH_READ_ATTEMPTS})...`);
        }
      }
    }

    if (chunk === null) {
      if (recoveries >= FLASH_READ_MAX_RECOVERIES) throw lastErr;
      recoveries++;
      console.warn(
        `[backup] Retries exhausted at 0x${(offset + base).toString(16)}, reopening the port ` +
          `(recovery ${recoveries}/${FLASH_READ_MAX_RECOVERIES})`
      );
      notice(`Reconnecting to the device (${recoveries} of ${FLASH_READ_MAX_RECOVERIES})...`);
      await reopenSerialPort();
      continue; // same chunk, fresh link -- resume rather than restart
    }

    out.set(chunk.subarray(0, want), base);
    done += want;
  }

  report(1);
  return out;
}

async function runFlash(onProgress, onStatus, onTitle) {
  const board = currentBoard();
  const variant = currentVariant();
  const firmware = await step(onStatus, "Loading firmware", () => loadLocalBinary(variant.firmwareFile));
  await step(onStatus, "Verifying firmware", () => verifySha256(firmware, variant.firmwareShaFile, variant.firmwareFile));

  const fileArray = [];
  let eraseAll = false;

  // Slot B gets a blank 0xFF buffer, leaving the bootloader exactly one valid
  // image without having to touch otadata.
  const blankSlotB = blankBuffer(hex(board.offsets.appMaxSize));

  // Loaded on both paths for the restore geometry; only written on erase
  const partitions = await step(onStatus, "Loading partition table", () => loadLocalBinary(board.partitionsFile));

  // Reads the table and filesystem before anything overwrites either. Reports
  // "ok" or "absent"; a failed read aborts rather than passing for empty.
  onTitle("Reading");
  const spiffsBackup = await step(onStatus, "Investigating existing data structure", async () => {
    let oldPartitions;
    let oldFs;
    try {
      const table = await connection.esploader.readFlash(0x8000, 0x1000);
      oldPartitions = parsePartitionTable(table);
      oldFs = findFilesystemPartition(oldPartitions);
    } catch (err) {
      throw new Error(
        `could not read the device's partition table (${errMsg(err)}). ` +
          `Nothing has been written or erased -- check the USB cable and port, then try again`
      );
    }

    // A blank chip legitimately has nothing to read
    if (!oldFs) return { status: "absent", partitions: oldPartitions };

    try {
      const data = await readFlashChunked(oldFs.offset, oldFs.size, onProgress, onStatus);
      return { status: "ok", partitions: oldPartitions, ...oldFs, data };
    } catch (err) {
      throw new Error(
        `this device has existing data that could not be read back (${errMsg(err)}). ` +
          `Nothing has been written or erased, so nothing has been lost -- try again, and if it keeps ` +
          `failing try a different USB cable or port`
      );
    }
  });
  // Reset so the write phase starts its own 0-100%
  onProgress(0);

  // The filesystem is the evidence, not how the device enumerated. Parsed once
  // here and handed to the restore step.
  let backupFiles = [];
  // Reused by the restore step to size a raw copy
  let backupUsedSize = 0;
  if (spiffsBackup.status === "ok" && spiffsBackup.subtype === PARTITION_SUBTYPE_SPIFFS) {
    try {
      backupUsedSize = spiffsUsedSize(spiffsBackup.data);
      if (backupUsedSize > 0) backupFiles = readSpiffsFiles(spiffsBackup.data.subarray(0, backupUsedSize));
    } catch (err) {
      // Unparsable means nothing worth keeping
      console.warn("[flash] Could not parse the backed-up filesystem:", err);
    }
  }
  const isMeshCore = looksLikeMeshCore(backupFiles);
  if (spiffsBackup.status !== "ok") {
    console.info("[flash] No filesystem found on this device, treating it as blank");
  } else if (isMeshCore) {
    console.info(`[flash] MeshCore device: ${backupFiles.length} file(s) found, existing data will be kept`);
  } else {
    const seen = backupFiles.map((f) => f.name).slice(0, 6).join(", ") || "nothing readable";
    console.warn(`[flash] Not a MeshCore device (${seen}), existing data will not be kept`);
  }

  // Reusing the device's own table only works while it still matches ours. A
  // changed layout writes the whole thing instead -- the data still survives,
  // rebuilt for the new partition size by the restore step below.
  const newPartitions = parsePartitionTable(partitions);
  const layoutChanged =
    Array.isArray(spiffsBackup.partitions) &&
    !partitionTablesMatch(spiffsBackup.partitions, newPartitions);
  if (layoutChanged) {
    console.info("[flash] Partition layout differs from the device's, writing the full layout instead of app images only");
  }

  // Nothing being kept, so nothing to lose by rewriting the layout
  const foreignInstall = spiffsBackup.status === "ok" && !isMeshCore;
  if (foreignInstall) {
    console.info("[flash] Writing the full layout, since nothing on this device is being preserved");
  }

  // How much to write, decided from the chip and never from how it enumerated
  // -- on >=1.17 a running node and the ROM bootloader share a VID:PID, so
  // deriving this from the port erased live devices. Skip the layout only on
  // positive evidence: a MeshCore filesystem we could read, on a table still
  // matching ours. This is not the same question as whether the user's data
  // survives -- that is the restore step below, which runs off the backup
  // regardless of what gets written here.
  const appSlotsOnly = spiffsBackup.status === "ok" && isMeshCore && !layoutChanged;
  // Drives the closing screen's wording, and is the honest record of what ran
  state.mode = appSlotsOnly ? "update" : "new";
  console.info(
    `[flash] ${appSlotsOnly ? "Writing app slots only" : "Writing the full layout with a full erase"} ` +
      `(filesystem ${spiffsBackup.status}, meshcore ${isMeshCore}, layout ${layoutChanged ? "changed" : "unchanged"})`
  );

  if (!appSlotsOnly) {
    const bootloader = await step(onStatus, "Loading bootloader", () => loadLocalBinary(board.bootloaderFile));
    const bootApp0 = await step(onStatus, "Preparing boot selector", () => loadLocalBinary(board.bootApp0));

    fileArray.push({ data: bootloader, address: hex(board.offsets.bootloader) });
    fileArray.push({ data: partitions, address: hex(board.offsets.partitions) });
    fileArray.push({ data: bootApp0, address: hex(board.offsets.otadata) });
    fileArray.push({ data: firmware, address: hex(board.offsets.app0) });
    fileArray.push({ data: blankSlotB, address: hex(board.offsets.app1) });
    eraseAll = true;
  } else {
    fileArray.push({ data: firmware, address: hex(board.offsets.app0) });
    fileArray.push({ data: blankSlotB, address: hex(board.offsets.app1) });
  }

  // Progress is reported per file, so weight each by its byte size to map a
  // multi-file write onto one continuous bar.
  const totalBytes = fileArray.reduce((sum, f) => sum + f.data.length, 0);
  const fileByteOffsets = [];
  {
    let offset = 0;
    for (const f of fileArray) {
      fileByteOffsets.push(offset);
      offset += f.data.length;
    }
  }

  // The eraseAll erase runs before any reportProgress call and can take most
  // of a minute, so label it up front and flip once data starts moving.
  // Slot A then Slot B are always the last two fileArray entries.
  const app0Index = fileArray.length - 2;
  const app1Index = fileArray.length - 1;

  // Slot B is uniformly 0xFF, so its written/total hits ~1 as soon as the tiny
  // compressed payload is acked, long before the device has written it. No
  // trustworthy per-byte signal exists, so replay Slot A's pacing instead,
  // capped just under full until writeFlash() resolves.
  const app0Timeline = []; // { elapsedMs, frac } samples from Slot A's real progress
  let app0StartTime = null;
  let app1SimTimer = null;

  let erasingStatusShown = eraseAll;
  onTitle(eraseAll ? "Erasing" : "Flashing");
  await step(onStatus, eraseAll ? "Erasing flash (this can take up to a minute)" : "Writing firmware", () =>
    connection.esploader.writeFlash({
      ...FLASH_WRITE_OPTS,
      fileArray,
      eraseAll,
      reportProgress: (fileIndex, written, total) => {
        if (erasingStatusShown) {
          onStatus("Writing firmware...");
          onTitle("Flashing");
          erasingStatusShown = false;
        }

        const fileBytes = fileArray[fileIndex].data.length;
        const baseFraction = fileByteOffsets[fileIndex] / totalBytes;
        const weight = fileBytes / totalBytes;

        if (fileIndex === app0Index) {
          if (app0StartTime === null) app0StartTime = performance.now();
          const frac = total > 0 ? written / total : 1;
          app0Timeline.push({ elapsedMs: performance.now() - app0StartTime, frac });
          onProgress(baseFraction + frac * weight);
          return;
        }

        if (fileIndex === app1Index) {
          if (!app1SimTimer) {
            const timeline = app0Timeline.length > 0 ? app0Timeline : [{ elapsedMs: 0, frac: 1 }];
            const duration = timeline[timeline.length - 1].elapsedMs || 1;
            const simStart = performance.now();
            let idx = 0;
            app1SimTimer = setInterval(() => {
              const elapsed = performance.now() - simStart;
              while (idx < timeline.length - 1 && timeline[idx + 1].elapsedMs <= elapsed) idx++;
              const frac = Math.min(timeline[idx].frac, 0.98);
              onProgress(baseFraction + frac * weight);
              if (elapsed >= duration) {
                clearInterval(app1SimTimer);
                app1SimTimer = null;
              }
            }, 100);
          }
          return;
        }

        // bootloader/partitions/boot selector -- small, reported as-is.
        onProgress(baseFraction + (total > 0 ? written / total : 1) * weight);
      },
    })
  );

  // Genuinely done -- stop the simulation and show true 100%
  if (app1SimTimer) {
    clearInterval(app1SimTimer);
    app1SimTimer = null;
  }
  onProgress(1);

  // Same size copies the used bytes; a different size has to rebuild, since
  // each block's lookup magic derives from the partition's block count.
  // Best-effort: never blocks reaching "Done".
  const backup = spiffsBackup.status === "ok" && isMeshCore ? spiffsBackup : null;
  if (backup && backupFiles.length > 0) {
    onTitle("Restoring");
    await step(onStatus, "Restoring existing data", async () => {
      try {
        const newFs = findFilesystemPartition(newPartitions);
        if (!newFs) {
          console.warn("[restore] New firmware has no filesystem partition, skipping restore");
          return;
        }

        const writeFs = (data) =>
          connection.esploader.writeFlash({
            ...FLASH_WRITE_OPTS,
            fileArray: [{ data, address: hex(newFs.offset) }],
            eraseAll: false,
          });

        const isSpiffs = backup.subtype === PARTITION_SUBTYPE_SPIFFS;
        if (backup.size !== newFs.size) {
          if (!isSpiffs) {
            console.warn("[restore] Partition resized but this filesystem isn't SPIFFS, skipping restore");
            return;
          }
          // Whole partition, not just the used part: the tail's magic is what
          // marks the rest of the filesystem formatted. Compresses to nothing.
          await writeFs(buildSpiffsImage(backupFiles, newFs.size));
          console.info(`[restore] Partition resized, rebuilt ${backupFiles.length} file(s) for ${newFs.size}B`);
          return;
        }

        const usedSize = isSpiffs ? backupUsedSize : backup.data.length;
        if (usedSize === 0) {
          console.warn("[restore] Backup has no data in use, nothing to restore");
          return;
        }
        // Same offset, same size, nothing erased -- what's on the device is
        // already what would be written back.
        if (!eraseAll && backup.offset === newFs.offset) {
          console.info("[restore] Filesystem partition unchanged, skipping restore");
          return;
        }
        await writeFs(backup.data.subarray(0, usedSize));
      } catch (err) {
        console.warn("[restore] Failed to restore filesystem backup:", err);
      }
    });
  }

  // hard_reset never re-samples the boot strapping pin on these parts
  // (https://github.com/espressif/esp-idf/issues/13287). Needs the
  // chip-reset-on-watchdog bits, undocumented in the S3 TRM. From esptool's
  // targets/esp32s3.py and esp32c3.py.
  const WATCHDOG_RESET_REGS = {
    "ESP32-S3": { WDTCONFIG0: 0x60008098, WDTCONFIG1: 0x6000809c, WDTWPROTECT: 0x600080b0, OPTION1: 0x6000812c },
    "ESP32-C3": { WDTCONFIG0: 0x60008090, WDTCONFIG1: 0x60008094, WDTWPROTECT: 0x600080a8, OPTION1: 0x600080f4 },
  };
  const RTC_CNTL_WDT_WKEY = 0x50d83aa1;
  // (1 << 31) | (5 << 28) | (1 << 8) | 2 -- WDT_CHIP_RESET_EN, WDT_CHIP_RESET_WIDTH=5,
  // plus a stage-0 timeout action of "reset system" and WDT enable.
  const WDTCONFIG0_CHIP_RESET_VALUE = 0xd0000102;

  await step(onStatus, "Resetting device", async () => {
    const regs = WATCHDOG_RESET_REGS[connection.esploader.chip.CHIP_NAME];
    if (!regs) {
      await connection.esploader.after("hard_reset");
      return;
    }
    try {
      const { esploader } = connection;
      // Clear FORCE_DOWNLOAD_BOOT (bit 0) so it can't re-enter download mode
      await esploader.writeReg(regs.OPTION1, 0, 1);
      await esploader.writeReg(regs.WDTWPROTECT, RTC_CNTL_WDT_WKEY); // unlock
      await esploader.writeReg(regs.WDTCONFIG1, 2000); // WDT timeout
      await esploader.writeReg(regs.WDTCONFIG0, WDTCONFIG0_CHIP_RESET_VALUE); // enable WDT w/ chip reset
      await esploader.writeReg(regs.WDTWPROTECT, 0); // lock
      await new Promise((resolve) => setTimeout(resolve, 500)); // wait for reset to take effect
    } catch (err) {
      console.error("[esptool] Watchdog chip-reset sequence failed, falling back to hard_reset:", err);
      await connection.esploader.after("hard_reset");
    }
  });
}

// Keyed by the titles onTitle() sets, so the description tracks the phase
const PHASE_DESC = {
  Reading: "Retrieving current flash data.",
  Erasing: "Clearing the device.",
  Flashing: "Writing new firmware image to your device.",
  Restoring: "Writing saved device configuration.",
};

function renderFlashing() {
  render(`
    <h2 class="step-title" id="title">Flashing</h2>
    <p class="step-desc" id="desc">Retrieving current flash data.</p>
    <div class="progress-track"><div class="progress-fill" id="fill"></div></div>
    ${state.boardIcon ? `<img class="flashing-icon" src="${state.boardIcon}" alt="" />` : ""}
    <p class="status-text" id="status">Starting...</p>
  `);

  const title = wizard.querySelector("#title");
  const desc = wizard.querySelector("#desc");
  const fill = wizard.querySelector("#fill");
  const status = wizard.querySelector("#status");

  runFlash(
    (fraction) => {
      fill.style.width = `${Math.round(fraction * 100)}%`;
    },
    (text) => {
      status.textContent = text;
    },
    (text) => {
      title.textContent = text;
      if (PHASE_DESC[text]) desc.textContent = PHASE_DESC[text];
    }
  )
    .then(renderPostFlashConfig)
    .catch(async (err) => {
      // "Try again" reopens the port, so hand it back first
      await releaseConnection();
      renderError(err, renderConnect);
    });
}

// Replaces the reboot screen while the command sequence runs
function renderFinalizing() {
  render(`
    <h2 class="step-title">Finalizing</h2>
    <p class="step-desc">Finalizing your device's configuration...</p>
    <div class="progress-track"><div class="progress-fill" id="fill"></div></div>
    ${state.boardIcon ? `<img class="flashing-icon" src="${state.boardIcon}" alt="" />` : ""}
    <p class="status-text" id="status">Connecting to the device...</p>
  `);
}

// Gates "Done" on a confirmed CLI-mode reconnect, whether or not there are
// commands to send. A failed send is a soft warning, not an error.
async function renderPostFlashConfig() {
  const board = currentBoard();
  render(`
    <h2 class="step-title">Device reset</h2>
    <p class="step-desc">${board.postFlashNote}</p>
    <div class="progress-track" style="visibility: hidden;"></div>
    ${state.boardIcon2 ? `<img class="flashing-icon" src="${state.boardIcon2}" alt="" />` : ""}
    <p class="status-text" id="status">Waiting for device to reboot...</p>
  `);

  let status = wizard.querySelector("#status");
  // Variant commands first, so a location can override them. CLI traffic during
  // the post-flash keypair generation is suspected of corrupting it, hence the
  // longer timeout on the first command.
  const commands = [...(currentVariant().postFlashCommands ?? []), ...(await loadMemberCommands())];
  const previousPort = connection && connection.port;
  const previousTransport = connection && connection.transport;
  connection = null;

  let port;
  try {
    // esptool-js's Transport keeps its own reader locked on this port the
    // entire time it's connected -- release that lock first, or our own
    // read attempt below throws immediately (stream already locked) and we
    // silently never actually watch for the real disconnect at all.
    if (previousTransport) await previousTransport.disconnect().catch(() => {});

    // Wait for the real drop rather than guessing at a timeout
    if (previousPort) await waitForPortDrop(previousPort, CLI_BAUD_RATE);

    status.textContent = "Reconnecting to device...";
    const attempts = 20;
    let lastErr;
    for (let i = 0; i < attempts; i++) {
      try {
        ({ port } = await connectSerial({ preferredPort: previousPort, baudRate: CLI_BAUD_RATE }));
        lastErr = null;
        break;
      } catch (err) {
        lastErr = err;
        await new Promise((resolve) => setTimeout(resolve, 500));
      }
    }
    if (lastErr) throw lastErr;

    if (commands.length > 0) {
      // The RST press has already happened by now
      renderFinalizing();
      status = wizard.querySelector("#status");
      // CLI command send and progress bar
      const fill = wizard.querySelector("#fill");
      const cli = createCliSession(port);
      try {
        for (let i = 0; i < commands.length; i++) {
          const cmd = commands[i];
          status.textContent = `Applying settings (${i + 1} of ${commands.length})...`;
          // The first command may land while the device is still generating
          // its identity keypair after a fresh erase, which can take a while
          // -- give it a longer window than the rest of the sequence needs
          // once the CLI is confirmed up and responding.
          await cli.sendCommand(cmd, i === 0 ? { timeoutMs: 30000 } : undefined);
          if (fill) fill.style.width = `${Math.round(((i + 1) / commands.length) * 100)}%`;
        }
      } finally {
        await cli.close();
      }

      // Raw, not sendCommand() -- the device is too busy rebooting to reply
      status.textContent = "Restarting device...";
      try {
        const writer = port.writable.getWriter();
        await writer.write(new TextEncoder().encode("reboot\r"));
        writer.releaseLock();
      } catch (err) {
        console.warn("[config] Could not send reboot:", err);
      }
    }
    await closeQuietly(port);
    renderDone();
  } catch (err) {
    await closeQuietly(port);
    renderDone(
      "Firmware flashed successfully, but automatic configuration could not be completed -- " +
        "connect manually via the Serial Console to finish setup."
    );
  }
}

function renderDone(configWarning) {
  const updateNote =
    state.mode === "update"
      ? `<p class="step-desc" style="text-align: center; opacity: 0.7;">
           Use the Serial Console button above to check on the device if needed.
         </p>`
      : "";
  render(`
    <h2 class="step-title">Done</h2>
    <div class="success-box">Firmware written successfully.</div>
    ${
      state.boardIcon
        ? `<div class="done-icon-stack">
             <img class="flashing-icon" src="${state.boardIcon}" alt="" />
             <img class="done-icon-checkmark" src="icons/checkmark.png" alt="" />
           </div>`
        : ""
    }
    ${configWarning ? `<div class="warning-box">${configWarning}</div>` : ""}
    ${updateNote}
    <div class="actions" style="justify-content: center;">
      <button class="btn btn-primary" id="restart">Flash another device</button>
    </div>
  `);
  wizard.querySelector("#restart").addEventListener("click", () => {
    connection = null;
    earlyPort = null;
    renderPlugIn();
  });
}

function renderError(err, retryStep) {
  render(`
    <h2 class="step-title">Something went wrong</h2>
    <div class="error-box">${errMsg(err)}</div>
    <div class="actions">
      <button class="btn btn-primary" id="retry">Try again</button>
    </div>
  `);
  wizard.querySelector("#retry").addEventListener("click", retryStep);
}

function formatAgo(date) {
  let remaining = Math.max(0, Math.floor((Date.now() - date.getTime()) / 1000));
  const days = Math.floor(remaining / 86400);
  remaining -= days * 86400;
  const hours = Math.floor(remaining / 3600);
  remaining -= hours * 3600;
  const mins = Math.floor(remaining / 60);
  const secs = remaining - mins * 60;

  const unit = (n, label) => `${n} ${label}${n === 1 ? "" : "s"}`;
  const parts = [];
  if (days) parts.push(unit(days, "day"));
  if (days || hours) parts.push(unit(hours, "hr"));
  if (days || hours || mins) parts.push(unit(mins, "min"));
  parts.push(unit(secs, "sec"));

  return `${parts.join(" ")} ago`;
}

// Flags a stale deploy (usually CDN caching) by comparing the embedded
// build-stamp against the latest commit touching pages/flasher. Ancestry, not
// equality: the deployed sha is often later than that commit and contains it.
async function loadBuildInfo() {
  const el = document.getElementById("build-info");
  try {
    const [stampRes, apiRes] = await Promise.all([
      fetch("./build-stamp.json", { cache: "no-store" }),
      fetch(`https://api.github.com/repos/${REPO}/commits?path=pages/flasher&per_page=1`),
    ]);
    if (!apiRes.ok) return;
    const [commit] = await apiRes.json();
    if (!commit) return;
    const date = new Date(commit.commit.committer.date);
    const sha = commit.sha.slice(0, 7);

    if (!stampRes.ok) {
      // No stamp -- e.g. a deploy from before this feature existed.
      el.textContent = `Page last updated ${formatAgo(date)} (commit ${sha})`;
      return;
    }

    const stamp = await stampRes.json();
    if (stamp.sha === commit.sha) {
      el.textContent = `Page is up to date (commit ${sha}, ${formatAgo(date)})`;
      return;
    }

    // Differs -- check whether stamp.sha already contains that commit
    const cmpRes = await fetch(`https://api.github.com/repos/${REPO}/compare/${commit.sha}...${stamp.sha}`);
    const cmp = cmpRes.ok ? await cmpRes.json() : null;
    if (cmp && (cmp.status === "ahead" || cmp.status === "identical")) {
      el.textContent = `Page is up to date (commit ${sha}, ${formatAgo(date)})`;
    } else {
      el.textContent = `This page is stale (built from ${stamp.sha.slice(0, 7)}, latest is ${sha}) -- try a hard refresh`;
      el.classList.add("stale");
    }
  } catch {
    // best-effort only -- leave the footer line blank if this fails
  }
}

// ---- Boot --------------------------------------------------------------

loadBoards()
  .then(loadBoardDisplay)
  .then(loadMemberConfigUrls)
  .then(renderPlugIn)
  .catch((err) => renderError(err, renderPlugIn));

loadBuildInfo();
