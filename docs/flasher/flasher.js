const REPO = "HeyVern/meshcore-hotspot-ota";
// Vendored rather than loaded from a CDN (e.g. esm.sh): esm.sh re-bundles the package's raw
// source itself, including a separate dynamic import of each chip's stub-loader JSON (large
// embedded base64 blobs) -- that re-bundling step was corrupting the ESP32-S3 stub's base64 and
// throwing "atob ... not correctly encoded" during flashing. This is esptool-js's own official
// prebuilt browser bundle straight from the npm package (bundle.js), with every chip's stub data
// already compiled in -- no separate CDN transform step left to get it wrong.
const ESPTOOL_JS_URL = "./vendor/esptool-js/bundle.js";

const wizard = document.getElementById("wizard");

const state = {
  mode: null,      // "new" | "update"
  boardId: null,
  variantId: null,
  slot: null,      // "A" | "B"
};

let boards = null;
let esploaderApi = null;
let connection = null; // { port, transport, esploader }

function hex(v) {
  return Number(v);
}

function currentBoard() {
  return boards[state.boardId];
}

function currentVariant() {
  return currentBoard().variants[state.variantId];
}

async function loadBoards() {
  const res = await fetch("./boards.json");
  if (!res.ok) throw new Error(`Could not load boards.json (${res.status})`);
  boards = await res.json();
}

// Firmware is vendored into docs/flasher/<board>/<variant>/ by CI on every successful build,
// same as bootloader.bin/partitions.bin/boot_app0.bin already were -- GitHub Release assets are
// served from a host (release-assets.githubusercontent.com) that sends no CORS headers at all, so
// this page's fetch() can never read them cross-origin. Same-origin avoids that entirely.
async function verifySha256(data, shaPath, label) {
  const res = await fetch(`./${shaPath}`);
  if (!res.ok) return; // no sidecar committed for this build yet -- proceed unverified
  const expected = (await res.text()).trim().toLowerCase();
  const digest = await crypto.subtle.digest("SHA-256", data);
  const actual = [...new Uint8Array(digest)].map((b) => b.toString(16).padStart(2, "0")).join("");
  if (actual !== expected) {
    throw new Error(`${label} failed checksum verification (expected ${expected}, got ${actual}).`);
  }
}

async function loadLocalBinary(path) {
  const res = await fetch(`./${path}`);
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

function renderIntro() {
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
    <h2 class="step-title">What are you flashing?</h2>
    <p class="step-desc">Choose whichever matches your situation.</p>
    <div class="tiles">
      <button class="tile" data-mode="new">
        <strong>New device</strong>
        <small>Blank board, or one that's bricked. Fully erases and writes a complete image.</small>
      </button>
      <button class="tile" data-mode="update">
        <strong>Update existing device</strong>
        <small>Already running MeshCore. Writes new firmware into a chosen OTA slot, no erase.</small>
      </button>
    </div>
  `);

  wizard.querySelectorAll(".tile").forEach((el) => {
    el.addEventListener("click", () => {
      state.mode = el.dataset.mode;
      renderBoard();
    });
  });
}

function renderBoard() {
  const entries = Object.entries(boards);
  render(`
    <h2 class="step-title">Select your hardware</h2>
    <p class="step-desc">Only boards this repo builds firmware for are listed.</p>
    <div class="tiles">
      ${entries
        .map(([id, b]) => `<button class="tile" data-board="${id}"><strong>${b.label}</strong></button>`)
        .join("")}
    </div>
    <div class="actions" style="justify-content: flex-start;">
      <button class="btn btn-secondary" id="back">Back</button>
    </div>
  `);

  wizard.querySelectorAll("[data-board]").forEach((el) => {
    el.addEventListener("click", () => {
      state.boardId = el.dataset.board;
      renderVariant();
    });
  });
  wizard.querySelector("#back").addEventListener("click", renderIntro);
}

function renderVariant() {
  const board = currentBoard();
  const entries = Object.entries(board.variants);
  render(`
    <h2 class="step-title">Select firmware</h2>
    <p class="step-desc">The latest published release for the chosen role will be used.</p>
    <div class="tiles">
      ${entries
        .map(([id, v]) => `<button class="tile" data-variant="${id}"><strong>${v.label}</strong></button>`)
        .join("")}
    </div>
    <div class="actions" style="justify-content: flex-start;">
      <button class="btn btn-secondary" id="back">Back</button>
    </div>
  `);

  wizard.querySelectorAll("[data-variant]").forEach((el) => {
    el.addEventListener("click", () => {
      state.variantId = el.dataset.variant;
      if (state.mode === "update") {
        renderSlot();
      } else {
        renderConnect();
      }
    });
  });
  wizard.querySelector("#back").addEventListener("click", renderBoard);
}

function renderSlot() {
  render(`
    <h2 class="step-title">Which OTA slot?</h2>
    <p class="step-desc">
      The device keeps two firmware slots (A/B). Pick the one you want to
      overwrite -- this does not change which slot the device boots into;
      use <code>set ota.active &lt;A|B&gt;</code> on-device for that.
    </p>
    <div class="tiles">
      <button class="tile" data-slot="A"><strong>Slot A</strong></button>
      <button class="tile" data-slot="B"><strong>Slot B</strong></button>
    </div>
    <div class="actions" style="justify-content: flex-start;">
      <button class="btn btn-secondary" id="back">Back</button>
    </div>
  `);

  wizard.querySelectorAll("[data-slot]").forEach((el) => {
    el.addEventListener("click", () => {
      state.slot = el.dataset.slot;
      renderConnect();
    });
  });
  wizard.querySelector("#back").addEventListener("click", renderVariant);
}

function renderConnect() {
  const board = currentBoard();
  render(`
    <h2 class="step-title">Connect your board</h2>
    <p class="step-desc">${board.connectNote}</p>
    <div class="actions" style="justify-content: space-between;">
      <button class="btn btn-secondary" id="back">Back</button>
      <button class="btn btn-primary" id="connect">Connect via USB</button>
    </div>
    <p class="status-text" id="status"></p>
  `);

  wizard.querySelector("#back").addEventListener("click", () => {
    state.mode === "update" ? renderSlot() : renderVariant();
  });

  wizard.querySelector("#connect").addEventListener("click", async () => {
    const status = wizard.querySelector("#status");
    const button = wizard.querySelector("#connect");
    button.disabled = true;
    status.textContent = "Waiting for port selection...";
    try {
      if (!esploaderApi) esploaderApi = await import(ESPTOOL_JS_URL);
      const { ESPLoader, Transport } = esploaderApi;
      const port = await navigator.serial.requestPort();
      const transport = new Transport(port, true);
      const terminal = {
        clean() {},
        writeLine(line) {
          status.textContent = line;
        },
        write() {},
      };
      const esploader = new ESPLoader({ transport, baudrate: 115200, terminal, debugLogging: false });
      status.textContent = "Detecting chip...";
      const chip = await esploader.main();
      connection = { port, transport, esploader };
      status.textContent = `Connected: ${chip}`;
      renderFlashing();
    } catch (err) {
      status.textContent = "";
      button.disabled = false;
      renderError(err, renderConnect);
    }
  });
}

// Wraps a step so a failure names the step it happened in -- "Failed to fetch" alone doesn't say
// which of several fetch() calls in this function failed, and the status line only ever shows the
// most recent line (see onStatus/renderFlashing's log), so a fast failure can blow past several
// steps before the user can read any of them.
async function step(label, fn) {
  try {
    return await fn();
  } catch (err) {
    const msg = (err && err.message) || String(err);
    throw new Error(`${label}: ${msg}`);
  }
}

async function runFlash(onProgress, onStatus) {
  const board = currentBoard();
  const variant = currentVariant();
  onStatus("Loading firmware...");
  const firmware = await step("Loading firmware", () => loadLocalBinary(variant.firmwareFile));
  await step("Verifying firmware", () => verifySha256(firmware, variant.firmwareShaFile, variant.firmwareFile));

  const fileArray = [];
  let eraseAll = false;

  if (state.mode === "new") {
    onStatus("Loading bootloader...");
    const bootloader = await step("Loading bootloader", () => loadLocalBinary(board.bootloaderFile));
    onStatus("Loading partition table...");
    const partitions = await step("Loading partition table", () => loadLocalBinary(board.partitionsFile));
    onStatus("Preparing boot selector...");
    const bootApp0 = await step("Preparing boot selector", () => loadLocalBinary(board.bootApp0));

    fileArray.push({ data: bootloader, address: hex(board.offsets.bootloader) });
    fileArray.push({ data: partitions, address: hex(board.offsets.partitions) });
    fileArray.push({ data: bootApp0, address: hex(board.offsets.otadata) });
    fileArray.push({ data: firmware, address: hex(board.offsets.app0) });
    eraseAll = true;
  } else {
    const slotOffset = state.slot === "A" ? board.offsets.app0 : board.offsets.app1;
    fileArray.push({ data: firmware, address: hex(slotOffset) });
  }

  onStatus("Flashing...");
  await step("Flashing", () =>
    connection.esploader.writeFlash({
      fileArray,
      flashMode: board.flashMode,
      flashFreq: board.flashFreq,
      flashSize: board.flashSize,
      eraseAll,
      compress: true,
      reportProgress: (fileIndex, written, total) => onProgress(written / total),
    })
  );
}

function renderFlashing() {
  render(`
    <h2 class="step-title">Flashing</h2>
    <div class="progress-track"><div class="progress-fill" id="fill"></div></div>
    <p class="status-text" id="status">Starting...</p>
    <ul class="step-log" id="steplog"></ul>
  `);

  const fill = wizard.querySelector("#fill");
  const status = wizard.querySelector("#status");
  const log = wizard.querySelector("#steplog");

  runFlash(
    (fraction) => {
      fill.style.width = `${Math.round(fraction * 100)}%`;
    },
    (text) => {
      // Appended, not overwritten -- a fast run of several steps otherwise blows past each status
      // line before it's readable, leaving only the last one visible if something then fails.
      status.textContent = text;
      const li = document.createElement("li");
      li.textContent = text;
      log.appendChild(li);
    }
  )
    .then(renderDone)
    .catch((err) => renderError(err, renderConnect));
}

function renderDone() {
  const board = currentBoard();
  const updateNote =
    state.mode === "update"
      ? `<p class="step-desc">
           The device will run the new image on probation for about 90
           seconds before confirming it. Use <code>get ota.active</code> to
           check its state, and <code>set ota.active ${state.slot}</code> if
           you need to boot into this slot.
         </p>`
      : "";
  render(`
    <h2 class="step-title">Done</h2>
    <div class="success-box">Firmware written successfully.</div>
    <p class="step-desc">${board.postFlashNote}</p>
    ${updateNote}
    <div class="actions">
      <button class="btn btn-primary" id="restart">Flash another device</button>
    </div>
  `);
  wizard.querySelector("#restart").addEventListener("click", () => {
    connection = null;
    renderIntro();
  });
}

function renderError(err, retryStep) {
  render(`
    <h2 class="step-title">Something went wrong</h2>
    <div class="error-box">${(err && err.message) || String(err)}</div>
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

async function loadBuildInfo() {
  const el = document.getElementById("build-info");
  try {
    const res = await fetch(`https://api.github.com/repos/${REPO}/commits?path=docs/flasher&per_page=1`);
    if (!res.ok) return;
    const [commit] = await res.json();
    if (!commit) return;
    const date = new Date(commit.commit.committer.date);
    const sha = commit.sha.slice(0, 7);
    el.textContent = `Page last updated ${formatAgo(date)} (commit ${sha})`;
  } catch {
    // best-effort only -- leave the footer line blank if this fails
  }
}

// ---- Boot --------------------------------------------------------------

loadBoards()
  .then(renderIntro)
  .catch((err) => renderError(err, renderIntro));

loadBuildInfo();
