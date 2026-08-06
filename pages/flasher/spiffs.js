// SPIFFS image reader/builder, ported from Jason2866/esp32tool (MIT), itself a
// port of ESP-IDF's spiffsgen.py. Always a full rebuild from the parsed file
// set, never in-place, which is what lets partitions differ in size.
// Fixed to 2-byte ids and little-endian, matching every target here.

const SPIFFS_PH_FLAG_USED_FINAL_INDEX = 0xf8;
const SPIFFS_PH_FLAG_USED_FINAL = 0xfc;
const SPIFFS_PH_FLAG_LEN = 1;
const SPIFFS_PH_IX_SIZE_LEN = 4;
const SPIFFS_PH_IX_OBJ_TYPE_LEN = 1;
const SPIFFS_TYPE_FILE = 1;

const OBJ_ID_LEN = 2;   // spiffs_obj_id
const SPAN_IX_LEN = 2;  // spiffs_span_ix
const PAGE_IX_LEN = 2;  // spiffs_page_ix
const BLOCK_IX_LEN = 2; // spiffs_block_ix

// Erased-flash value for a 2-byte object id -- a lookup slot reading this
// means "no page here".
const EMPTY_OBJ_ID = 0xffff;
// High bit of an object id marks the page as an object *index* page rather
// than a data page.
const OBJ_ID_IX_FLAG = 1 << (OBJ_ID_LEN * 8 - 1);

export class SpiffsFullError extends Error {
  constructor(message = "SPIFFS is full") {
    super(message);
    this.name = "SpiffsFullError";
  }
}

// Layout arithmetic for one SPIFFS geometry. Every derived value here mirrors
// spiffsgen.py's SpiffsBuildConfig of the same name.
export class SpiffsBuildConfig {
  constructor({ pageSize = 256, blockSize = 4096, objNameLen = 32, metaLen = 4, useMagic = true, useMagicLen = true } = {}) {
    if (blockSize % pageSize !== 0) {
      throw new Error("block size should be a multiple of page size");
    }

    this.pageSize = pageSize;
    this.blockSize = blockSize;
    this.objNameLen = objNameLen;
    this.metaLen = metaLen;
    this.useMagic = useMagic;
    this.useMagicLen = useMagicLen;

    this.objIdLen = OBJ_ID_LEN;
    this.spanIxLen = SPAN_IX_LEN;
    this.pageIxLen = PAGE_IX_LEN;
    this.blockIxLen = BLOCK_IX_LEN;

    this.PAGES_PER_BLOCK = Math.floor(blockSize / pageSize);
    this.OBJ_LU_PAGES_PER_BLOCK = Math.ceil((this.PAGES_PER_BLOCK * this.objIdLen) / pageSize);
    this.OBJ_USABLE_PAGES_PER_BLOCK = this.PAGES_PER_BLOCK - this.OBJ_LU_PAGES_PER_BLOCK;
    this.OBJ_LU_PAGES_OBJ_IDS_LIM = Math.floor(pageSize / this.objIdLen);

    // Common page header: obj id + span index + flags, then padded out to a
    // 4-byte boundary for the index-page variants that follow it.
    this.OBJ_DATA_PAGE_HEADER_LEN = this.objIdLen + this.spanIxLen + SPIFFS_PH_FLAG_LEN;
    const pad = 4 - (this.OBJ_DATA_PAGE_HEADER_LEN % 4 === 0 ? 4 : this.OBJ_DATA_PAGE_HEADER_LEN % 4);
    this.OBJ_DATA_PAGE_HEADER_LEN_ALIGNED = this.OBJ_DATA_PAGE_HEADER_LEN + pad;
    this.OBJ_DATA_PAGE_HEADER_LEN_ALIGNED_PAD = pad;
    // Data pages carry the unaligned header only -- the padding above applies
    // to index pages, not to the payload split.
    this.OBJ_DATA_PAGE_CONTENT_LEN = pageSize - this.OBJ_DATA_PAGE_HEADER_LEN;

    // A span-0 index page additionally carries size, type, name and meta
    // before its page-index table starts.
    this.OBJ_INDEX_PAGES_HEADER_LEN =
      this.OBJ_DATA_PAGE_HEADER_LEN_ALIGNED +
      SPIFFS_PH_IX_SIZE_LEN +
      SPIFFS_PH_IX_OBJ_TYPE_LEN +
      objNameLen +
      metaLen;

    this.OBJ_INDEX_PAGES_OBJ_IDS_HEAD_LIM = Math.floor((pageSize - this.OBJ_INDEX_PAGES_HEADER_LEN) / this.blockIxLen);
    this.OBJ_INDEX_PAGES_OBJ_IDS_LIM = Math.floor((pageSize - this.OBJ_DATA_PAGE_HEADER_LEN_ALIGNED) / this.blockIxLen);
  }
}

// The geometry every board this flasher supports actually uses -- ESP-IDF /
// Arduino-ESP32 defaults, unmodified by this firmware's SPIFFS.begin() calls.
export const DEFAULT_SPIFFS_CONFIG = new SpiffsBuildConfig({ pageSize: 256, blockSize: 4096 });

// ---- Builder ---------------------------------------------------------

// One 2-byte object id per usable page in the block, in fill order
class SpiffsObjLuPage {
  constructor(bix, cfg) {
    this.bix = bix;
    this.cfg = cfg;
    this.objIdsLimit = cfg.OBJ_LU_PAGES_OBJ_IDS_LIM;
    this.objIds = []; // [id, isIndex]
  }

  // SPIFFS stores a derived constant in the tail of each block's lookup page
  // and refuses to mount an image whose blocks don't carry it.
  calcMagic(blocksLim) {
    let magic = 0x20140529 ^ this.cfg.pageSize;
    if (this.cfg.useMagicLen) magic ^= blocksLim - this.bix;
    return magic & ((1 << (this.cfg.objIdLen * 8)) - 1);
  }

  registerPage(objId, isIndex) {
    if (this.objIdsLimit <= 0) throw new SpiffsFullError();
    this.objIds.push([objId, isIndex]);
    this.objIdsLimit--;
  }

  magicfy(blocksLim) {
    const remaining = this.objIdsLimit;
    if (remaining < 2) return;
    for (let i = 0; i < remaining; i++) {
      if (i === remaining - 2) {
        this.objIds.push([this.calcMagic(blocksLim), false]);
        break;
      }
      this.objIds.push([EMPTY_OBJ_ID, false]);
      this.objIdsLimit--;
    }
  }

  toBinary() {
    const img = new Uint8Array(this.cfg.pageSize).fill(0xff);
    const view = new DataView(img.buffer);
    let offset = 0;
    for (const [objId, isIndex] of this.objIds) {
      view.setUint16(offset, isIndex ? objId ^ OBJ_ID_IX_FLAG : objId, true);
      offset += this.cfg.objIdLen;
    }
    return img;
  }
}

// An object index page: span 0 additionally holds the file's size and name;
// every span holds a table of page indices pointing at that file's data pages.
class SpiffsObjIndexPage {
  constructor(objId, spanIx, size, name, cfg) {
    this.objId = objId;
    this.spanIx = spanIx;
    this.size = size;
    this.name = name;
    this.cfg = cfg;
    this.pagesLim = spanIx === 0 ? cfg.OBJ_INDEX_PAGES_OBJ_IDS_HEAD_LIM : cfg.OBJ_INDEX_PAGES_OBJ_IDS_LIM;
    this.pages = [];
  }

  registerPage(dataPageOffset) {
    if (this.pagesLim <= 0) throw new SpiffsFullError();
    this.pages.push(dataPageOffset);
    this.pagesLim--;
  }

  toBinary() {
    const img = new Uint8Array(this.cfg.pageSize).fill(0xff);
    const view = new DataView(img.buffer);

    view.setUint16(0, this.objId ^ OBJ_ID_IX_FLAG, true);
    view.setUint16(this.cfg.objIdLen, this.spanIx, true);
    view.setUint8(this.cfg.objIdLen + this.cfg.spanIxLen, SPIFFS_PH_FLAG_USED_FINAL_INDEX);

    let offset = this.cfg.OBJ_DATA_PAGE_HEADER_LEN + this.cfg.OBJ_DATA_PAGE_HEADER_LEN_ALIGNED_PAD;

    if (this.spanIx === 0) {
      view.setUint32(offset, this.size, true);
      offset += SPIFFS_PH_IX_SIZE_LEN;
      view.setUint8(offset, SPIFFS_TYPE_FILE);
      offset += SPIFFS_PH_IX_OBJ_TYPE_LEN;

      // Name through meta is null-padded, not left at the 0xFF erased fill.
      // spiffsgen.py zero-fills the whole run.
      const nameBytes = new TextEncoder().encode(this.name);
      const written = Math.min(nameBytes.length, this.cfg.objNameLen);
      img.set(nameBytes.subarray(0, written), offset);
      img.fill(0x00, offset + written, offset + this.cfg.objNameLen + this.cfg.metaLen);
      offset += this.cfg.objNameLen + this.cfg.metaLen;
    }

    for (const pageOffset of this.pages) {
      view.setUint16(offset, Math.floor(pageOffset / this.cfg.pageSize), true);
      offset += this.cfg.pageIxLen;
    }

    return img;
  }
}

// One page of file content, prefixed by the common page header.
class SpiffsObjDataPage {
  constructor(offset, objId, spanIx, contents, cfg) {
    this.offset = offset;
    this.objId = objId;
    this.spanIx = spanIx;
    this.contents = contents;
    this.cfg = cfg;
  }

  toBinary() {
    const img = new Uint8Array(this.cfg.pageSize).fill(0xff);
    const view = new DataView(img.buffer);
    view.setUint16(0, this.objId, true);
    view.setUint16(this.cfg.objIdLen, this.spanIx, true);
    view.setUint8(this.cfg.objIdLen + this.cfg.spanIxLen, SPIFFS_PH_FLAG_USED_FINAL);
    img.set(this.contents, this.cfg.OBJ_DATA_PAGE_HEADER_LEN);
    return img;
  }
}

// Lookup page(s) then however many index and data pages fit, filled in order
class SpiffsBlock {
  constructor(bix, cfg) {
    this.cfg = cfg;
    this.bix = bix;
    this.offset = bix * cfg.blockSize;
    this.remainingPages = cfg.OBJ_USABLE_PAGES_PER_BLOCK;
    this.luPages = [];
    for (let i = 0; i < cfg.OBJ_LU_PAGES_PER_BLOCK; i++) {
      this.luPages.push(new SpiffsObjLuPage(bix, cfg));
    }
    this.pages = [...this.luPages];
    this.luPageIx = 0;

    this.curObjIndexSpanIx = 0;
    this.curObjDataSpanIx = 0;
    this.curObjId = 0;
    this.curObjIdxPage = null;
  }

  reset() {
    this.curObjIndexSpanIx = 0;
    this.curObjDataSpanIx = 0;
    this.curObjId = 0;
    this.curObjIdxPage = null;
  }

  registerPage(page, isIndex) {
    if (!isIndex) {
      if (!this.curObjIdxPage) throw new Error("No current object index page");
      this.curObjIdxPage.registerPage(page.offset);
    }

    let luPage = this.luPages[this.luPageIx];
    try {
      if (!luPage) throw new SpiffsFullError();
      luPage.registerPage(page.objId, isIndex);
    } catch (err) {
      if (!(err instanceof SpiffsFullError)) throw err;
      this.luPageIx++;
      luPage = this.luPages[this.luPageIx];
      if (!luPage) {
        throw new Error("Invalid attempt to add page to a block when there is no more space in lookup");
      }
      luPage.registerPage(page.objId, isIndex);
    }

    this.pages.push(page);
  }

  beginObj(objId, size, name, objIndexSpanIx = 0, objDataSpanIx = 0) {
    if (this.remainingPages <= 0) throw new SpiffsFullError();

    this.reset();
    this.curObjId = objId;
    this.curObjIndexSpanIx = objIndexSpanIx;
    this.curObjDataSpanIx = objDataSpanIx;

    const page = new SpiffsObjIndexPage(objId, this.curObjIndexSpanIx, size, name, this.cfg);
    this.registerPage(page, true);
    this.curObjIdxPage = page;
    this.remainingPages--;
    this.curObjIndexSpanIx++;
  }

  updateObj(contents) {
    if (this.remainingPages <= 0) throw new SpiffsFullError();
    const page = new SpiffsObjDataPage(
      this.offset + this.pages.length * this.cfg.pageSize,
      this.curObjId,
      this.curObjDataSpanIx,
      contents,
      this.cfg
    );
    this.registerPage(page, false);
    this.curObjDataSpanIx++;
    this.remainingPages--;
  }

  endObj() {
    this.reset();
  }

  isFull() {
    return this.remainingPages <= 0;
  }

  toBinary(blocksLim) {
    const img = new Uint8Array(this.cfg.blockSize).fill(0xff);
    let offset = 0;
    for (let idx = 0; idx < this.pages.length; idx++) {
      const page = this.pages[idx];
      if (this.cfg.useMagic && idx === this.cfg.OBJ_LU_PAGES_PER_BLOCK - 1 && page instanceof SpiffsObjLuPage) {
        page.magicfy(blocksLim);
      }
      const bin = page.toBinary();
      img.set(bin, offset);
      offset += bin.length;
    }
    return img;
  }
}

// Lays a set of files out into a complete image of a fixed size.
export class SpiffsFS {
  constructor(imgSize, cfg = DEFAULT_SPIFFS_CONFIG) {
    if (imgSize % cfg.blockSize !== 0) {
      throw new Error("image size should be a multiple of block size");
    }
    this.imgSize = imgSize;
    this.cfg = cfg;
    this.blocks = [];
    this.blocksLim = Math.floor(imgSize / cfg.blockSize);
    this.remainingBlocks = this.blocksLim;
    this.curObjId = 1; // object ids start at 1; 0 and 0xFFFF are reserved
  }

  createBlock() {
    if (this.isFull()) throw new SpiffsFullError("the image size has been exceeded");
    const block = new SpiffsBlock(this.blocks.length, this.cfg);
    this.blocks.push(block);
    this.remainingBlocks--;
    return block;
  }

  isFull() {
    return this.remainingBlocks <= 0;
  }

  createFile(imgPath, contents) {
    if (imgPath.length > this.cfg.objNameLen) {
      throw new Error(`object name '${imgPath}' too long`);
    }

    try {
      this.blocks[this.blocks.length - 1].beginObj(this.curObjId, contents.length, imgPath);
    } catch {
      this.createBlock().beginObj(this.curObjId, contents.length, imgPath);
    }

    let offset = 0;
    while (offset < contents.length) {
      const chunkSize = Math.min(this.cfg.OBJ_DATA_PAGE_CONTENT_LEN, contents.length - offset);
      const chunk = contents.subarray(offset, offset + chunkSize);

      try {
        const block = this.blocks[this.blocks.length - 1];
        try {
          block.updateObj(chunk);
        } catch (err) {
          if (!(err instanceof SpiffsFullError)) throw err;
          if (block.isFull()) throw err;
          // Index table for this span is full but the block still has pages --
          // open a fresh index span and retry the same chunk.
          block.beginObj(this.curObjId, contents.length, imgPath, block.curObjIndexSpanIx, block.curObjDataSpanIx);
          continue;
        }
      } catch (err) {
        if (!(err instanceof SpiffsFullError)) throw err;
        // Block exhausted -- carry the in-progress object state into a new one.
        const prev = this.blocks[this.blocks.length - 1];
        const block = this.createBlock();
        block.curObjId = prev.curObjId;
        block.curObjIdxPage = prev.curObjIdxPage;
        block.curObjDataSpanIx = prev.curObjDataSpanIx;
        block.curObjIndexSpanIx = prev.curObjIndexSpanIx;
        continue;
      }

      offset += chunkSize;
    }

    this.blocks[this.blocks.length - 1].endObj();
    this.curObjId++;
  }

  toBinary() {
    const img = new Uint8Array(this.imgSize).fill(0xff);
    let offset = 0;

    for (const block of this.blocks) {
      img.set(block.toBinary(this.blocksLim), offset);
      offset += this.cfg.blockSize;
    }

    // Unused blocks still need their lookup-page magic, or SPIFFS treats the
    // image as unformatted and reformats on first mount -- losing everything.
    if (this.cfg.useMagic) {
      for (let bix = this.blocks.length; bix < this.blocksLim; bix++) {
        img.set(new SpiffsBlock(bix, this.cfg).toBinary(this.blocksLim), offset);
        offset += this.cfg.blockSize;
      }
    }

    return img;
  }
}

// ---- Reader ----------------------------------------------------------

// Recovers each file's name, size and content. Read-only and offline.
export class SpiffsReader {
  constructor(imageData, cfg = DEFAULT_SPIFFS_CONFIG) {
    this.imageData = imageData;
    this.cfg = cfg;
    this.filesMap = new Map();
  }

  parse() {
    const blocks = Math.floor(this.imageData.length / this.cfg.blockSize);
    for (let bix = 0; bix < blocks; bix++) {
      const start = bix * this.cfg.blockSize;
      this.parseBlock(this.imageData.subarray(start, start + this.cfg.blockSize));
    }
    return this;
  }

  parseBlock(blockData) {
    // Later slots are padding and the block magic, not object ids
    const luView = new DataView(blockData.buffer, blockData.byteOffset, this.cfg.pageSize);
    for (let i = 0; i < this.cfg.OBJ_USABLE_PAGES_PER_BLOCK; i++) {
      const objId = luView.getUint16(i * this.cfg.objIdLen, true);
      if (objId === EMPTY_OBJ_ID) continue;
      if ((objId & OBJ_ID_IX_FLAG) === 0) continue;
      const realObjId = objId & ~OBJ_ID_IX_FLAG;
      if (!this.filesMap.has(realObjId)) {
        this.filesMap.set(realObjId, { name: null, size: 0, dataPages: [] });
      }
    }

    for (let pageIdx = this.cfg.OBJ_LU_PAGES_PER_BLOCK; pageIdx < this.cfg.PAGES_PER_BLOCK; pageIdx++) {
      const start = pageIdx * this.cfg.pageSize;
      this.parsePage(blockData.subarray(start, start + this.cfg.pageSize));
    }
  }

  parsePage(pageData) {
    const headerSize = this.cfg.OBJ_DATA_PAGE_HEADER_LEN;
    if (pageData.length < headerSize) return;

    const view = new DataView(pageData.buffer, pageData.byteOffset, pageData.length);
    const objId = view.getUint16(0, true);
    if (objId === EMPTY_OBJ_ID) return;

    const spanIx = view.getUint16(this.cfg.objIdLen, true);
    const flags = view.getUint8(this.cfg.objIdLen + this.cfg.spanIxLen);
    const isIndex = (objId & OBJ_ID_IX_FLAG) !== 0;
    const realObjId = objId & ~OBJ_ID_IX_FLAG;

    if (isIndex && flags === SPIFFS_PH_FLAG_USED_FINAL_INDEX) {
      if (!this.filesMap.has(realObjId)) {
        this.filesMap.set(realObjId, { name: null, size: 0, dataPages: [] });
      }
      // Only span 0 carries the name and size; later spans are pure page tables.
      if (spanIx === 0) this.parseIndexPage(pageData, view, realObjId);
    } else if (!isIndex && flags === SPIFFS_PH_FLAG_USED_FINAL) {
      const file = this.filesMap.get(realObjId);
      if (file) {
        const content = pageData.subarray(headerSize, headerSize + this.cfg.OBJ_DATA_PAGE_CONTENT_LEN);
        file.dataPages.push([spanIx, content]);
      }
    }
  }

  parseIndexPage(pageData, view, objId) {
    let offset = this.cfg.OBJ_DATA_PAGE_HEADER_LEN + this.cfg.OBJ_DATA_PAGE_HEADER_LEN_ALIGNED_PAD;
    if (offset + SPIFFS_PH_IX_SIZE_LEN + SPIFFS_PH_IX_OBJ_TYPE_LEN > pageData.length) return;

    const size = view.getUint32(offset, true);
    offset += SPIFFS_PH_IX_SIZE_LEN + SPIFFS_PH_IX_OBJ_TYPE_LEN;

    const nameEnd = offset + this.cfg.objNameLen;
    if (nameEnd > pageData.length) return;

    const nameBytes = pageData.subarray(offset, nameEnd);
    const nul = nameBytes.indexOf(0);
    const file = this.filesMap.get(objId);
    file.name = new TextDecoder().decode(nul !== -1 ? nameBytes.subarray(0, nul) : nameBytes);
    file.size = size;
  }

  // Files whose name never resolved are skipped rather than half-formed
  listFiles() {
    const files = [];
    for (const file of this.filesMap.values()) {
      if (file.name === null) continue;

      file.dataPages.sort((a, b) => a[0] - b[0]);

      const data = new Uint8Array(file.size);
      let written = 0;
      for (const [, content] of file.dataPages) {
        if (written >= file.size) break;
        const take = Math.min(content.length, file.size - written);
        data.set(content.subarray(0, take), written);
        written += take;
      }

      files.push({ name: file.name, size: file.size, data: data.subarray(0, written) });
    }
    return files;
  }
}

// ---- Helpers ---------------------------------------------------------

// Convenience wrapper: raw image bytes in, file list out.
export function readSpiffsFiles(imageData, cfg = DEFAULT_SPIFFS_CONFIG) {
  return new SpiffsReader(imageData, cfg).parse().listFiles();
}

// Convenience wrapper: file list in, complete image of `imgSize` bytes out.
export function buildSpiffsImage(files, imgSize, cfg = DEFAULT_SPIFFS_CONFIG) {
  const fs = new SpiffsFS(imgSize, cfg);
  for (const file of files) {
    fs.createFile(file.name, file.data);
  }
  return fs.toBinary();
}

// Highest block SPIFFS has allocated, so a restore can fit a smaller partition
// than the backup declared. SPIFFS only -- LittleFS has a different format.
export function spiffsUsedSize(data, cfg = DEFAULT_SPIFFS_CONFIG) {
  const blockCount = Math.floor(data.length / cfg.blockSize);
  for (let bix = blockCount - 1; bix >= 0; bix--) {
    const start = bix * cfg.blockSize;
    const view = new DataView(data.buffer, data.byteOffset + start, cfg.pageSize);
    for (let i = 0; i < cfg.OBJ_USABLE_PAGES_PER_BLOCK; i++) {
      if (view.getUint16(i * cfg.objIdLen, true) !== EMPTY_OBJ_ID) {
        return (bix + 1) * cfg.blockSize;
      }
    }
  }
  return 0;
}
