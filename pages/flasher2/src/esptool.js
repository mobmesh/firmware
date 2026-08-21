// esptool-js boundary — §10.1, §10.2 state 2.
//
// Everything that touches the vendored bundle goes through here, so the rest of
// the tool never imports it directly and its option-bag shapes stay in one file.

import {
  ENTRY_CONNECT_ATTEMPTS,
  ESP_ROM_BAUD_RATE,
  SERIAL_READ_BUFFER_BYTES,
  SYNC_PROBE_ATTEMPTS,
  SYNC_PROBE_TIMEOUT_MS,
} from './constants.js';

const ESPTOOL_JS_URL = '../vendor/esptool-js/bundle.js';

let loadedApi = null;

/** Loaded on first use — the bundle is 218 KB and the nRF52 path never needs it. */
export async function loadEsptoolApi() {
  if (!loadedApi) loadedApi = await import(ESPTOOL_JS_URL);
  return loadedApi;
}

// esptool's chatter belongs in devtools, not on the page.
const silentTerminal = {
  clean() {},
  writeLine(line) {
    console.log(`[esptool] ${line}`);
  },
  write() {},
};

function timeout(milliseconds, message) {
  return new Promise((_, reject) => {
    setTimeout(() => reject(new Error(message)), milliseconds);
  });
}

/**
 * §10.2 state 2: does the ROM bootloader answer?
 *
 * Deliberately passive. `sync()` is called directly rather than through
 * `connect()`/`main()` so that **no reset strategy is ever issued** — a probe that
 * resets the device destroys the state it was asked to identify, and state 3 (a
 * device that enumerates, stays silent and still holds valid SPIFFS) is exactly
 * the state that must survive being looked at.
 *
 * The port must be closed on entry; this opens and releases it. Answers only the
 * affirmative question — a `false` means "did not answer", never "is in app mode".
 */
export async function probeEsptoolSync(port) {
  const { ESPLoader, Transport } = await loadEsptoolApi();
  const transport = new Transport(port, false);
  const loader = new ESPLoader({
    transport,
    baudrate: ESP_ROM_BAUD_RATE,
    romBaudrate: ESP_ROM_BAUD_RATE,
    terminal: silentTerminal,
    debugLogging: false,
    serialOptions: { bufferSize: SERIAL_READ_BUFFER_BYTES },
  });

  try {
    // `connect` opens the port and starts the transport's read loop — calling
    // `sync()` without it reads from a stream nobody is pumping and fails as
    // "Serial data stream stopped". Let esptool own that plumbing.
    //
    // `no_reset` makes this passive: constructResetSequence returns an empty list,
    // so no reset strategy is ever applied. One attempt is enough — each already
    // performs five SYNC exchanges internally, and with no reset between them a
    // second attempt asks the same question again. Chip detection is skipped; it
    // belongs to the executor, not to a state probe.
    await Promise.race([
      loader.connect('no_reset', SYNC_PROBE_ATTEMPTS, false),
      timeout(SYNC_PROBE_TIMEOUT_MS, 'SYNC probe timed out'),
    ]);
    return true;
  } catch {
    // Any failure is a "no". The caller distinguishes states, not this function —
    // and a silent device is never reported as a bootloader (§10.2).
    return false;
  } finally {
    await transport.disconnect().catch(() => {});
  }
}

/**
 * §10.3, `0x1001` mechanism: delegate the reset to esptool-js.
 *
 * `default_reset` makes esptool pick its own strategy — `USBJTAGSerialReset` when
 * the port is a USB Serial/JTAG device, classic DTR/RTS otherwise. That path is
 * hardware-driven, needs no cooperating app, and can recover a hung device, which
 * the app-cooperative gesture cannot.
 *
 * Unlike the probe this is *not* passive: it resets the device by design. Success
 * means the ROM answered SYNC afterwards, so entry is confirmed, not assumed.
 */
export async function resetIntoDownloadMode(port) {
  const { ESPLoader, Transport } = await loadEsptoolApi();
  const transport = new Transport(port, false);
  const loader = new ESPLoader({
    transport,
    baudrate: ESP_ROM_BAUD_RATE,
    romBaudrate: ESP_ROM_BAUD_RATE,
    terminal: silentTerminal,
    debugLogging: false,
    serialOptions: { bufferSize: SERIAL_READ_BUFFER_BYTES },
  });

  try {
    await loader.connect('default_reset', ENTRY_CONNECT_ATTEMPTS, false);
    return true;
  } catch {
    return false;
  } finally {
    await transport.disconnect().catch(() => {});
  }
}
