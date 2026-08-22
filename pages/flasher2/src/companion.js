// Companion protocol — the minimum needed to set a device's regional radio parameters.
//
// Companion builds serve this binary protocol on the USB port instead of the MeshCore CLI,
// which is why `probeCliVersion` gets nothing from them. `workflow.md` requires frequency,
// spreading factor and coding rate to be set on *every* path, so this exists to reach the one
// family of builds the CLI cannot.
//
// Deliberately not a companion client. Contacts, messaging, identity and the rest of the
// settings surface stay with the external app — this sends one command and reads one reply.
// Specified from `@liamcottle/meshcore.js` (MIT), reimplemented rather than depended on (§2.1).

import { COMPANION_REPLY_TIMEOUT_MS, PORT_PROBE_BAUD_RATE } from './constants.js';
import { closeSerialPortQuietly } from './serial-port.js';

// `protocol`. Frame is a type byte, a little-endian u16 length, then the payload. The type
// bytes are ASCII '<' and '>' — direction, seen from the app.
const FRAME_TO_RADIO = 0x3c;
const FRAME_FROM_RADIO = 0x3e;
const FRAME_HEADER_BYTES = 3;

// `protocol`. Only the two commands this module sends, and the replies it can act on.
const CMD_APP_START = 0x01;
const CMD_SET_RADIO_PARAMS = 0x0b;
const RSP_OK = 0x00;
const RSP_ERR = 0x01;
const RSP_SELF_INFO = 0x05;

const APP_VERSION = 1;

/** The device answered, and said no. Carries the command so the caller need not track it. */
export class CompanionCommandError extends Error {
  constructor(command) {
    super(`The device rejected the ${command} command.`);
    this.name = 'CompanionCommandError';
    this.command = command;
  }
}

/** Nothing came back in time — usually a build that does not speak this protocol at all. */
export class CompanionTimeoutError extends Error {
  constructor(command, timeoutMs) {
    super(`No reply to ${command} after ${timeoutMs} ms.`);
    this.name = 'CompanionTimeoutError';
    this.command = command;
  }
}

function frame(payload) {
  const out = new Uint8Array(FRAME_HEADER_BYTES + payload.length);
  out[0] = FRAME_TO_RADIO;
  out[1] = payload.length & 0xff;
  out[2] = (payload.length >> 8) & 0xff;
  out.set(payload, FRAME_HEADER_BYTES);
  return out;
}

// Length-prefixed frames arrive split across reads, so the buffer is drained by header, never
// by assuming one read is one frame.
function takeFrame(buffer) {
  while (buffer.length >= FRAME_HEADER_BYTES) {
    if (buffer[0] !== FRAME_FROM_RADIO && buffer[0] !== FRAME_TO_RADIO) {
      buffer.splice(0, 1); // resynchronise rather than abandon the stream
      continue;
    }
    const length = buffer[1] | (buffer[2] << 8);
    if (length === 0) {
      buffer.splice(0, 1);
      continue;
    }
    if (buffer.length < FRAME_HEADER_BYTES + length) return null;
    return new Uint8Array(buffer.splice(0, FRAME_HEADER_BYTES + length).slice(FRAME_HEADER_BYTES));
  }
  return null;
}

/**
 * Opens the port and starts a session. Returns the device's own SelfInfo, which carries the
 * radio parameters it is running now — the caller can read them before deciding to write.
 */
export async function openCompanionSession(port, { timeoutMs = COMPANION_REPLY_TIMEOUT_MS } = {}) {
  await closeSerialPortQuietly(port);
  await port.open({ baudRate: PORT_PROBE_BAUD_RATE });

  const session = { port, reader: port.readable.getReader(), buffer: [] };
  const start = new Uint8Array(8);
  start[0] = CMD_APP_START;
  start[1] = APP_VERSION;
  // Bytes 2-7 are reserved and must be zero.

  try {
    const reply = await exchange(session, start, 'app start', timeoutMs);
    if (reply[0] !== RSP_SELF_INFO) throw new CompanionCommandError('app start');
    session.selfInfo = parseSelfInfo(reply);
    return session;
  } catch (error) {
    await closeCompanionSession(session);
    throw error;
  }
}

export async function closeCompanionSession(session) {
  try {
    await session.reader.cancel();
    session.reader.releaseLock();
  } catch {
    // A reader we cannot reach is one the close below discards anyway.
  }
  await closeSerialPortQuietly(session.port);
}

async function exchange(session, payload, label, timeoutMs) {
  const writer = session.port.writable.getWriter();
  try {
    await writer.write(frame(payload));
  } finally {
    writer.releaseLock();
  }

  const deadline = Date.now() + timeoutMs;
  for (;;) {
    const ready = takeFrame(session.buffer);
    if (ready) return ready;
    const remaining = deadline - Date.now();
    if (remaining <= 0) throw new CompanionTimeoutError(label, timeoutMs);

    const chunk = await Promise.race([
      session.reader.read(),
      new Promise((resolve) => setTimeout(() => resolve({ timedOut: true }), remaining)),
    ]);
    if (chunk.timedOut) throw new CompanionTimeoutError(label, timeoutMs);
    if (chunk.done) throw new CompanionTimeoutError(label, timeoutMs);
    if (chunk.value) session.buffer.push(...chunk.value);
  }
}

// Layout after the response-code byte. Only the radio fields and name are read back; the rest
// is skipped by width so the offsets stay honest.
function parseSelfInfo(payload) {
  const view = new DataView(payload.buffer, payload.byteOffset, payload.byteLength);
  let at = 1;
  const type = view.getUint8(at); at += 1;
  const txPower = view.getUint8(at); at += 1;
  const maxTxPower = view.getUint8(at); at += 1;
  const publicKey = payload.slice(at, at + 32); at += 32;
  const advLat = view.getInt32(at, true); at += 4;
  const advLon = view.getInt32(at, true); at += 4;
  at += 3; // reserved
  at += 1; // manualAddContacts
  const radioFreq = view.getUint32(at, true); at += 4;
  const radioBw = view.getUint32(at, true); at += 4;
  const radioSf = view.getUint8(at); at += 1;
  const radioCr = view.getUint8(at); at += 1;
  const name = new TextDecoder().decode(payload.slice(at)).replace(/\0.*$/s, '');

  return {
    type,
    txPower,
    maxTxPower,
    publicKeyHex: [...publicKey].map((b) => b.toString(16).padStart(2, '0')).join('').toUpperCase(),
    advLat,
    advLon,
    radioFreq,
    radioBw,
    radioSf,
    radioCr,
    name,
  };
}

/**
 * Sets the four regional radio parameters. Units are the device's own — read them from
 * `session.selfInfo` and write back in the same scale rather than converting.
 */
export async function setRadioParams(
  session,
  { radioFreq, radioBw, radioSf, radioCr },
  { timeoutMs = COMPANION_REPLY_TIMEOUT_MS } = {}
) {
  const payload = new Uint8Array(11);
  const view = new DataView(payload.buffer);
  view.setUint8(0, CMD_SET_RADIO_PARAMS);
  view.setUint32(1, radioFreq, true);
  view.setUint32(5, radioBw, true);
  view.setUint8(9, radioSf);
  view.setUint8(10, radioCr);

  const reply = await exchange(session, payload, 'set radio params', timeoutMs);
  if (reply[0] === RSP_OK) return true;
  if (reply[0] === RSP_ERR) throw new CompanionCommandError('set radio params');
  // An unexpected code is not a success — the device answered something else entirely.
  throw new CompanionCommandError(`set radio params (unexpected reply 0x${reply[0].toString(16)})`);
}
