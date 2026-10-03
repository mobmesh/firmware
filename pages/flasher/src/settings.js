const MOD_BITS = { 'hotspot-ota': 0x02, 'sync-settings': 0x04 };
const META_MAGIC = [77, 79, 66, 77, 69, 83, 72, 0];

export function flashedModBits(plan) {
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
