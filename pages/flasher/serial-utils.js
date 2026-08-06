// Shared Web Serial helpers: silent reconnect to already-authorized ports
// before ever falling back to the picker dialog.

export const ESPRESSIF_VENDOR_ID = 0x303a;

export async function getAuthorizedPorts() {
  try {
    return await navigator.serial.getPorts();
  } catch {
    return [];
  }
}

export async function isDeviceAlreadyAuthorized() {
  return (await getAuthorizedPorts()).length > 0;
}

export async function requestFilteredPort(filters = [{ usbVendorId: ESPRESSIF_VENDOR_ID }]) {
  return navigator.serial.requestPort({ filters });
}

const CLI_RESPONSE_MARKER = "  -> ";

// Writes `command\r` and resolves when the device's "  -> <response>" line
// appears, or rejects after timeoutMs. One command in flight at a time.
export function createCliSession(port) {
  const decoder = new TextDecoderStream();
  const readableClosed = port.readable.pipeTo(decoder.writable);
  const reader = decoder.readable.getReader();
  const writer = port.writable.getWriter();
  let buffer = "";
  let pending = null; // { resolve, reject, timer }

  function checkBuffer() {
    if (!pending) return;
    const markerIndex = buffer.indexOf(CLI_RESPONSE_MARKER);
    if (markerIndex === -1) return;
    const lineEnd = buffer.indexOf("\r\n", markerIndex);
    if (lineEnd === -1) return;
    const response = buffer.slice(markerIndex + CLI_RESPONSE_MARKER.length, lineEnd).trim();
    buffer = buffer.slice(lineEnd + 2);
    const { resolve, timer } = pending;
    clearTimeout(timer);
    pending = null;
    resolve(response);
  }

  (async () => {
    try {
      // eslint-disable-next-line no-constant-condition
      while (true) {
        const { value, done } = await reader.read();
        if (done) break;
        buffer += value;
        checkBuffer();
      }
    } catch {
      // port dropped -- any pending command is rejected below
    } finally {
      if (pending) pending.reject(new Error("Serial connection lost"));
    }
  })();

  async function sendCommand(command, { timeoutMs = 5000, postDelayMs = 100 } = {}) {
    if (pending) throw new Error("sendCommand called while a command is already pending");
    await writer.write(new TextEncoder().encode(`${command}\r`));
    const response = await new Promise((resolve, reject) => {
      const timer = setTimeout(() => {
        pending = null;
        reject(new Error(`Command timed out: ${command}`));
      }, timeoutMs);
      pending = { resolve, reject, timer };
      checkBuffer(); // covers the response having already arrived
    });
    if (postDelayMs) await new Promise((r) => setTimeout(r, postDelayMs));
    return response;
  }

  async function close() {
    writer.releaseLock();
    await reader.cancel().catch(() => {});
    await readableClosed.catch(() => {});
  }

  return { sendCommand, close };
}

// Preferred port, then any authorized one, then the picker. Only the picker
// can prompt.
export async function connectSerial({ preferredPort, baudRate = 115200, skipOpen = false } = {}) {
  const tryOpen = async (port) => {
    if (!skipOpen && !port.readable) await port.open({ baudRate });
    return port;
  };

  if (preferredPort) {
    try {
      return { port: await tryOpen(preferredPort), viaReconnect: true };
    } catch {
      // fall through to the general reconnect/picker path below
    }
  }

  const authorized = await getAuthorizedPorts();
  if (authorized.length > 0) {
    try {
      return { port: await tryOpen(authorized[0]), viaReconnect: true };
    } catch {
      // fall through to the picker
    }
  }

  const port = await requestFilteredPort();
  return { port: await tryOpen(port), viaReconnect: false };
}
