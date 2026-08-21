// ESP-IDF partition table — the on-flash format only.
//
// Pure decoding: no device, no I/O, no policy. The format is Espressif's and
// changes on their schedule, not ours, which is why it is not folded into the
// ESP32 device module. §10.5 reads it as evidence and §10.6 takes the restore
// geometry from it.
//
// Format constants live here rather than in the §7 register: §7 exists to stop a
// reimplementer normalising values that look arbitrary, and a struct layout read
// straight from the entries it parses is self-evident. Timing and retry values
// around the *read* are a different matter and stay in `constants.js`.

/** `protocol`. Espressif fixes the table at this offset on every ESP32 part. */
export const PARTITION_TABLE_OFFSET = 0x8000;

/** `protocol`. One flash sector; the table may not exceed it. */
export const PARTITION_TABLE_MAX_BYTES = 0x1000;

// `protocol`. 32-byte little-endian entries:
// magic(2) type(1) subtype(1) offset(4) size(4) label(16) flags(4).
const ENTRY_BYTES = 32;
const ENTRY_MAGIC = 0x50aa;
const LABEL_OFFSET = 12;
const LABEL_BYTES = 16;

const DATA_TYPE = 0x01;
const SUBTYPE_SPIFFS = 0x82;
const SUBTYPE_LITTLEFS = 0x83;

/**
 * @typedef {object} Partition
 * @property {number} type
 * @property {number} subtype
 * @property {number} offset
 * @property {number} size
 * @property {string} label
 */

/**
 * Decode entries until the first one without the magic.
 *
 * A table that is absent or unreadable decodes to an empty list rather than
 * raising: "no partitions" is a legitimate answer for a blank chip, and §10.5
 * distinguishes that from a *failed read*, which its caller must catch at the
 * read itself. Trailing bytes are the MD5 entry and padding, neither of which
 * carries the magic.
 *
 * @param {Uint8Array} bytes
 * @returns {Partition[]}
 */
export function parsePartitionTable(bytes) {
  const partitions = [];
  const decoder = new TextDecoder();

  for (let start = 0; start + ENTRY_BYTES <= bytes.length; start += ENTRY_BYTES) {
    const entry = new DataView(bytes.buffer, bytes.byteOffset + start, ENTRY_BYTES);
    if (entry.getUint16(0, true) !== ENTRY_MAGIC) break;

    const label = decoder
      .decode(bytes.subarray(start + LABEL_OFFSET, start + LABEL_OFFSET + LABEL_BYTES))
      .replace(/\0.*$/s, '');

    partitions.push({
      type: entry.getUint8(2),
      subtype: entry.getUint8(3),
      offset: entry.getUint32(4, true),
      size: entry.getUint32(8, true),
      label,
    });
  }

  return partitions;
}

/**
 * The first SPIFFS or LittleFS data partition, or undefined.
 *
 * Absent means a blank chip as far as §10.5 is concerned — there is no filesystem
 * to read, so there is nothing to preserve. Matched on type and subtype rather
 * than label: the label is a build-time string and boards do not agree on it.
 */
export function findFilesystemPartition(partitions) {
  return partitions.find(
    (partition) =>
      partition.type === DATA_TYPE &&
      (partition.subtype === SUBTYPE_SPIFFS || partition.subtype === SUBTYPE_LITTLEFS)
  );
}

/** Whether a filesystem partition is SPIFFS, which alone can be rebuilt at a new size (§10.6). */
export function isSpiffsPartition(partition) {
  return partition?.subtype === SUBTYPE_SPIFFS;
}

/**
 * §10.5 input 4: does the device's layout match the one about to be written?
 *
 * Entry by entry, never raw bytes — the table image carries an MD5 entry and
 * padding that differ between builds without the layout differing, so a byte
 * comparison reports a mismatch on every flash and forces a full-layout write
 * that was never needed.
 */
export function partitionTablesMatch(a, b) {
  if (a.length !== b.length) return false;
  return a.every(
    (partition, index) =>
      partition.type === b[index].type &&
      partition.subtype === b[index].subtype &&
      partition.offset === b[index].offset &&
      partition.size === b[index].size &&
      partition.label === b[index].label
  );
}
