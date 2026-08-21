// esptool-js boundary — §10.1, §10.2 state 2, §10.4.
//
// Everything that touches the vendored bundle goes through here, so the rest of
// the tool never imports it directly and its option-bag shapes stay in one file.

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

/**
 * A loader bound to a closed port. `baudRate` is the rate esptool renegotiates to
 * once the stub is up; probes never get that far and stay at the ROM's rate.
 *
 * The transport comes back alongside because it, not the loader, owns releasing
 * the port — esptool-js keeps a reader locked on it for as long as it is connected.
 */
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

/**
 * §10.2 state 2: does the ROM bootloader answer?
 *
 * Deliberately passive. `connect('no_reset')` makes `constructResetSequence` return
 * an empty list, so **no reset strategy is ever issued** — a probe that resets the
 * device destroys the state it was asked to identify, and state 3 (a device that
 * enumerates, stays silent and still holds valid SPIFFS) is exactly the state that
 * must survive being looked at.
 *
 * The port must be closed on entry; this opens and releases it. Answers only the
 * affirmative question — a `false` means "did not answer", never "is in app mode".
 */
export async function probeEsptoolSync(port) {
  const { loader, transport } = await createLoader(port, ESP_ROM_BAUD_RATE);
  try {
    // `connect` opens the port and starts the transport's read loop — calling
    // `sync()` without it reads from a stream nobody is pumping and fails as
    // "Serial data stream stopped". Let esptool own that plumbing.
    //
    // One attempt is enough: each already performs five SYNC exchanges internally,
    // and with no reset between them a second attempt asks the same question again.
    // Chip detection is skipped; it belongs to the executor, not to a state probe.
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

/**
 * §10.1: a connected loader with the stub running, at the working baud rate.
 *
 * One session covers the whole flash *and* the §10.4 exit that follows it. That is
 * not a convenience: replicating the exit's register sequence across separate
 * connections never armed the watchdog, because each connection re-syncs with the
 * ROM in between and the unlock is undone.
 *
 * `default_reset` rather than `no_reset` even though §10.3 has just confirmed the
 * device is in download mode — this is the donor's proven path, and a redundant
 * reset of a device already in download mode costs a second and nothing else.
 *
 * The port must be closed on entry. Always pair with `closeEsptoolSession`.
 */
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

/**
 * Releases the port. esptool-js keeps its own reader locked on the port for as long
 * as the transport is connected, so anything that opens the port afterwards — the
 * CLI session, another probe — fails on a locked stream until this has run.
 */
export async function closeEsptoolSession(session) {
  await session.transport.disconnect().catch(() => {});
}

/**
 * §10.1: write a plan's files at their offsets.
 *
 * `onProgress(fileIndex, written, total)` is esptool's own per-file signal, passed
 * through unweighted — the caller knows what the files mean and this does not.
 *
 * @param {{ data: Uint8Array, address: number }[]} files
 */
export async function writeFlashFiles(session, { files, eraseAll, onProgress }) {
  await session.loader.writeFlash({
    fileArray: files,
    flashMode: FLASH_IMAGE_PARAMETER_KEEP,
    flashFreq: FLASH_IMAGE_PARAMETER_KEEP,
    flashSize: FLASH_IMAGE_PARAMETER_KEEP,
    compress: FLASH_WRITE_COMPRESSED,
    eraseAll,
    // esptool only verifies what it wrote if the caller supplies this, and it
    // compares the result against the device's own `flashMd5sum` of the same
    // region. Omitting it — or stubbing it to "" as the GulfCoastMesh donor does
    // (§12.3) — means firmware is written and nothing checks it landed.
    calculateMD5Hash: md5Hex,
    reportProgress: onProgress,
  });
}

/**
 * Read a region of flash back. Used by §10.5's evidence chain before anything is
 * written or erased.
 *
 * Unchunked and without retry, which suits the reads that are one sector long. The
 * filesystem read in §10.6 spans megabytes, where one dropped packet costs the whole
 * partition — that needs the chunked, port-reopening variant and gets it there.
 *
 * @returns {Promise<Uint8Array>}
 */
export async function readFlashRegion(session, offset, size, onProgress) {
  return session.loader.readFlash(offset, size, (_packet, read, total) => {
    onProgress?.(total > 0 ? read / total : 1);
  });
}

/**
 * Reopen the port under a live session, leaving the stub running so a read can
 * resume mid-partition rather than starting over. The caller must re-issue whatever
 * was in flight — the transport is new, the loader's state is not.
 */
async function reopenEsptoolTransport(session) {
  const { loader, transport } = session;
  await transport.disconnect().catch(() => {});
  await sleep(PORT_REOPEN_SETTLE_MS);
  await transport.connect(loader.baudrate, loader.serialOptions ?? {});
  transport.flushInput?.();
}

/**
 * Read a region of flash back, in chunks, with retries and a port reopen behind
 * them. Used for the filesystem partition (§10.5 input 3, §10.6 backup), which is
 * megabytes long — a single lost packet there otherwise costs the whole read.
 *
 * Recovery resumes at the failed chunk rather than restarting, and a reopen is only
 * reached once a chunk's own retries are spent. Exhausting the reopens rethrows the
 * last failure: a partial read must never be handed back as if it were complete,
 * because §10.5 reads its result as evidence about what is on the device.
 *
 * `onNotice` surfaces retries, which otherwise look like a hang.
 *
 * @returns {Promise<Uint8Array>}
 */
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

/**
 * Raw register write. The addresses and values are ESP32 device knowledge and live
 * with the device module (§10.4); only the call shape belongs here.
 */
export async function writeEsptoolRegister(session, address, value, mask = 0xffffffff) {
  await session.loader.writeReg(address, value, mask);
}

/** esptool's own RTS/DTR reset. The §10.4 fallback for a chip with no known WDT registers. */
export async function hardResetDevice(session) {
  await session.loader.after('hard_reset');
}

// --- MD5 ------------------------------------------------------------------
//
// Kept here rather than in its own module: it exists solely because esptool-js's
// `writeFlash` takes a *synchronous* hash callback and verifies nothing without
// one. Web Crypto offers no MD5 and is async either way, so there is nothing to
// delegate to. RFC 1321; checked against a reference implementation over the empty
// string, the RFC's own vectors, every padding boundary and 200 random buffers.

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

/** @param {Uint8Array} bytes @returns {string} lowercase hex */
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
