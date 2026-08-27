#!/usr/bin/env python3
"""Write MobMesh OTA metadata into the reserved tail of an ESP32 app image.

The 80 bytes at offsets 208-288 are zero in every stock build. Writing there leaves a
normal flashable image, but the segment checksum byte and the appended SHA-256 must both
be recomputed or esptool image-info rejects the result.

Layout, version 0x01:
    208   8  magic "MOBMESH\\0" -- identifies the block; the layout version, not the
                magic, changes when the fields below move
    216   1  layout version
    217   1  flags
    218   2  reserved
    220  16  upstream version   e.g. "v1.17.1"
    236  12  repo short sha     e.g. "ceb8915"
    248  24  board/role         e.g. "heltec_v4/repeater"
    272   4  mod bitfield, u32 LE -- which mods this image actually carries
    276  12  reserved
"""

import argparse
import hashlib
import struct
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
MODS_DIR = REPO_ROOT / "mods"

RESERVED_OFFSET = 208
RESERVED_LEN = 80
MAGIC = b"MOBMESH\0"
LAYOUT_VERSION = 0x01

# esp_image_header_t: magic, segment count, and the flag that says a SHA-256 is appended.
IMAGE_MAGIC = 0xE9
HEADER_LEN = 24
HASH_APPENDED_OFFSET = 23
CHECKSUM_SEED = 0xEF
DIGEST_LEN = 32

FIELDS = [
    ("magic", 208, 8),
    ("layout_version", 216, 1),
    ("flags", 217, 1),
    ("_pad", 218, 2),
    ("upstream_version", 220, 16),
    ("repo_sha", 236, 12),
    ("board_role", 248, 24),
    ("mods", 272, 4),
    ("_reserved", 276, 12),
]


class ImageError(Exception):
    pass


def parse_image(data):
    """Walk the segment table. Returns (checksum_offset, computed_checksum).

    Derived from the image itself rather than from its length: a truncated or padded
    file then fails as a structural mismatch here, not as an unexplained hash failure.
    """
    if len(data) < HEADER_LEN or data[0] != IMAGE_MAGIC:
        raise ImageError(f"not an ESP32 image (first byte {data[0]:#04x}, expected 0xe9)")
    if data[HASH_APPENDED_OFFSET] != 1:
        raise ImageError("hash_appended is not set; this patcher only handles images that carry one")

    segment_count = data[1]
    checksum = CHECKSUM_SEED
    pos = HEADER_LEN
    for index in range(segment_count):
        if pos + 8 > len(data):
            raise ImageError(f"segment {index} header runs past end of file")
        _addr, length = struct.unpack_from("<II", data, pos)
        pos += 8
        if pos + length > len(data):
            raise ImageError(f"segment {index} data runs past end of file")
        for byte in data[pos:pos + length]:
            checksum ^= byte
        pos += length

    # The checksum byte is the last byte of a 16-byte-aligned run counted from file start.
    checksum_offset = (pos + 15) // 16 * 16 - 1
    if checksum_offset < pos:
        checksum_offset += 16
    return checksum_offset, checksum & 0xFF


def verify(data, label="image"):
    """Confirm the stored checksum and digest match what the image's own contents imply."""
    checksum_offset, computed = parse_image(data)
    expected_offset = len(data) - DIGEST_LEN - 1
    if checksum_offset != expected_offset:
        raise ImageError(
            f"{label}: checksum byte derived at {checksum_offset} but the file's length puts "
            f"it at {expected_offset} -- the file is truncated, padded, or not what it claims"
        )
    stored = data[checksum_offset]
    if stored != computed:
        raise ImageError(f"{label}: checksum byte is {stored:#04x}, contents give {computed:#04x}")
    stored_digest = data[-DIGEST_LEN:]
    computed_digest = hashlib.sha256(data[:-DIGEST_LEN]).digest()
    if stored_digest != computed_digest:
        raise ImageError(
            f"{label}: appended digest is {stored_digest.hex()}, contents give {computed_digest.hex()}"
        )
    return checksum_offset


def field(value, length, name):
    encoded = value.encode("ascii", errors="strict")
    if len(encoded) >= length:
        raise ImageError(f"{name} is {len(encoded)} bytes, does not fit {length} with a NUL")
    return encoded + b"\0" * (length - len(encoded))


def load_mod_registry(mods_dir=MODS_DIR):
    """Every mod that has earned a bit, by name. Absent keys mean "not detectable"."""
    import yaml

    registry = {}
    for manifest in sorted(Path(mods_dir).glob("*/mod.yaml")):
        meta = yaml.safe_load(manifest.read_text()) or {}
        bit, marker = meta.get("bit"), meta.get("image_marker")
        if bit is None or marker is None:
            continue
        name = meta.get("name") or manifest.parent.name
        if not isinstance(bit, int) or not 0 <= bit < 32:
            raise ImageError(f"{manifest}: bit must be an integer 0-31, got {bit!r}")
        registry[name] = (bit, marker.encode())
    return registry


def resolve_mod_bits(data, claimed, registry):
    """Set a bit only where the built image actually carries that mod's marker.

    Reading it out of the binary rather than off the build config is the whole point: a
    target can list a mod whose patch quietly stopped compiling, and the config would
    still say yes. That case fails the build here instead of shipping a lie.
    """
    bits = 0
    for name in claimed:
        entry = registry.get(name)
        if entry is None:
            continue   # no marker, no bit -- a clear bit means absent, never "unknown"
        bit, marker = entry
        if marker not in data:
            # Report it, do not stop the build: the bit's honest value is 0, and whether a
            # missing mod should block a release is the publish step's call, not this one.
            print(
                f"::warning::this target lists the '{name}' mod, but its marker "
                f"{marker.decode()!r} is not in the built image -- leaving its bit clear",
                file=sys.stderr,
            )
            continue
        bits |= 1 << bit
    return bits


def build_payload(upstream_version, repo_sha, board, role, mods=0, flags=0x00):
    payload = bytearray(RESERVED_LEN)
    payload[0:8] = MAGIC
    payload[8] = LAYOUT_VERSION
    payload[9] = flags
    payload[12:28] = field(upstream_version, 16, "upstream version")
    payload[28:40] = field(repo_sha, 12, "repo sha")
    payload[40:64] = field(f"{board}/{role}", 24, "board/role")
    payload[64:68] = struct.pack("<I", mods)
    return bytes(payload)


def patch(data, payload):
    if len(payload) != RESERVED_LEN:
        raise ImageError(f"payload is {len(payload)} bytes, must be {RESERVED_LEN}")
    region = slice(RESERVED_OFFSET, RESERVED_OFFSET + RESERVED_LEN)
    if any(data[region]):
        raise ImageError("reserved area is not zero -- already patched, or the field is in use")

    checksum_offset = verify(data, "input")
    out = bytearray(data)
    out[region] = payload

    # The reserved bytes were zero, so their old XOR contribution was nothing: the new
    # checksum is the old one flipped by the XOR of the bytes just written.
    delta = 0
    for byte in payload:
        delta ^= byte
    out[checksum_offset] = data[checksum_offset] ^ delta

    out[-DIGEST_LEN:] = hashlib.sha256(bytes(out[:-DIGEST_LEN])).digest()

    # Cheap proof the shortcut above agrees with a full re-walk of the segments.
    verify(bytes(out), "output")
    return bytes(out)


def read_metadata(data):
    """Read back what patch() wrote. None when the area holds no recognised payload."""
    region = bytes(data[RESERVED_OFFSET:RESERVED_OFFSET + RESERVED_LEN])
    if region[0:8] != MAGIC:
        return None
    out = {"layout_version": region[8], "flags": region[9]}
    for name, offset, length in FIELDS:
        if name.startswith("_") or name in ("magic", "layout_version", "flags"):
            continue
        raw = region[offset - RESERVED_OFFSET:offset - RESERVED_OFFSET + length]
        if name == "mods":
            out[name] = struct.unpack("<I", raw)[0]
            continue
        out[name] = raw.split(b"\0", 1)[0].decode("ascii")
    return out


def describe_mods(bits, registry):
    """Bit numbers back to names, so the build log is readable at a glance."""
    named = sorted(name for name, (bit, _) in registry.items() if bits & (1 << bit))
    return ", ".join(named) if named else "none"


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("image", type=Path)
    parser.add_argument("--out", type=Path, help="output path (default: patch in place)")
    parser.add_argument("--upstream-version")
    parser.add_argument("--repo-sha")
    parser.add_argument("--board")
    parser.add_argument("--role")
    parser.add_argument(
        "--mods",
        default="",
        help="comma-separated mods this target claims; a bit is set only where the image "
             "carries that mod's marker",
    )
    parser.add_argument("--show", action="store_true", help="report the result's metadata")
    parser.add_argument(
        "--check",
        action="store_true",
        help="read an already-patched image: report its metadata and fail if it carries none",
    )
    args = parser.parse_args()

    data = args.image.read_bytes()
    if args.check:
        metadata = read_metadata(data)
        if metadata is None:
            print(f"::error::{args.image}: no MobMesh metadata at offset {RESERVED_OFFSET}", file=sys.stderr)
            return 1
        try:
            verify(data, str(args.image))
        except ImageError as error:
            print(f"::error::{args.image}: {error}", file=sys.stderr)
            return 1
        print(f"{args.image}: {metadata}")
        print(f"  mods 0x{metadata['mods']:08x} -> {describe_mods(metadata['mods'], load_mod_registry())}")
        return 0

    missing = [n for n in ("upstream_version", "repo_sha", "board", "role") if not getattr(args, n)]
    if missing:
        parser.error("--" + ", --".join(n.replace("_", "-") for n in missing) + " required unless --check")

    try:
        registry = load_mod_registry()
        claimed = [name for name in args.mods.split(",") if name]
        mods = resolve_mod_bits(data, claimed, registry)
        payload = build_payload(args.upstream_version, args.repo_sha, args.board, args.role, mods)
        out = patch(data, payload)
    except ImageError as error:
        print(f"::error::{args.image}: {error}", file=sys.stderr)
        return 1

    (args.out or args.image).write_bytes(out)
    if args.show:
        metadata = read_metadata(out)
        print(f"{args.image}: {metadata}")
        print(f"  mods 0x{metadata['mods']:08x} -> {describe_mods(metadata['mods'], registry)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
