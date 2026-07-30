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
    // On reconnect, try to auto-connect to previously approved port first
    if (isReconnect) {
      const ports = await navigator.serial.getPorts();
      if (ports.length > 0) {
        try {
          console.log(`Attempting auto-reconnect to ${ports[0].getInfo().usbProductName}...`);
          port = ports[0];
          await port.open({ baudRate: 115200 });
          console.log("Auto-reconnect succeeded");
        } catch (err) {
          // Auto-connect failed, fall back to manual selection
          console.log("Auto-reconnect failed:", err.message);
          log("[auto-reconnect failed, select port manually]\n");
          port = await navigator.serial.requestPort();
          await port.open({ baudRate: 115200 });
        }
      } else {
        console.log("No previously approved ports, requesting manual selection");
        port = await navigator.serial.requestPort();
        await port.open({ baudRate: 115200 });
      }
    } else {
      port = await navigator.serial.requestPort();
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
    const res = await fetch("./commands.json");
    commands = await res.json();
    updateReference();
  } catch (err) {
    console.error("Failed to load commands.json:", err);
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

toggleBtn.addEventListener("click", () => {
  backdrop.classList.remove("hidden");
  (port ? input : connectBtn).focus();
});
closeBtn.addEventListener("click", () => backdrop.classList.add("hidden"));
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
