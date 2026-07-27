const REPO = "HeyVern/meshcore-hotspot-ota";
const RELEASES_API = `https://api.github.com/repos/${REPO}/releases`;
const ESPTOOL_JS_URL = "https://esm.sh/esptool-js@0.6.0";

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

async function fetchLatestRelease(assetBasename) {
  const res = await fetch(RELEASES_API);
  if (!res.ok) throw new Error(`GitHub API error (${res.status}) while looking up releases`);
  const releases = await res.json();
  const binPattern = new RegExp(`^${assetBasename}-v[0-9.]+\\.bin$`);

  for (const release of releases) {
    if (release.draft) continue;
    const bin = release.assets.find((a) => binPattern.test(a.name));
    if (!bin) continue;
    const base = bin.name.replace(/\.bin$/, "");
    return {
      tag: release.tag_name,
      bin,
      sha: release.assets.find((a) => a.name === `${bin.name}.sha256`),
      bootloader: release.assets.find((a) => a.name === `${base}-bootloader.bin`),
      partitions: release.assets.find((a) => a.name === `${base}-partitions.bin`),
    };
  }
  throw new Error(`No published release with a "${assetBasename}" asset was found.`);
}

async function downloadBinary(asset) {
  const res = await fetch(asset.browser_download_url);
  if (!res.ok) throw new Error(`Failed to download ${asset.name} (${res.status})`);
  return new Uint8Array(await res.arrayBuffer());
}

async function verifySha256(data, shaAsset, label) {
  if (!shaAsset) return; // no sidecar published for this asset -- proceed unverified
  const expected = (await (await fetch(shaAsset.browser_download_url)).text()).trim().toLowerCase();
  const digest = await crypto.subtle.digest("SHA-256", data);
  const actual = [...new Uint8Array(digest)].map((b) => b.toString(16).padStart(2, "0")).join("");
  if (actual !== expected) {
    throw new Error(`${label} failed checksum verification (expected ${expected}, got ${actual}).`);
  }
}

async function loadBootApp0(board) {
  const res = await fetch(`./${board.bootApp0}`);
  if (!res.ok) throw new Error(`Could not load ${board.bootApp0}`);
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
  render(`
    <h2 class="step-title">Connect your board</h2>
    <p class="step-desc">Plug the board in over USB, then connect.</p>
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

async function runFlash(onProgress, onStatus) {
  const board = currentBoard();
  const variant = currentVariant();
  onStatus("Looking up the latest release...");
  const release = await fetchLatestRelease(variant.assetBasename);

  onStatus(`Downloading ${release.bin.name}...`);
  const firmware = await downloadBinary(release.bin);
  await verifySha256(firmware, release.sha, release.bin.name);

  const fileArray = [];
  let eraseAll = false;

  if (state.mode === "new") {
    if (!release.bootloader || !release.partitions) {
      throw new Error("This release is missing bootloader/partitions assets needed for a new-device flash.");
    }
    onStatus(`Downloading ${release.bootloader.name}...`);
    const bootloader = await downloadBinary(release.bootloader);
    onStatus(`Downloading ${release.partitions.name}...`);
    const partitions = await downloadBinary(release.partitions);
    onStatus("Preparing boot selector...");
    const bootApp0 = await loadBootApp0(board);

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
  await connection.esploader.writeFlash({
    fileArray,
    flashMode: board.flashMode,
    flashFreq: board.flashFreq,
    flashSize: board.flashSize,
    eraseAll,
    compress: true,
    reportProgress: (fileIndex, written, total) => onProgress(written / total),
  });
}

function renderFlashing() {
  render(`
    <h2 class="step-title">Flashing</h2>
    <div class="progress-track"><div class="progress-fill" id="fill"></div></div>
    <p class="status-text" id="status">Starting...</p>
  `);

  const fill = wizard.querySelector("#fill");
  const status = wizard.querySelector("#status");

  runFlash(
    (fraction) => {
      fill.style.width = `${Math.round(fraction * 100)}%`;
    },
    (text) => {
      status.textContent = text;
    }
  )
    .then(renderDone)
    .catch((err) => renderError(err, renderConnect));
}

function renderDone() {
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
    <div class="success-box">Firmware written successfully. You can disconnect the board now.</div>
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

async function loadBuildInfo() {
  const el = document.getElementById("build-info");
  try {
    const res = await fetch(`https://api.github.com/repos/${REPO}/commits?path=docs/flasher&per_page=1`);
    if (!res.ok) return;
    const [commit] = await res.json();
    if (!commit) return;
    const date = new Date(commit.commit.committer.date);
    const formatted = date.toLocaleString("en-US", {
      dateStyle: "medium",
      timeStyle: "short",
      timeZone: "UTC",
    });
    el.textContent = `Page last updated ${formatted} UTC (commit ${commit.sha.slice(0, 7)})`;
  } catch {
    // best-effort only -- leave the footer line blank if this fails
  }
}

// ---- Boot --------------------------------------------------------------

loadBoards()
  .then(renderIntro)
  .catch((err) => renderError(err, renderIntro));

loadBuildInfo();
