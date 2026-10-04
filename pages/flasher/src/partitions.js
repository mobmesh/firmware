// ESP-IDF partition table, decoding only: no device, no I/O, no policy. Espressif owns
// the format, which is why it is not folded into the ESP32 module.

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

const APP_TYPE = 0x00;
const SUBTYPE_OTA_1 = 0x11;
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

// Decodes to the first entry without the magic. An empty list is a legitimate answer for
// a blank chip; a failed *read* is the caller's to catch.
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

// Matched on type and subtype, not label: the label is a build-time string boards disagree on.
export function findFilesystemPartition(partitions) {
  return partitions.find(
    (partition) =>
      partition.type === DATA_TYPE &&
      (partition.subtype === SUBTYPE_SPIFFS || partition.subtype === SUBTYPE_LITTLEFS)
  );
}

// The second OTA app slot. Blanking it is what stops otadata booting the image that was
// just replaced; matched on subtype because labels vary between builds.
export function findSecondAppSlot(partitions) {
  return partitions.find((p) => p.type === APP_TYPE && p.subtype === SUBTYPE_OTA_1);
}

/** Whether a filesystem partition is SPIFFS, which alone can be rebuilt at a new size. */
export function isSpiffsPartition(partition) {
  return partition?.subtype === SUBTYPE_SPIFFS;
}

// Entry by entry, never raw bytes — the image carries an MD5 entry and padding
// that differ between builds without the layout differing.
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
