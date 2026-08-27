import { connectSerial, requestFilteredPort, getAuthorizedPorts } from "./serial-utils.js";

const toggleBtn = document.getElementById("console-toggle");
const backdrop = document.getElementById("console-backdrop");
const closeBtn = document.getElementById("console-close");
const connectBtn = document.getElementById("console-connect");
const output = document.getElementById("console-output");
const form = document.getElementById("console-form");
const input = document.getElementById("console-input");
const referenceResults = document.getElementById("reference-results");

let port = null;
let reader = null;
let readableClosed = null;
let keepReading = false;
let connectionLost = false;
let commands = [];

function log(text) {
  output.textContent += text;
  output.scrollTop = output.scrollHeight;
}

async function readLoop() {
  const decoder = new TextDecoderStream();
  readableClosed = port.readable.pipeTo(decoder.writable);
  reader = decoder.readable.getReader();
  try {
    while (keepReading) {
      const { value, done } = await reader.read();
      if (done) break;
      if (value) log(value);
    }
  } catch (err) {
    log(`\n[connection lost: ${err.message}]\n`);
    connectionLost = true;
    disconnect();
  }
}

async function connect(isReconnect = false) {
  if (!("serial" in navigator)) {
    log("Web Serial isn't supported in this browser.\n");
    return;
  }
  try {
    if (isReconnect) {
      const result = await connectSerial({ baudRate: 115200 });
      port = result.port;
      if (!result.viaReconnect) log("[auto-reconnect failed, select port manually]\n");
    } else {
      port = await requestFilteredPort();
      await port.open({ baudRate: 115200 });
    }
  } catch (err) {
    log(`[could not open port: ${err.message}]\n`);
    port = null;
    return;
  }
  keepReading = true;
  connectBtn.textContent = "Disconnect";
  input.disabled = false;
  input.focus();
  readLoop();
}

async function disconnect() {
  keepReading = false;
  if (reader) {
    await reader.cancel().catch(() => {});
    reader = null;
  }
  if (readableClosed) {
    // port.readable stays locked (by the pipeTo below) until this settles --
    // port.close() throws if the lock isn't released first.
    await readableClosed.catch(() => {});
    readableClosed = null;
  }
  if (port) {
    await port.close().catch(() => {});
    port = null;
  }
  connectBtn.textContent = connectionLost ? "Reconnect" : "Connect";
  connectionLost = false;
  input.disabled = true;
}

async function send(text) {
  if (!port || !port.writable) {
    log("[not connected]\n");
    return;
  }
  const writer = port.writable.getWriter();
  try {
    await writer.write(new TextEncoder().encode(`${text}\r\n`));
  } finally {
    writer.releaseLock();
  }
}

async function loadCommands() {
  try {
    const res = await fetch("./auto_commands.json");
    commands = await res.json();
    updateReference();
  } catch (err) {
    console.error("Failed to load auto_commands.json:", err);
  }
}

function updateReference() {
  const query = input.value.toLowerCase().trim();

  if (!query) {
    referenceResults.textContent = "";
    return;
  }

  const filtered = commands.filter((cmd) =>
    cmd.usage.toLowerCase().startsWith(query)
  );

  referenceResults.textContent = filtered
    .map((cmd) => cmd.usage)
    .join(" | ");
}

// Checks for an already-paired port as soon as the console is opened (never
// prompts) and, if found, connects silently and issues 'ver' to confirm the
// device is alive in CLI mode. The Connect button only ever reads "Connect"
// if this didn't happen -- otherwise it flips straight to "Disconnect".
async function autoDetectAndConnect() {
  const ports = await getAuthorizedPorts();
  if (ports.length === 0) return;

  const candidate = ports[0];
  const info = candidate.getInfo();
  console.log(
    `[console] detected paired device: vendorId 0x${info.usbVendorId?.toString(16) || "?"}, ` +
      `productId 0x${info.usbProductId?.toString(16) || "?"}`
  );

  if (!candidate.readable) {
    try {
      await candidate.open({ baudRate: 115200 });
    } catch (err) {
      console.log(`[console] could not open paired device: ${err.message}`);
      return;
    }
  }

  port = candidate;
  keepReading = true;
  connectBtn.textContent = "Disconnect";
  input.disabled = false;
  readLoop();
  await send("ver");
}

toggleBtn.addEventListener("click", async () => {
  backdrop.classList.remove("hidden");
  if (!port) await autoDetectAndConnect();
  (port ? input : connectBtn).focus();
});
// Closing the console without disconnecting first would leave the port open
// and locked to this reader, so a returning wizard flow (flasher.js) can't
// reclaim it -- simulate a Disconnect click first whenever one is showing.
closeBtn.addEventListener("click", async () => {
  if (connectBtn.textContent === "Disconnect") await disconnect();
  backdrop.classList.add("hidden");
});
connectBtn.addEventListener("click", () => {
  if (port) {
    disconnect();
  } else {
    const isReconnect = connectBtn.textContent === "Reconnect";
    connect(isReconnect);
  }
});
input.addEventListener("input", updateReference);

form.addEventListener("submit", (event) => {
  event.preventDefault();
  const text = input.value;
  if (!text) return;
  send(text); // the device echoes the command itself, so no local echo here
  input.value = "";
  updateReference();
});

loadCommands();
