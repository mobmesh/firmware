// Companion protocol: the minimum to set radio parameters on builds that serve this
// instead of the CLI. Not a companion client — the settings surface stays in the app.

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
const CMD_DEVICE_QUERY = 0x16;
const CMD_GET_DEVICE_TIME = 0x05;
const CMD_SET_DEVICE_TIME = 0x06;
const CMD_SEND_SELF_ADVERT = 0x07;
const CMD_SET_ADVERT_NAME = 0x08;
const CMD_SET_RADIO_TX_POWER = 0x0c;
const CMD_SET_ADVERT_LATLON = 0x0e;
const CMD_REBOOT = 0x13;
const CMD_GET_BATT_AND_STORAGE = 0x14;
const CMD_SET_TUNING_PARAMS = 0x15;
const CMD_EXPORT_PRIVATE_KEY = 0x17;
const CMD_IMPORT_PRIVATE_KEY = 0x18;
const CMD_SET_DEVICE_PIN = 0x25;
const CMD_SET_OTHER_PARAMS = 0x26;
const CMD_GET_CUSTOM_VARS = 0x28;
const CMD_SET_CUSTOM_VAR = 0x29;
const CMD_GET_TUNING_PARAMS = 0x2b;
const CMD_FACTORY_RESET = 0x33;
const CMD_GET_STATS = 0x38;
const CMD_SET_AUTOADD_CONFIG = 0x3a;
const CMD_GET_AUTOADD_CONFIG = 0x3b;
const CMD_GET_ALLOWED_REPEAT_FREQ = 0x3c;
const CMD_SET_DEFAULT_FLOOD_SCOPE = 0x3f;
const CMD_GET_DEFAULT_FLOOD_SCOPE = 0x40;
// Absent from `meshcore.js`, which is behind the firmware here — taken from the firmware's
// own `MyMesh.cpp` (`CMD_SET_PATH_HASH_MODE 61`).
const CMD_SET_PATH_HASH_MODE = 0x3d;
const RSP_OK = 0x00;
const RSP_ERR = 0x01;
const RSP_SELF_INFO = 0x05;
const RSP_DEVICE_INFO = 0x0d;
const RSP_CURR_TIME = 0x09;
const RSP_BATT_AND_STORAGE = 0x0c;
const RSP_PRIVATE_KEY = 0x0e;
const RSP_DISABLED = 0x0f;
const RSP_CUSTOM_VARS = 0x15;
const RSP_TUNING_PARAMS = 0x17;
const RSP_STATS = 0x18;
const RSP_AUTOADD_CONFIG = 0x19;
const RSP_DEFAULT_FLOOD_SCOPE = 0x1c;

/** Scale the firmware applies to advert coordinates: degrees are carried as millionths. */
export const COORDINATE_SCALE = 1e6;

/** `get stats` selector. Core is the only type this module asks for. */
export const STATS_TYPE_CORE = 0;

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
 * Opens the port and returns the device's SelfInfo, carrying the radio parameters it is
 * running now, so the caller can read before deciding to write.
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
 * Sets the four regional radio parameters. Units are the device's own: read from
 * `session.selfInfo` and write back in the same scale.
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

/**
 * `DeviceQuery`: the only way to read firmware version and path hash mode. Trailing fields
 * arrived by version, so each is read only if the frame is long enough to carry it.
 */
export async function queryDeviceInfo(session, { timeoutMs = COMPANION_REPLY_TIMEOUT_MS } = {}) {
  const payload = new Uint8Array([CMD_DEVICE_QUERY, APP_VERSION]);
  const reply = await exchange(session, payload, 'device query', timeoutMs);
  if (reply[0] !== RSP_DEVICE_INFO) throw new CompanionCommandError('device query');

  const view = new DataView(reply.buffer, reply.byteOffset, reply.byteLength);
  const text = (from, length) =>
    new TextDecoder().decode(reply.slice(from, from + length)).replace(/\0.*$/s, '').trim();

  return {
    firmwareVersionCode: view.getUint8(1),
    maxContacts: view.getUint8(2) * 2,
    maxChannels: view.getUint8(3),
    blePin: view.getUint32(4, true),
    firmwareBuild: text(8, 12),
    model: text(20, 40),
    firmwareVersion: text(60, 20),
    repeatEnabled: reply.length > 80 ? view.getUint8(80) === 1 : null,
    pathHashMode: reply.length > 81 ? view.getUint8(81) : null,
  };
}

/**
 * Path hash size in adverts: 0 = 1-byte, 1 = 2-byte, 2 = 3-byte. Mesh-wide — older nodes
 * drop anyone on a larger hash, so it is a flag day, not a per-device preference.
 */
export async function setPathHashMode(session, mode, { timeoutMs = COMPANION_REPLY_TIMEOUT_MS } = {}) {
  if (!Number.isInteger(mode) || mode < 0 || mode > 2) {
    throw new CompanionCommandError(`set path hash mode (${mode} is not 0, 1 or 2)`);
  }
  // Byte 1 must be zero: the firmware tests it explicitly before reading the mode.
  const payload = new Uint8Array([CMD_SET_PATH_HASH_MODE, 0, mode]);
  const reply = await exchange(session, payload, 'set path hash mode', timeoutMs);
  if (reply[0] === RSP_OK) return true;
  throw new CompanionCommandError('set path hash mode');
}

// --- shared helpers ---------------------------------------------------------------------

const encoder = new TextEncoder();
const decoder = new TextDecoder();

function bytes(...parts) {
  const flat = [];
  for (const part of parts) {
    if (typeof part === 'number') flat.push(part & 0xff);
    else flat.push(...part);
  }
  return new Uint8Array(flat);
}

function u32(value) {
  return [value & 0xff, (value >>> 8) & 0xff, (value >>> 16) & 0xff, (value >>> 24) & 0xff];
}

function text(payload, from, length) {
  return decoder.decode(payload.slice(from, from + length)).replace(/\0.*$/s, '').trim();
}

// A command whose only success answer is OK. Anything else is a refusal, including the
// firmware's ILLEGAL_ARG — the caller gets the label, never a raw code to interpret.
async function commandExpectingOk(session, payload, label, timeoutMs) {
  const reply = await exchange(session, payload, label, timeoutMs ?? COMPANION_REPLY_TIMEOUT_MS);
  if (reply[0] === RSP_OK) return true;
  if (reply[0] === RSP_DISABLED) throw new CompanionCommandError(`${label} (disabled in this build)`);
  throw new CompanionCommandError(label);
}

async function commandExpecting(session, payload, label, expected, timeoutMs) {
  const reply = await exchange(session, payload, label, timeoutMs ?? COMPANION_REPLY_TIMEOUT_MS);
  if (reply[0] === RSP_DISABLED) throw new CompanionCommandError(`${label} (disabled in this build)`);
  if (reply[0] !== expected) throw new CompanionCommandError(label);
  return reply;
}

// --- identity ---------------------------------------------------------------------------

/** The 64-byte expanded key, same layout the CLI's `set prv.key` takes. */
export async function exportPrivateKey(session, options) {
  const reply = await commandExpecting(session, bytes(CMD_EXPORT_PRIVATE_KEY), 'export private key', RSP_PRIVATE_KEY, options?.timeoutMs);
  return reply.slice(1, 65);
}

/** Replaces the node's identity. The firmware validates the key and rejects a malformed one. */
export async function importPrivateKey(session, privateKey, options) {
  if (privateKey.length !== 64) {
    throw new CompanionCommandError(`import private key (expected 64 bytes, got ${privateKey.length})`);
  }
  return commandExpectingOk(session, bytes(CMD_IMPORT_PRIVATE_KEY, privateKey), 'import private key', options?.timeoutMs);
}

// --- identity and presence ----------------------------------------------------------------

export async function setAdvertName(session, name, options) {
  return commandExpectingOk(session, bytes(CMD_SET_ADVERT_NAME, encoder.encode(name)), 'set advert name', options?.timeoutMs);
}

/** Degrees. The firmware rejects anything outside ±90 / ±180 once scaled. */
export async function setAdvertLatLon(session, { latitude, longitude }, options) {
  const lat = Math.round(latitude * COORDINATE_SCALE);
  const lon = Math.round(longitude * COORDINATE_SCALE);
  return commandExpectingOk(session, bytes(CMD_SET_ADVERT_LATLON, u32(lat), u32(lon)), 'set advert lat/lon', options?.timeoutMs);
}

/** `flood` reaches the mesh; `zeroHop` only direct neighbours. */
export async function sendSelfAdvert(session, { flood = true } = {}, options) {
  return commandExpectingOk(session, bytes(CMD_SEND_SELF_ADVERT, flood ? 1 : 0), 'send self advert', options?.timeoutMs);
}

// --- radio ---------------------------------------------------------------------------------

/** dBm. The firmware rejects below -9 or above the board's maximum. */
export async function setTxPower(session, dbm, options) {
  return commandExpectingOk(session, bytes(CMD_SET_RADIO_TX_POWER, dbm & 0xff), 'set tx power', options?.timeoutMs);
}

/** Seconds, as the CLI states them; the wire carries milliseconds. */
export async function setTuningParams(session, { rxDelayBase, airtimeFactor }, options) {
  const payload = bytes(CMD_SET_TUNING_PARAMS, u32(Math.round(rxDelayBase * 1000)), u32(Math.round(airtimeFactor * 1000)));
  return commandExpectingOk(session, payload, 'set tuning params', options?.timeoutMs);
}

export async function getTuningParams(session, options) {
  const reply = await commandExpecting(session, bytes(CMD_GET_TUNING_PARAMS), 'get tuning params', RSP_TUNING_PARAMS, options?.timeoutMs);
  const view = new DataView(reply.buffer, reply.byteOffset, reply.byteLength);
  return { rxDelayBase: view.getUint32(1, true) / 1000, airtimeFactor: view.getUint32(5, true) / 1000 };
}

export async function getAllowedRepeatFrequencies(session, options) {
  const reply = await exchange(session, bytes(CMD_GET_ALLOWED_REPEAT_FREQ), 'get allowed repeat freq', options?.timeoutMs ?? COMPANION_REPLY_TIMEOUT_MS);
  const view = new DataView(reply.buffer, reply.byteOffset, reply.byteLength);
  const ranges = [];
  for (let at = 1; at + 8 <= reply.length; at += 8) {
    ranges.push({ lower: view.getUint32(at, true), upper: view.getUint32(at + 4, true) });
  }
  return ranges;
}

// --- clock ---------------------------------------------------------------------------------

export async function getDeviceTime(session, options) {
  const reply = await commandExpecting(session, bytes(CMD_GET_DEVICE_TIME), 'get device time', RSP_CURR_TIME, options?.timeoutMs);
  return new DataView(reply.buffer, reply.byteOffset, reply.byteLength).getUint32(1, true);
}

/** Epoch seconds. The firmware refuses a time earlier than the one it already holds. */
export async function setDeviceTime(session, epochSeconds, options) {
  return commandExpectingOk(session, bytes(CMD_SET_DEVICE_TIME, u32(epochSeconds)), 'set device time', options?.timeoutMs);
}

// --- behaviour ------------------------------------------------------------------------------

/**
 * Trailing fields are sent only when supplied: a short frame is how the firmware detects
 * an older client.
 */
export async function setOtherParams(session, { manualAddContacts, telemetryMode, advertLocationPolicy, multiAcks }, options) {
  const parts = [CMD_SET_OTHER_PARAMS, manualAddContacts ? 1 : 0];
  if (telemetryMode !== undefined) {
    parts.push(telemetryMode & 0xff);
    if (advertLocationPolicy !== undefined) {
      parts.push(advertLocationPolicy & 0xff);
      if (multiAcks !== undefined) parts.push(multiAcks & 0xff);
    }
  }
  return commandExpectingOk(session, bytes(...parts), 'set other params', options?.timeoutMs);
}

export async function setAutoAddConfig(session, { config, maxHops }, options) {
  const parts = [CMD_SET_AUTOADD_CONFIG, config & 0xff];
  if (maxHops !== undefined) parts.push(Math.min(maxHops, 64) & 0xff);
  return commandExpectingOk(session, bytes(...parts), 'set auto-add config', options?.timeoutMs);
}

export async function getAutoAddConfig(session, options) {
  const reply = await commandExpecting(session, bytes(CMD_GET_AUTOADD_CONFIG), 'get auto-add config', RSP_AUTOADD_CONFIG, options?.timeoutMs);
  return { config: reply[1], maxHops: reply[2] };
}

/** Zero clears it; anything else must be a six-digit PIN or the firmware refuses. */
export async function setDevicePin(session, pin, options) {
  return commandExpectingOk(session, bytes(CMD_SET_DEVICE_PIN, u32(pin)), 'set device pin', options?.timeoutMs);
}

// --- flood scope -----------------------------------------------------------------------------

/** Name is a 31-byte field and the key is exactly 16 bytes; the firmware reads fixed offsets. */
export async function setDefaultFloodScope(session, { name, key }, options) {
  if (key.length !== 16) throw new CompanionCommandError(`set default flood scope (key must be 16 bytes)`);
  const nameField = new Uint8Array(31);
  nameField.set(encoder.encode(name).slice(0, 30));
  return commandExpectingOk(session, bytes(CMD_SET_DEFAULT_FLOOD_SCOPE, nameField, key), 'set default flood scope', options?.timeoutMs);
}

/** A bare response code means no scope is set — absence, not failure. */
export async function getDefaultFloodScope(session, options) {
  const reply = await commandExpecting(session, bytes(CMD_GET_DEFAULT_FLOOD_SCOPE), 'get default flood scope', RSP_DEFAULT_FLOOD_SCOPE, options?.timeoutMs);
  if (reply.length < 1 + 31 + 16) return null;
  return { name: text(reply, 1, 31), key: reply.slice(32, 48) };
}

// --- sensors and diagnostics -------------------------------------------------------------------

export async function getBatteryAndStorage(session, options) {
  const reply = await commandExpecting(session, bytes(CMD_GET_BATT_AND_STORAGE), 'get battery and storage', RSP_BATT_AND_STORAGE, options?.timeoutMs);
  const view = new DataView(reply.buffer, reply.byteOffset, reply.byteLength);
  return { batteryMillivolts: view.getUint16(1, true), storageUsedKb: view.getUint32(3, true), storageTotalKb: view.getUint32(7, true) };
}

export async function getStats(session, { type = STATS_TYPE_CORE } = {}, options) {
  const reply = await commandExpecting(session, bytes(CMD_GET_STATS, type), 'get stats', RSP_STATS, options?.timeoutMs);
  const view = new DataView(reply.buffer, reply.byteOffset, reply.byteLength);
  if (reply[1] !== STATS_TYPE_CORE) return { type: reply[1], raw: reply.slice(2) };
  return {
    type: reply[1],
    batteryMillivolts: view.getUint16(2, true),
    uptimeSeconds: view.getUint32(4, true),
    outboundQueueLength: reply[8],
    raw: reply.slice(2),
  };
}

/** Sensor settings, which the firmware returns as one `name:value,name:value` string. */
export async function getCustomVars(session, options) {
  const reply = await commandExpecting(session, bytes(CMD_GET_CUSTOM_VARS), 'get custom vars', RSP_CUSTOM_VARS, options?.timeoutMs);
  const vars = {};
  for (const pair of text(reply, 1, reply.length - 1).split(',')) {
    const at = pair.indexOf(':');
    if (at > 0) vars[pair.slice(0, at)] = pair.slice(at + 1);
  }
  return vars;
}

export async function setCustomVar(session, name, value, options) {
  return commandExpectingOk(session, bytes(CMD_SET_CUSTOM_VAR, encoder.encode(`${name}:${value}`)), 'set custom var', options?.timeoutMs);
}

// --- lifecycle ---------------------------------------------------------------------------------

/** Fire and forget: the firmware reboots without answering, so waiting would always time out. */
export async function reboot(session) {
  const writer = session.port.writable.getWriter();
  try {
    await writer.write(frame(bytes(CMD_REBOOT, encoder.encode('reboot'))));
  } finally {
    writer.releaseLock();
  }
}

/**
 * Erases identity, settings and contacts, then reboots. The firmware drops serial before
 * answering, so a timeout here is success.
 */
export async function factoryReset(session, options) {
  try {
    return await commandExpectingOk(session, bytes(CMD_FACTORY_RESET, encoder.encode('reset')), 'factory reset', options?.timeoutMs);
  } catch (error) {
    if (error instanceof CompanionTimeoutError) return true;
    throw error;
  }
}
