// The flash plan — rewrite_code.md §4.1.
//
// Every path resolves to one of these before any hardware is touched. Manifest
// resolvers produce it; engines consume it; neither knows about the other. This
// is the reuse boundary that keeps the custom and stock ESP32 paths on one
// executor (C3) and the two manifests on one resolver (C6).

/**
 * @typedef {{ data: ArrayBuffer, address: number }} FlashFile
 *
 * @typedef {object} FlashPlan
 * @property {'esptool'|'dfu'} engine
 * @property {FlashFile[]} files        esptool only; empty for dfu
 * @property {Blob|null} package        dfu only
 * @property {boolean} eraseAll
 * @property {boolean} preserveFs       esp32 custom only
 * @property {{ sha256: string }|null} verify
 * @property {{ commands: string[] }|null} postFlash
 */

/**
 * @param {Partial<FlashPlan> & { engine: 'esptool'|'dfu' }} fields
 * @returns {FlashPlan}
 */
export function createFlashPlan(fields) {
  return {
    engine: fields.engine,
    files: fields.files ?? [],
    package: fields.package ?? null,
    eraseAll: fields.eraseAll ?? false,
    preserveFs: fields.preserveFs ?? false,
    // Null means "no checksum was supplied", which the UI must state rather than
    // silently skip: stock firmware arrives through the relay without one unless
    // the manifest carries it, custom firmware always has its sidecar (C7).
    verify: fields.verify ?? null,
    postFlash: fields.postFlash ?? null,
  };
}
