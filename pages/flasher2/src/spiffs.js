// SPIFFS image reader — the on-flash format only.
//
// Its own module for the same reason as `partitions.js`: a fixed on-flash format
// with no device, I/O or policy knowledge. C5 also requires that this be
// structurally unreachable from the DFU path — nRF52 cannot read flash back, so a
// stray call would fail confusingly mid-flash. Nothing here imports anything.
//
// Read-only. §10.6's rebuild-for-a-resized-partition is a separate concern and is
// not here yet; §10.5 only needs to know *what files exist*.
//
// Geometry and field layout follow ESP-IDF's `spiffsgen.py`. Fixed to 2-byte ids
// and little-endian, which is what every board here uses.

const OBJ_ID_BYTES = 2;
const SPAN_INDEX_BYTES = 2;
const BLOCK_INDEX_BYTES = 2;
const FLAG_BYTES = 1;
const INDEX_SIZE_BYTES = 4;
const INDEX_OBJ_TYPE_BYTES = 1;

// Page flag values. A page counts only when it is both used and final.
const FLAG_USED_FINAL_INDEX = 0xf8;
const FLAG_USED_FINAL = 0xfc;

// Erased flash for a 2-byte id — a lookup slot reading this means "no page here".
const EMPTY_OBJ_ID = 0xffff;
// High bit of an object id marks an object *index* page rather than a data page.
const OBJ_ID_INDEX_FLAG = 1 << (OBJ_ID_BYTES * 8 - 1);

/**
 * Layout arithmetic for one SPIFFS geometry. Every derived value mirrors
 * `spiffsgen.py`'s field of the same meaning.
 */
export function spiffsGeometry({ pageSize = 256, blockSize = 4096, nameBytes = 32, metaBytes = 4 } = {}) {
  if (blockSize % pageSize !== 0) throw new Error('SPIFFS block size must be a multiple of the page size');

  const pagesPerBlock = Math.floor(blockSize / pageSize);
  const lookupPagesPerBlock = Math.ceil((pagesPerBlock * OBJ_ID_BYTES) / pageSize);

  // Common page header: object id + span index + flags. Index pages pad this out
  // to a 4-byte boundary before their own fields; data pages do not, so the
  // payload split uses the *unaligned* length.
  const headerBytes = OBJ_ID_BYTES + SPAN_INDEX_BYTES + FLAG_BYTES;
  const headerPadding = 4 - (headerBytes % 4 === 0 ? 4 : headerBytes % 4);

  return {
    pageSize,
    blockSize,
    nameBytes,
    metaBytes,
    pagesPerBlock,
    lookupPagesPerBlock,
    usablePagesPerBlock: pagesPerBlock - lookupPagesPerBlock,
    headerBytes,
    headerPadding,
    dataPageContentBytes: pageSize - headerBytes,
    indexEntryBytes: BLOCK_INDEX_BYTES,
  };
}

/**
 * The geometry every supported board actually uses — ESP-IDF / Arduino-ESP32
 * defaults, which this firmware's `SPIFFS.begin()` does not override.
 */
export const DEFAULT_SPIFFS_GEOMETRY = spiffsGeometry();

/**
 * Recover the files in a SPIFFS image.
 *
 * Two passes over each block, because SPIFFS stores a file's identity and its
 * contents in different places: a span-0 *index* page carries the name and size,
 * while the bytes live in numbered data pages that may sit in any block. Pages are
 * reassembled in span order at the end.
 *
 * Tolerant by design — it is pointed at whatever was on the device, which may be a
 * blank partition, another project's filesystem, or a partial write. Anything that
 * does not decode is skipped, and the answer is simply a shorter list. Callers use
 * the result as *evidence*, so a wrong "there is nothing here" is the expensive
 * error and an unparsable image must never masquerade as an empty one — that
 * distinction is the caller's, drawn from a read that either succeeded or raised.
 *
 * @param {Uint8Array} image
 * @returns {{ name: string, size: number, data: Uint8Array }[]}
 */
export function readSpiffsFiles(image, geometry = DEFAULT_SPIFFS_GEOMETRY) {
  const files = new Map(); // realObjId -> { name, size, pages: [spanIndex, bytes][] }
  const decoder = new TextDecoder();

  const fileFor = (objId) => {
    if (!files.has(objId)) files.set(objId, { name: null, size: 0, pages: [] });
    return files.get(objId);
  };

  const blockCount = Math.floor(image.length / geometry.blockSize);
  for (let blockIndex = 0; blockIndex < blockCount; blockIndex++) {
    const blockStart = blockIndex * geometry.blockSize;
    const block = image.subarray(blockStart, blockStart + geometry.blockSize);

    // The block's lookup page announces which objects have pages here. Slots past
    // the usable-page count are padding and the block magic, not object ids.
    const lookup = new DataView(block.buffer, block.byteOffset, geometry.pageSize);
    for (let slot = 0; slot < geometry.usablePagesPerBlock; slot++) {
      const objId = lookup.getUint16(slot * OBJ_ID_BYTES, true);
      if (objId === EMPTY_OBJ_ID) continue;
      if ((objId & OBJ_ID_INDEX_FLAG) === 0) continue;
      fileFor(objId & ~OBJ_ID_INDEX_FLAG);
    }

    for (let pageIndex = geometry.lookupPagesPerBlock; pageIndex < geometry.pagesPerBlock; pageIndex++) {
      const pageStart = pageIndex * geometry.pageSize;
      readPage(block.subarray(pageStart, pageStart + geometry.pageSize), geometry, files, fileFor, decoder);
    }
  }

  const result = [];
  for (const file of files.values()) {
    // A file whose index page was never found has no name and no declared size.
    // Skipping it is right: half a file is not evidence of anything.
    if (file.name === null) continue;

    file.pages.sort((a, b) => a[0] - b[0]);
    const data = new Uint8Array(file.size);
    let written = 0;
    for (const [, content] of file.pages) {
      if (written >= file.size) break;
      const take = Math.min(content.length, file.size - written);
      data.set(content.subarray(0, take), written);
      written += take;
    }
    // `subarray(0, written)` rather than the declared size: a truncated file
    // reports the bytes actually recovered, not zero padding that was never there.
    result.push({ name: file.name, size: file.size, data: data.subarray(0, written) });
  }
  return result;
}

function readPage(page, geometry, files, fileFor, decoder) {
  if (page.length < geometry.headerBytes) return;

  const view = new DataView(page.buffer, page.byteOffset, page.length);
  const objId = view.getUint16(0, true);
  if (objId === EMPTY_OBJ_ID) return;

  const spanIndex = view.getUint16(OBJ_ID_BYTES, true);
  const flags = view.getUint8(OBJ_ID_BYTES + SPAN_INDEX_BYTES);
  const isIndexPage = (objId & OBJ_ID_INDEX_FLAG) !== 0;
  const realObjId = objId & ~OBJ_ID_INDEX_FLAG;

  if (isIndexPage) {
    if (flags !== FLAG_USED_FINAL_INDEX) return;
    fileFor(realObjId);
    // Only span 0 carries the name and size; later spans are pure page tables.
    if (spanIndex === 0) readIndexPage(page, view, geometry, files.get(realObjId), decoder);
    return;
  }

  if (flags !== FLAG_USED_FINAL) return;
  // Data pages for an object whose lookup slot was never seen are ignored rather
  // than inventing a file: without an index page there is no name or length.
  const file = files.get(realObjId);
  if (!file) return;
  const content = page.subarray(geometry.headerBytes, geometry.headerBytes + geometry.dataPageContentBytes);
  file.pages.push([spanIndex, content]);
}

function readIndexPage(page, view, geometry, file, decoder) {
  let offset = geometry.headerBytes + geometry.headerPadding;
  if (offset + INDEX_SIZE_BYTES + INDEX_OBJ_TYPE_BYTES > page.length) return;

  const size = view.getUint32(offset, true);
  offset += INDEX_SIZE_BYTES + INDEX_OBJ_TYPE_BYTES;

  const nameEnd = offset + geometry.nameBytes;
  if (nameEnd > page.length) return;

  const nameBytes = page.subarray(offset, nameEnd);
  const terminator = nameBytes.indexOf(0);
  file.name = decoder.decode(terminator === -1 ? nameBytes : nameBytes.subarray(0, terminator));
  file.size = size;
}
