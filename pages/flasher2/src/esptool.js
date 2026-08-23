// esptool-js boundary: the SYNC probe, the flash session, and the exit reset. Everything
// touching the vendored bundle goes through here, so its option shapes stay in one file.

import {
  ENTRY_CONNECT_ATTEMPTS,
  FLASH_READ_ATTEMPTS_PER_CHUNK,
  FLASH_READ_CHUNK_BYTES,
  FLASH_READ_MAX_PORT_REOPENS,
  ESP_ROM_BAUD_RATE,
  ESPTOOL_BAUD_RATE,
  FLASH_IMAGE_PARAMETER_KEEP,
  FLASH_WRITE_COMPRESSED,
  PORT_REOPEN_SETTLE_MS,
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

function sleep(milliseconds) {
  return new Promise((resolve) => setTimeout(resolve, milliseconds));
}

function timeout(milliseconds, message) {
  return new Promise((_, reject) => {
    setTimeout(() => reject(new Error(message)), milliseconds);
  });
}

// `baudRate` is what esptool renegotiates to once the stub is up; probes never get that far.
// The transport, not the loader, owns releasing the port.
async function createLoader(port, baudRate) {
  const { ESPLoader, Transport } = await loadEsptoolApi();
  // Second argument is `tracing`, not a slip-reader switch: true hex-dumps every
  // packet synchronously on the thread that has to keep acking.
  const transport = new Transport(port, false);
  const loader = new ESPLoader({
    transport,
    baudrate: baudRate,
    romBaudrate: ESP_ROM_BAUD_RATE,
    terminal: silentTerminal,
    debugLogging: false,
    serialOptions: { bufferSize: SERIAL_READ_BUFFER_BYTES },
  });
  return { loader, transport };
}

// Passive: `no_reset` issues no reset, because a probe that resets destroys the very
// state it exists to identify. Takes a closed port; false only ever means "did not answer".
export async function probeEsptoolSync(port) {
  const { loader, transport } = await createLoader(port, ESP_ROM_BAUD_RATE);
  try {
    // `connect` starts the read loop; `sync()` alone reads a stream nobody is pumping.
    // One attempt: it already performs five SYNC exchanges, and no reset separates them.
    await Promise.race([
      loader.connect('no_reset', SYNC_PROBE_ATTEMPTS, false),
      timeout(SYNC_PROBE_TIMEOUT_MS, 'SYNC probe timed out'),
    ]);
    return true;
  } catch {
    // Any failure is a "no". The caller distinguishes states, not this function —
    // and a silent device is never reported as a bootloader.
    return false;
  } finally {
    await transport.disconnect().catch(() => {});
  }
}

// `0x1001`. `default_reset` makes esptool pick `USBJTAGSerialReset` — hardware-driven,
// needs no cooperating app, recovers a hung device. Resets by design, unlike the probe.
export async function resetIntoDownloadMode(port) {
  const { loader, transport } = await createLoader(port, ESP_ROM_BAUD_RATE);
  try {
    await loader.connect('default_reset', ENTRY_CONNECT_ATTEMPTS, false);
    return true;
  } catch {
    return false;
  } finally {
    await transport.disconnect().catch(() => {});
  }
}

// One session covers the flash and the exit reset: the exit's register sequence fails if
// anything re-syncs the ROM partway. Always pair with `closeEsptoolSession`.
export async function openEsptoolSession(port) {
  const { loader, transport } = await createLoader(port, ESPTOOL_BAUD_RATE);
  try {
    const chipDescription = await loader.main();
    return { loader, transport, chipName: loader.chip.CHIP_NAME, chipDescription };
  } catch (error) {
    // Nothing else holds the transport yet, so release it here rather than leaving
    // the port locked to a session the caller never received.
    await transport.disconnect().catch(() => {});
    throw error;
  }
}

// esptool keeps a reader locked on the port while connected, so anything opening it
// afterwards fails on a locked stream until this runs.
export async function closeEsptoolSession(session) {
  await session.transport.disconnect().catch(() => {});
}

// `onProgress(fileIndex, written, total)` is esptool's own per-file signal, unweighted —
// the caller knows what the files mean.
export async function writeFlashFiles(session, { files, eraseAll, onProgress }) {
  await session.loader.writeFlash({
    fileArray: files,
    flashMode: FLASH_IMAGE_PARAMETER_KEEP,
    flashFreq: FLASH_IMAGE_PARAMETER_KEEP,
    flashSize: FLASH_IMAGE_PARAMETER_KEEP,
    compress: FLASH_WRITE_COMPRESSED,
    eraseAll,
    // esptool verifies the write only if this is supplied, against the device's own
    // `flashMd5sum`. Omitting it means firmware is written and nothing checks it landed.
    calculateMD5Hash: md5Hex,
    reportProgress: onProgress,
  });
}

// Unchunked, for reads a sector long. Megabyte reads use `readFlashChunked`.
export async function readFlashRegion(session, offset, size, onProgress) {
  return session.loader.readFlash(offset, size, (_packet, read, total) => {
    onProgress?.(total > 0 ? read / total : 1);
  });
}

// Leaves the stub running so a read resumes mid-partition. The caller re-issues what was
// in flight — the transport is new, the loader's state is not.
async function reopenEsptoolTransport(session) {
  const { loader, transport } = session;
  await transport.disconnect().catch(() => {});
  await sleep(PORT_REOPEN_SETTLE_MS);
  await transport.connect(loader.baudrate, loader.serialOptions ?? {});
  transport.flushInput?.();
}

// Retries a chunk, then reopens the port behind it. Exhausting the reopens rethrows: the
// erase decision reads this as evidence, so a partial read must never pass as complete.
export async function readFlashChunked(session, offset, size, { onProgress, onNotice } = {}) {
  const out = new Uint8Array(size);
  let done = 0;
  let reopens = 0;

  while (done < size) {
    const want = Math.min(FLASH_READ_CHUNK_BYTES, size - done);
    const base = done;
    let chunk = null;
    let lastError = null;

    for (let attempt = 1; attempt <= FLASH_READ_ATTEMPTS_PER_CHUNK && chunk === null; attempt += 1) {
      try {
        // A failed attempt's leftovers would head this one's reply.
        if (attempt > 1) session.transport.flushInput?.();
        chunk = await session.loader.readFlash(offset + base, want, (_packet, read, total) => {
          onProgress?.((base + (total > 0 ? read / total : 1) * want) / size);
        });
      } catch (error) {
        lastError = error;
        console.warn(
          `[esptool] Read of ${want}B at 0x${(offset + base).toString(16)} failed ` +
            `(attempt ${attempt}/${FLASH_READ_ATTEMPTS_PER_CHUNK})`,
          error
        );
        if (attempt < FLASH_READ_ATTEMPTS_PER_CHUNK) {
          onNotice?.(`Read interrupted, retrying (${attempt + 1} of ${FLASH_READ_ATTEMPTS_PER_CHUNK})…`);
        }
      }
    }

    if (chunk === null) {
      if (reopens >= FLASH_READ_MAX_PORT_REOPENS) throw lastError;
      reopens += 1;
      onNotice?.(`Reconnecting to the device (${reopens} of ${FLASH_READ_MAX_PORT_REOPENS})…`);
      await reopenEsptoolTransport(session);
      continue; // same chunk, fresh link — resume rather than restart
    }

    out.set(chunk.subarray(0, want), base);
    done += want;
  }

  onProgress?.(1);
  return out;
}

// Addresses and values are device knowledge and live in the ESP32 module; only the call shape is here.
export async function writeEsptoolRegister(session, address, value, mask = 0xffffffff) {
  await session.loader.writeReg(address, value, mask);
}

/** esptool's own RTS/DTR reset. The fallback for a chip with no known WDT registers. */
export async function hardResetDevice(session) {
  await session.loader.after('hard_reset');
}

// --- MD5 ------------------------------------------------------------------
// Here because `writeFlash` takes a *synchronous* hash callback and Web Crypto offers no
// MD5. RFC 1321, checked against the RFC vectors, every padding boundary and 200 buffers.

// prettier-ignore
const MD5_SHIFTS = [
  7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
  5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20,
  4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
  6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21,
];

// K[i] = floor(abs(sin(i + 1)) * 2^32), per the RFC.
const MD5_SINE_TABLE = new Uint32Array(64);
for (let i = 0; i < 64; i++) MD5_SINE_TABLE[i] = Math.abs(Math.sin(i + 1)) * 0x100000000;

// RFC 1321. Checked against a reference implementation over the empty string, the RFC
// vectors, every padding boundary and 200 random buffers.
function md5Hex(bytes) {
  const totalBits = bytes.length * 8;
  // Message plus a 0x80 terminator plus an 8-byte length, rounded up to 64 bytes.
  const wordCount = ((((bytes.length + 8) >> 6) + 1) << 6) >> 2;
  const words = new Uint32Array(wordCount);
  for (let i = 0; i < bytes.length; i++) words[i >> 2] |= bytes[i] << ((i & 3) << 3);
  words[bytes.length >> 2] |= 0x80 << ((bytes.length & 3) << 3);
  // ToUint32 truncates the low word correctly even past 2^32 bits.
  words[wordCount - 2] = totalBits >>> 0;
  words[wordCount - 1] = Math.floor(totalBits / 0x100000000) >>> 0;

  let a0 = 0x67452301;
  let b0 = 0xefcdab89;
  let c0 = 0x98badcfe;
  let d0 = 0x10325476;

  for (let base = 0; base < wordCount; base += 16) {
    let a = a0;
    let b = b0;
    let c = c0;
    let d = d0;

    for (let i = 0; i < 64; i++) {
      let mixed;
      let wordIndex;
      if (i < 16) {
        mixed = (b & c) | (~b & d);
        wordIndex = i;
      } else if (i < 32) {
        mixed = (d & b) | (~d & c);
        wordIndex = (5 * i + 1) & 15;
      } else if (i < 48) {
        mixed = b ^ c ^ d;
        wordIndex = (3 * i + 5) & 15;
      } else {
        mixed = c ^ (b | ~d);
        wordIndex = (7 * i) & 15;
      }
      const shift = MD5_SHIFTS[i];
      const sum = (a + mixed + MD5_SINE_TABLE[i] + words[base + wordIndex]) >>> 0;
      a = d;
      d = c;
      c = b;
      b = (b + ((sum << shift) | (sum >>> (32 - shift)))) >>> 0;
    }

    a0 = (a0 + a) >>> 0;
    b0 = (b0 + b) >>> 0;
    c0 = (c0 + c) >>> 0;
    d0 = (d0 + d) >>> 0;
  }

  let hex = '';
  for (const word of [a0, b0, c0, d0]) {
    // Little-endian digest order.
    for (let byte = 0; byte < 4; byte++) {
      hex += ((word >>> (byte * 8)) & 0xff).toString(16).padStart(2, '0');
    }
  }
  return hex;
}
