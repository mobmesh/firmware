const toggleBtn = document.getElementById("console-toggle");
const backdrop = document.getElementById("console-backdrop");
const closeBtn = document.getElementById("console-close");
const connectBtn = document.getElementById("console-connect");
const output = document.getElementById("console-output");
const form = document.getElementById("console-form");
const input = document.getElementById("console-input");

let port = null;
let reader = null;
let keepReading = false;

function log(text) {
  output.textContent += text;
  output.scrollTop = output.scrollHeight;
}

async function readLoop() {
  const decoder = new TextDecoderStream();
  port.readable.pipeTo(decoder.writable).catch(() => {});
  reader = decoder.readable.getReader();
  try {
    while (keepReading) {
      const { value, done } = await reader.read();
      if (done) break;
      if (value) log(value);
    }
  } catch (err) {
    log(`\n[connection lost: ${err.message}]\n`);
  }
}

async function connect() {
  if (!("serial" in navigator)) {
    log("Web Serial isn't supported in this browser.\n");
    return;
  }
  try {
    port = await navigator.serial.requestPort();
    await port.open({ baudRate: 115200 });
  } catch (err) {
    log(`[could not open port: ${err.message}]\n`);
    port = null;
    return;
  }
  keepReading = true;
  connectBtn.textContent = "Disconnect";
  readLoop();
}

async function disconnect() {
  keepReading = false;
  if (reader) {
    await reader.cancel().catch(() => {});
    reader = null;
  }
  if (port) {
    await port.close().catch(() => {});
    port = null;
  }
  connectBtn.textContent = "Connect";
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

toggleBtn.addEventListener("click", () => backdrop.classList.remove("hidden"));
closeBtn.addEventListener("click", () => backdrop.classList.add("hidden"));
connectBtn.addEventListener("click", () => (port ? disconnect() : connect()));

form.addEventListener("submit", (event) => {
  event.preventDefault();
  const text = input.value;
  if (!text) return;
  log(`> ${text}\n`);
  send(text);
  input.value = "";
});
