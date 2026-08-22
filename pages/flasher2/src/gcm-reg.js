// GulfCoastMesh node registry (MeshBuddy) — optional, and never load-bearing.
//
// Its own module because it is a third-party HTTP API: it changes independently of
// anything else here, and nothing in the flash path may depend on it.
//
// The registry claims a 2-byte prefix of a node's *public* key so two nodes on the mesh
// do not collide. No private key is ever sent — see `reservePrefix`'s payload.
//
// Key generation lives here rather than in a module of its own: nothing else in the tool
// needs a MeshCore identity, and it exists only to feed the mining loop below.

import { GCM_REGISTRY_TIMEOUT_MS } from './constants.js';

// Their public service, called directly: it echoes any Origin (and answers `*` with none),
// so unlike stock firmware (§9.2) this needs no relay.
export const GCM_REGISTRY_BASE = 'https://meshbuddy.gulfcoastmesh.org';

// Their format: 4 hex characters, taken from the front of the public key.
export const GCM_PREFIX_LENGTH = 4;

// MeshCore's key is the *expanded* 64-byte layout — a clamped scalar followed by a signing
// component — which is why WebCrypto's Ed25519 cannot produce one and the scalar is drawn
// directly rather than hashed from a seed.
const MESHCORE_SCALAR_BYTES = 32;
const MESHCORE_PRIVATE_KEY_BYTES = 64;

// `protocol`. The Ed25519 group order L from RFC 8032. The base point has this order, so
// reducing before multiplying changes no public key — but a clamped scalar exceeds L and
// the curve rejects it unreduced.
const ED25519_GROUP_ORDER = 2n ** 252n + 27742317777372353535851937790883648493n;

// 30 KB of curve arithmetic that only the mining loop needs; the flash path never loads it.
let loadedCurve = null;
async function loadCurve() {
  if (!loadedCurve) loadedCurve = await import('../vendor/noble-ed25519/index.js');
  return loadedCurve;
}

function bytesToHex(bytes) {
  return Array.from(bytes, (b) => b.toString(16).padStart(2, '0')).join('');
}

// RFC 8032 clamping: clear the low three bits, clear bit 255, set bit 254.
function clampScalar(scalar) {
  const clamped = new Uint8Array(scalar);
  clamped[0] &= 248;
  clamped[31] &= 63;
  clamped[31] |= 64;
  return clamped;
}

function scalarToBigIntLE(bytes) {
  let value = 0n;
  for (let i = bytes.length - 1; i >= 0; i -= 1) value = (value << 8n) | BigInt(bytes[i]);
  return value;
}

/**
 * A fresh MeshCore identity, generated here and never transmitted. Returns
 * `{ privateKeyHex, publicKeyHex, prefix }`; only `prefix` is ever sent anywhere.
 */
export async function generateIdentityKeypair() {
  const { Point } = await loadCurve();
  const scalar = clampScalar(crypto.getRandomValues(new Uint8Array(MESHCORE_SCALAR_BYTES)));
  const signingComponent = crypto.getRandomValues(new Uint8Array(MESHCORE_SCALAR_BYTES));

  const expanded = new Uint8Array(MESHCORE_PRIVATE_KEY_BYTES);
  expanded.set(scalar, 0);
  expanded.set(signingComponent, MESHCORE_SCALAR_BYTES);

  const publicKey = Point.BASE.multiply(scalarToBigIntLE(scalar) % ED25519_GROUP_ORDER).toBytes();
  const publicKeyHex = bytesToHex(publicKey).toUpperCase();

  return { privateKeyHex: bytesToHex(expanded), publicKeyHex, prefix: extractPrefix(publicKeyHex) };
}

/** The device echoes its own public key after `set prv.key`; it must match what we generated. */
export function publicKeysMatch(expected, actual) {
  const clean = (hex) => String(hex).replace(/[^0-9a-f]/gi, '').toUpperCase();
  return clean(expected).length > 0 && clean(expected) === clean(actual);
}

/** @typedef {{ prefix: string, available: boolean|null, reason: string, message: string, reachable: boolean }} PrefixStatus */

export function normalisePrefix(prefix) {
  return String(prefix).trim().toUpperCase();
}

export function isValidPrefix(prefix) {
  const normalised = normalisePrefix(prefix);
  return normalised.length === GCM_PREFIX_LENGTH && /^[0-9A-F]+$/.test(normalised);
}

// All-zeros and all-Fs are reserved by convention; the registry refuses them anyway.
export function isUsablePrefix(prefix) {
  const normalised = normalisePrefix(prefix);
  return normalised !== '0000' && normalised !== 'FFFF';
}

export function extractPrefix(publicKeyHex) {
  const hex = String(publicKeyHex).replace(/[^0-9a-f]/gi, '').toUpperCase();
  if (hex.length < GCM_PREFIX_LENGTH) throw new Error('Public key is too short to extract a prefix.');
  return hex.slice(0, GCM_PREFIX_LENGTH);
}

// Anything that is not a real answer from the registry. Callers proceed without it.
function unreachable(prefix, message) {
  return { prefix, available: null, reason: 'unreachable', message, reachable: false };
}

async function getJson(url, timeoutMs, init = {}) {
  const abort = new AbortController();
  const timer = setTimeout(() => abort.abort(), timeoutMs);
  try {
    const res = await fetch(url, { ...init, signal: abort.signal, headers: { Accept: 'application/json', ...init.headers } });
    return { res, body: await res.json().catch(() => null) };
  } finally {
    clearTimeout(timer);
  }
}

/**
 * Is this prefix free? Never throws for an unreachable registry — a down, slow or
 * CORS-blocked service returns `reachable: false` and the caller carries on unregistered.
 * An invalid prefix is a programmer error and does throw.
 */
export async function checkPrefixAvailable(
  prefix,
  { baseUrl = GCM_REGISTRY_BASE, timeoutMs = GCM_REGISTRY_TIMEOUT_MS } = {}
) {
  const normalised = normalisePrefix(prefix);
  if (!isValidPrefix(normalised)) {
    throw new Error(`Invalid prefix '${prefix}'. Expected ${GCM_PREFIX_LENGTH} hex characters.`);
  }
  // Answered without a request: their own rule, and it saves a round trip.
  if (!isUsablePrefix(normalised)) {
    return {
      prefix: normalised,
      available: false,
      reason: 'unusable',
      message: 'Prefix is reserved by convention (all zeros or all Fs).',
      reachable: true,
    };
  }

  try {
    const { res, body } = await getJson(`${baseUrl}/api/prefix/${normalised}`, timeoutMs);
    if (!res.ok || typeof body?.available !== 'boolean') {
      return unreachable(normalised, `Registry answered HTTP ${res.status}.`);
    }
    return {
      prefix: normalised,
      available: body.available,
      reason: body.reason ?? (body.available ? 'available' : 'taken'),
      message: body.message ?? '',
      reachable: true,
    };
  } catch (error) {
    // Timeout, DNS, offline, CORS — all the same to the caller: no answer, keep going.
    return unreachable(normalised, error.name === 'AbortError' ? 'Registry did not answer in time.' : error.message);
  }
}

/** Convenience: check the prefix of a public key read off a device. */
export async function checkPublicKey(publicKeyHex, options) {
  return checkPrefixAvailable(extractPrefix(publicKeyHex), options);
}

/**
 * Mine a keypair whose prefix the registry will accept. Each attempt is one scalar multiply,
 * so the loop costs microseconds — the device is never involved. Returns the first accepted
 * keypair, or null when none was accepted or the registry never answered; the caller flashes
 * anyway and skips registration.
 */
export async function findAvailablePrefix(generateKeypair = generateIdentityKeypair, { attempts = 15, onProgress, ...options } = {}) {
  for (let attempt = 1; attempt <= attempts; attempt += 1) {
    onProgress?.(attempt, attempts);
    const keypair = await generateKeypair();
    const status = await checkPrefixAvailable(extractPrefix(keypair.publicKeyHex), options);
    // A registry that is not answering will not answer for attempt 2 either.
    if (!status.reachable) return null;
    if (status.available) return { keypair, status };
  }
  return null;
}

/**
 * Claim a prefix. **Creates a real, public record against the supplied email** — call it
 * only from a deliberate user action, never speculatively. Nothing in this tool calls it.
 * Untested against the live service: the read path above is verified, this is not.
 */
export async function reservePrefix(
  { prefix, name, email, lat, lon, altitude = 0 },
  { baseUrl = GCM_REGISTRY_BASE, timeoutMs = GCM_REGISTRY_TIMEOUT_MS } = {}
) {
  const normalised = normalisePrefix(prefix);
  if (!isValidPrefix(normalised)) throw new Error(`Invalid prefix '${prefix}'.`);

  try {
    const { res, body } = await getJson(`${baseUrl}/api/reserve`, timeoutMs, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ prefix: normalised, name, email, lat, lon, altitude }),
    });
    if (!res.ok) return { reserved: false, reachable: true, message: body?.error ?? `HTTP ${res.status}.` };
    return { reserved: true, reachable: true, message: body?.message ?? 'Reserved.' };
  } catch (error) {
    return { reserved: false, reachable: false, message: error.message };
  }
}
