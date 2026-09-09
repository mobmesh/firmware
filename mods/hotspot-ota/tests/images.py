"""Synthetic firmware images shaped the way the OTA page parses them.

Offsets follow the page's own reader: the esp32 magic at byte 32, chip id at 12, and the
MOBMESH trailer at 208 carrying version, sha, role and a flags word.
"""
import struct

HDR = 208
MAGIC = 0xABCD5432


def image(chip=9, version="v1.17.1-ccccccc", sha="ccccccc", role="repeater",
          flags=2, size=4096, magic=MAGIC, marker=b"MOBMESH"):
    b = bytearray(b"\0" * max(size, HDR + 80))
    b[0] = 0xE9
    b[12] = chip
    struct.pack_into("<I", b, 32, magic)
    b[HDR:HDR + 7] = marker
    b[HDR + 7] = 0
    b[HDR + 8] = 1
    b[HDR + 12:HDR + 12 + len(version)] = version.encode()
    b[HDR + 28:HDR + 28 + len(sha)] = sha.encode()
    b[HDR + 40:HDR + 40 + len(role)] = role.encode()
    struct.pack_into("<I", b, HDR + 64, flags)
    return bytes(b)


def good(**kw):
    return image(**kw)


def full_flash(**kw):
    # Wrong magic at 32 is how the page recognises a merged full-flash image.
    return image(magic=0x12345678, **kw)


def wrong_chip(**kw):
    return image(chip=5, **kw)          # ESP32-C3 image offered to an S3 node


def wrong_role(**kw):
    return image(role="room_server", **kw)


def no_hotspot_ota(**kw):
    return image(marker=b"XXXXXXX", **kw)


def tiny():
    return b"\xe9\x00\x01"              # too short for the magic read
