// SPIFFS image reader — the on-flash format only.
//
// Its own module for the same reason as `partitions.js`: a fixed on-flash format
// with no device, I/O or policy knowledge. C5 also requires that this be
// structurally unreachable from the DFU path — nRF52 cannot read flash back, so a
// stray call would fail confusingly mid-flash. Nothing here imports anything.
//
// Reads an image, and builds one. A rebuild is always from a parsed file set, never
// in place, which is what lets the partition change size (§10.6): every block's
// lookup magic derives from the image's block count, so a raw copy into a
// differently sized partition produces something SPIFFS refuses to mount.
//
// Geometry and field layout follow ESP-IDF's `spiffsgen.py`. Fixed to 2-byte ids
// and little-endian, which is what every board here uses.

const OBJ_ID_BYTES = 2;
const SPAN_INDEX_BYTES = 2;
const PAGE_INDEX_BYTES = 2;
const FLAG_BYTES = 1;
const INDEX_SIZE_BYTES = 4;
const INDEX_OBJ_TYPE_BYTES = 1;

// Page flag values. A page counts only when it is both used and final.
const FLAG_USED_FINAL_INDEX = 0xf8;
const FLAG_USED_FINAL = 0xfc;

// Object type marker in a span-0 index page. Only regular files are written.
const TYPE_FILE = 1;

// Erased flash for a 2-byte id — a lookup slot reading this means "no page here".
const EMPTY_OBJ_ID = 0xffff;
// High bit of an object id marks an object *index* page rather than a data page.
const OBJ_ID_INDEX_FLAG = 1 << (OBJ_ID_BYTES * 8 - 1);

// Layout arithmetic for one geometry; every value mirrors `spiffsgen.py`.
export function spiffsGeometry({ pageSize = 256, blockSize = 4096, nameBytes = 32, metaBytes = 4 } = {}) {
  if (blockSize % pageSize !== 0) throw new Error('SPIFFS block size must be a multiple of the page size');

  const pagesPerBlock = Math.floor(blockSize / pageSize);
  const lookupPagesPerBlock = Math.ceil((pagesPerBlock * OBJ_ID_BYTES) / pageSize);

  // Common page header: object id + span index + flags. Index pages pad this out
  // to a 4-byte boundary before their own fields; data pages do not, so the
  // payload split uses the *unaligned* length.
  const headerBytes = OBJ_ID_BYTES + SPAN_INDEX_BYTES + FLAG_BYTES;
  const headerPadding = 4 - (headerBytes % 4 === 0 ? 4 : headerBytes % 4);

  // A span-0 index page carries size, type, name and meta before its page table;
  // later spans are pure page tables and so hold more entries.
  const alignedHeaderBytes = headerBytes + headerPadding;
  const firstSpanHeaderBytes =
    alignedHeaderBytes + INDEX_SIZE_BYTES + INDEX_OBJ_TYPE_BYTES + nameBytes + metaBytes;

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
    alignedHeaderBytes,
    dataPageContentBytes: pageSize - headerBytes,
    lookupSlotsPerPage: Math.floor(pageSize / OBJ_ID_BYTES),
    firstSpanPageCapacity: Math.floor((pageSize - firstSpanHeaderBytes) / PAGE_INDEX_BYTES),
    laterSpanPageCapacity: Math.floor((pageSize - alignedHeaderBytes) / PAGE_INDEX_BYTES),
  };
}

/**
 * The geometry every supported board actually uses — ESP-IDF / Arduino-ESP32
 * defaults, which this firmware's `SPIFFS.begin()` does not override.
 */
export const DEFAULT_SPIFFS_GEOMETRY = spiffsGeometry();

// Two passes per block: a span-0 index page carries the name and size, the bytes live in
// numbered data pages that may sit in any block. Tolerant — it is pointed at whatever was on
// the device. Anything that does not decode is skipped, so callers must not read a short list
// as "nothing here"; that distinction comes from a read that either succeeded or raised.
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

// --- Builder --------------------------------------------------------------

// Means "cannot restore", never a flash failure.
export class SpiffsFullError extends Error {
  constructor(message = 'The filesystem image is full') {
    super(message);
    this.name = 'SpiffsFullError';
  }
}

// `protocol`. The seed SPIFFS mixes into every block's lookup magic. A block whose
// magic does not match what the driver derives is treated as unformatted.
const MAGIC_SEED = 0x20140529;

// Derived from the page size and the image's block count — which is exactly why a filesystem
// cannot be copied into a differently sized partition.
function blockMagic(blockIndex, blockCount, geometry) {
  const magic = (MAGIC_SEED ^ geometry.pageSize) ^ (blockCount - blockIndex);
  return magic & 0xffff;
}

// Always a full rebuild, never an edit — that is what allows a resized partition (§10.6).
// The whole image is produced because the magic in the *unused* blocks marks the rest of the
// partition formatted; without it SPIFFS reformats on first mount. Object ids start at 1.
export function buildSpiffsImage(files, imageBytes, geometry = DEFAULT_SPIFFS_GEOMETRY) {
  if (imageBytes % geometry.blockSize !== 0) {
    throw new Error('SPIFFS image size must be a whole number of blocks');
  }

  const state = {
    geometry,
    blocks: [],
    blockCount: Math.floor(imageBytes / geometry.blockSize),
    nextObjId: 1,
  };

  for (const file of files) addFile(state, file.name, file.data);
  return serialiseImage(state, imageBytes);
}

/** The last block if it still has a free page, otherwise a new one. */
function blockWithSpace(state) {
  const last = state.blocks[state.blocks.length - 1];
  if (last && last.freePages > 0) return last;

  if (state.blocks.length >= state.blockCount) throw new SpiffsFullError();
  const block = {
    index: state.blocks.length,
    lookupSlots: [],
    pages: [],
    freePages: state.geometry.usablePagesPerBlock,
  };
  state.blocks.push(block);
  return block;
}

/** Absolute page index within the image — what an index page's table stores. */
function absolutePageIndex(state, block) {
  return block.index * state.geometry.pagesPerBlock + state.geometry.lookupPagesPerBlock + block.pages.length;
}

function claimPage(state, block, page, isIndexPage) {
  if (block.lookupSlots.length >= state.geometry.lookupSlotsPerPage) throw new SpiffsFullError();
  block.lookupSlots.push({ objId: page.objId, isIndexPage });
  block.pages.push(page);
  block.freePages -= 1;
  return page;
}

function beginObject(state, block, objId, size, name, spanIndex) {
  return claimPage(
    state,
    block,
    {
      kind: 'index',
      objId,
      spanIndex,
      size,
      name,
      dataPageIndexes: [],
      capacity: spanIndex === 0 ? state.geometry.firstSpanPageCapacity : state.geometry.laterSpanPageCapacity,
    },
    true
  );
}

function addFile(state, name, data) {
  const encodedName = new TextEncoder().encode(name);
  if (encodedName.length > state.geometry.nameBytes) {
    throw new Error(`SPIFFS object name '${name}' is longer than ${state.geometry.nameBytes} bytes`);
  }

  const objId = state.nextObjId;
  let indexSpan = 0;
  let dataSpan = 0;
  // A zero-length file is legitimate and gets an index page and no data pages.
  let indexPage = beginObject(state, blockWithSpace(state), objId, data.length, name, indexSpan++);

  let offset = 0;
  while (offset < data.length) {
    // An index page's table is finite. When it fills, the object continues under a
    // further index span rather than the file being split — later spans carry no
    // name or size and so hold more entries.
    if (indexPage.dataPageIndexes.length >= indexPage.capacity) {
      indexPage = beginObject(state, blockWithSpace(state), objId, data.length, name, indexSpan++);
    }

    // Re-resolved every iteration: the page above may have been the block's last.
    const block = blockWithSpace(state);
    const take = Math.min(state.geometry.dataPageContentBytes, data.length - offset);
    const page = { kind: 'data', objId, spanIndex: dataSpan++, content: data.subarray(offset, offset + take) };
    indexPage.dataPageIndexes.push(absolutePageIndex(state, block));
    claimPage(state, block, page, false);
    offset += take;
  }

  state.nextObjId += 1;
}

function serialiseImage(state, imageBytes) {
  const { geometry } = state;
  const image = new Uint8Array(imageBytes).fill(0xff);

  // Every block, not just the ones holding files — see buildSpiffsImage.
  for (let blockIndex = 0; blockIndex < state.blockCount; blockIndex += 1) {
    const block = state.blocks[blockIndex];
    const base = blockIndex * geometry.blockSize;

    const lookup = new DataView(image.buffer, base, geometry.pageSize);
    const slots = block ? block.lookupSlots : [];
    slots.forEach((slot, position) => {
      lookup.setUint16(
        position * OBJ_ID_BYTES,
        slot.isIndexPage ? slot.objId ^ OBJ_ID_INDEX_FLAG : slot.objId,
        true
      );
    });
    // Second-to-last slot. Slots between the real entries and it stay 0xffff,
    // which is already the erased fill.
    const magicSlot = geometry.lookupSlotsPerPage - 2;
    if (slots.length <= magicSlot) {
      lookup.setUint16(magicSlot * OBJ_ID_BYTES, blockMagic(blockIndex, state.blockCount, geometry), true);
    }

    if (!block) continue;

    block.pages.forEach((page, position) => {
      const pageStart = base + (geometry.lookupPagesPerBlock + position) * geometry.pageSize;
      serialisePage(image, pageStart, page, geometry);
    });
  }

  return image;
}

function serialisePage(image, start, page, geometry) {
  const view = new DataView(image.buffer, start, geometry.pageSize);
  const isIndexPage = page.kind === 'index';

  view.setUint16(0, isIndexPage ? page.objId ^ OBJ_ID_INDEX_FLAG : page.objId, true);
  view.setUint16(OBJ_ID_BYTES, page.spanIndex, true);
  view.setUint8(OBJ_ID_BYTES + SPAN_INDEX_BYTES, isIndexPage ? FLAG_USED_FINAL_INDEX : FLAG_USED_FINAL);

  if (!isIndexPage) {
    image.set(page.content, start + geometry.headerBytes);
    return;
  }

  let offset = geometry.alignedHeaderBytes;
  if (page.spanIndex === 0) {
    view.setUint32(offset, page.size, true);
    offset += INDEX_SIZE_BYTES;
    view.setUint8(offset, TYPE_FILE);
    offset += INDEX_OBJ_TYPE_BYTES;

    // Name through meta is zero-filled, not left at the erased 0xff — the driver
    // reads the field as a C string and 0xff is not a terminator.
    const encodedName = new TextEncoder().encode(page.name);
    image.fill(0, start + offset, start + offset + geometry.nameBytes + geometry.metaBytes);
    image.set(encodedName, start + offset);
    offset += geometry.nameBytes + geometry.metaBytes;
  }

  for (const pageIndex of page.dataPageIndexes) {
    view.setUint16(offset, pageIndex, true);
    offset += PAGE_INDEX_BYTES;
  }
}

// Lets a restore write only the part of a backup holding anything. SPIFFS only.
export function spiffsUsedBytes(image, geometry = DEFAULT_SPIFFS_GEOMETRY) {
  const blockCount = Math.floor(image.length / geometry.blockSize);
  for (let blockIndex = blockCount - 1; blockIndex >= 0; blockIndex -= 1) {
    const lookup = new DataView(image.buffer, image.byteOffset + blockIndex * geometry.blockSize, geometry.pageSize);
    for (let slot = 0; slot < geometry.usablePagesPerBlock; slot += 1) {
      if (lookup.getUint16(slot * OBJ_ID_BYTES, true) !== EMPTY_OBJ_ID) {
        return (blockIndex + 1) * geometry.blockSize;
      }
    }
  }
  return 0;
}
