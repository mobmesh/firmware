const MOD_BITS = { 'hotspot-ota': 0x02, 'sync-settings': 0x04 };
const META_MAGIC = [77, 79, 66, 77, 69, 83, 72, 0];

// Strings each mod compiles into its image (`image_marker` in its mod.yaml). An nRF52 package
// carries no stamped metadata, so its bits are read the way CI reads them, from the binary.
const MOD_IMAGE_MARKERS = { 'hotspot-ota': 'H0TSP0T', 'sync-settings': 'set sync.channel' };

function containsBytes(haystack, needle) {
  outer: for (let i = 0; i + needle.length <= haystack.length; i++) {
    for (let j = 0; j < needle.length; j++) if (haystack[i + j] !== needle[j]) continue outer;
    return true;
  }
  return false;
}

/** The mod bits an application binary proves by carrying each mod's marker string. */
export function modBitsFromImage(bin) {
  let bits = 0;
  for (const [mod, marker] of Object.entries(MOD_IMAGE_MARKERS)) {
    if (containsBytes(bin, new TextEncoder().encode(marker))) bits |= MOD_BITS[mod];
  }
  return bits;
}

export function flashedModBits(plan) {
  if (plan.engine === 'dfu') {
    if (!Number.isInteger(plan.modBits)) throw new Error('Enhanced nRF52 plan carries no mod bits.');
    return plan.modBits;
  }
  for (const file of plan.files) {
    const bytes = file.data instanceof Uint8Array ? file.data : new Uint8Array(file.data);
    if (file.address < 0x10000 || bytes[0] !== 0xe9) continue;
    if (bytes.length < 288) throw new Error('Enhanced firmware has no supported mod metadata; regional setup cannot be selected safely.');
    const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
    let marker = -1;
    for (let offset = 208; offset <= 280; offset++) {
      if (offset >= 236 && bytes[offset - 1] === 1 && META_MAGIC.every((value, i) => bytes[offset + i] === value) &&
          view.getUint16(offset - 4, true) && bytes[offset - 2]) {
        marker = offset;
        break;
      }
    }
    if (marker < 0) {
      throw new Error('Enhanced firmware has no supported mod metadata; regional setup cannot be selected safely.');
    }
    return view.getUint32(marker - 8, true);
  }
  throw new Error('Enhanced flash plan has no application image.');
}

export function resolveSettings(commands, modBits = 0) {
  if (!Array.isArray(commands)) throw new Error('Regional commands must be an array.');
  const result = [];
  for (const command of commands) {
    if (typeof command === 'string') {
      if (command.startsWith('_')) continue;
      if (!command.length || /[\r\n\0]/.test(command) || new TextEncoder().encode(command).length > 159) {
        throw new Error('Invalid or oversized regional CLI command.');
      }
      result.push(command);
    } else {
      if (!command || !Object.hasOwn(MOD_BITS, command.if_mod) ||
          !Array.isArray(command.then) || !Array.isArray(command.else)) {
        throw new Error('Invalid regional mod condition.');
      }
      const branch = (modBits & MOD_BITS[command.if_mod]) !== 0 ? command.then : command.else;
      if (branch.some((item) => typeof item !== 'string')) {
        throw new Error('Conditional regional branches must contain command strings.');
      }
      result.push(...resolveSettings(branch, modBits));
    }
  }
  return result;
}
