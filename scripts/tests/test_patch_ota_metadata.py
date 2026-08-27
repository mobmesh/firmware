#!/usr/bin/env python3
"""Unit tests for .github/scripts/patch_ota_metadata.py.

Run: python3 scripts/tests/test_patch_ota_metadata.py

The strongest case here is test_verify_accepts_vendored_images: the parser reproduces
the checksum and digest of four real shipped builds, so the segment walk, the checksum
seed and the alignment rule are checked against ground truth rather than against a
fixture this file made up.
"""
import hashlib
import importlib.util
import struct
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent.parent
SCRIPT_PATH = REPO / ".github" / "scripts" / "patch_ota_metadata.py"
spec = importlib.util.spec_from_file_location("patch_ota_metadata", SCRIPT_PATH)
pom = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pom)

VENDORED = sorted((REPO / "pages" / "flasher").glob("*/*/firmware.bin"))
REGISTRY = pom.load_mod_registry()
MARKER = REGISTRY["hotspot-ota"][1]


def unpatched(data):
    """A vendored image with its metadata block zeroed, as it was before CI patched it.

    patch() refuses a non-zero reserved area, so a round-trip test needs the pristine
    bytes. Zeroing the block alone leaves the checksum and digest stale, so both are
    restored the same way the patcher computes them.
    """
    out = bytearray(data)
    region = slice(pom.RESERVED_OFFSET, pom.RESERVED_OFFSET + pom.RESERVED_LEN)
    delta = 0
    for byte in out[region]:
        delta ^= byte
    checksum_offset = len(out) - pom.DIGEST_LEN - 1
    out[region] = bytes(pom.RESERVED_LEN)
    out[checksum_offset] ^= delta
    out[-pom.DIGEST_LEN:] = hashlib.sha256(bytes(out[:-pom.DIGEST_LEN])).digest()
    return bytes(out)


def make_image(segment_len=512, hash_appended=1, segment_count=1, marker=True):
    """A minimal but structurally real ESP32 image, big enough to hold the reserved area.

    The checksum position comes from the parser under test rather than being restated
    here -- a fixture that reimplements the alignment rule tests the fixture, not the code.
    """
    header = bytearray(pom.HEADER_LEN)
    header[0] = pom.IMAGE_MAGIC
    header[1] = segment_count
    header[pom.HASH_APPENDED_OFFSET] = hash_appended

    body = bytearray()
    for index in range(segment_count):
        body += struct.pack("<II", 0x40080000 + index, segment_len)
        segment = bytearray(segment_len)
        if marker and index == 0:
            segment[400:400 + len(MARKER)] = MARKER
        body += segment

    data = bytearray(header + body)
    checksum_offset, checksum = pom.parse_image(bytes(data) + bytes(64))
    data += bytes(checksum_offset - len(data) + 1)
    data[checksum_offset] = checksum
    if hash_appended:
        data += hashlib.sha256(bytes(data)).digest()
    return bytes(data)


class ParserGroundTruth(unittest.TestCase):
    def test_verify_accepts_vendored_images(self):
        self.assertTrue(VENDORED, "no vendored firmware.bin found to test against")
        for image in VENDORED:
            with self.subTest(image=str(image.relative_to(REPO))):
                pom.verify(image.read_bytes(), str(image))

    def test_vendored_images_carry_their_metadata(self):
        # These are CI output, patched at build time. A missing block means the release
        # step did not run, not that the parser is wrong.
        for image in VENDORED:
            with self.subTest(image=str(image.relative_to(REPO))):
                meta = pom.read_metadata(image.read_bytes())
                self.assertIsNotNone(meta, "vendored image carries no metadata block")
                self.assertEqual(meta["layout_version"], pom.LAYOUT_VERSION)
                board, _, role = meta["board_role"].partition("/")
                self.assertEqual((board, role), (image.parent.parent.name, image.parent.name))
                self.assertTrue(meta["mods"] & (1 << REGISTRY["hotspot-ota"][0]))


class Payload(unittest.TestCase):
    def test_layout_is_eighty_bytes(self):
        payload = pom.build_payload("v1.17.1", "ceb8915", "heltec_v4", "repeater")
        self.assertEqual(len(payload), pom.RESERVED_LEN)
        self.assertEqual(payload[:8], pom.MAGIC)
        self.assertEqual(payload[8], pom.LAYOUT_VERSION)

    def test_longest_real_board_role_fits(self):
        pom.build_payload("v1.17.1", "ceb8915", "heltec_v4", "room_server")

    def test_oversized_field_is_refused(self):
        with self.assertRaises(pom.ImageError):
            pom.build_payload("v1.17.1", "ceb8915", "a" * 30, "repeater")

    def test_reserved_bytes_stay_zero(self):
        payload = pom.build_payload("v1.17.1", "ceb8915", "heltec_v4", "repeater")
        self.assertEqual(payload[10:12], b"\0\0")
        self.assertEqual(payload[68:80], bytes(12))


class ModBits(unittest.TestCase):
    def test_registry_bits_are_unique_and_in_range(self):
        seen = {}
        for name, (bit, _marker) in REGISTRY.items():
            self.assertTrue(0 <= bit < 32, f"{name}: bit {bit} out of range")
            self.assertNotIn(bit, seen, f"{name} and {seen.get(bit)} share bit {bit}")
            seen[bit] = name

    def test_bit_set_only_when_the_marker_is_in_the_image(self):
        present = pom.resolve_mod_bits(make_image(), ["hotspot-ota"], REGISTRY)
        self.assertEqual(present, 1 << REGISTRY["hotspot-ota"][0])

    def test_claimed_mod_with_no_marker_leaves_its_bit_clear(self):
        # The drift case: build config lists the mod, the binary does not carry it.
        self.assertEqual(pom.resolve_mod_bits(make_image(marker=False), ["hotspot-ota"], REGISTRY), 0)

    def test_undetectable_mod_is_silently_bitless(self):
        self.assertEqual(pom.resolve_mod_bits(make_image(), ["timing-safety"], REGISTRY), 0)

    def test_image_without_ota_is_still_stamped(self):
        # A build with no OTA mod is identified, with the bit clear -- it is not refused.
        payload = pom.build_payload("v1.17.1", "ceb8915", "heltec_v4", "repeater", mods=0)
        out = pom.patch(make_image(marker=False), payload)
        self.assertEqual(pom.read_metadata(out)["mods"], 0)

    def test_vendored_images_carry_the_marker(self):
        for image in VENDORED:
            with self.subTest(image=str(image.relative_to(REPO))):
                self.assertIn(MARKER, image.read_bytes())


class Patching(unittest.TestCase):
    def setUp(self):
        self.payload = pom.build_payload(
            "v1.17.1", "ceb8915", "heltec_v4", "repeater", mods=1 << REGISTRY["hotspot-ota"][0]
        )

    def test_round_trip_on_vendored_images(self):
        for image in VENDORED:
            with self.subTest(image=str(image.relative_to(REPO))):
                out = pom.patch(unpatched(image.read_bytes()), self.payload)
                meta = pom.read_metadata(out)
                self.assertEqual(meta["upstream_version"], "v1.17.1")
                self.assertEqual(meta["repo_sha"], "ceb8915")
                self.assertEqual(meta["board_role"], "heltec_v4/repeater")
                self.assertEqual(meta["layout_version"], pom.LAYOUT_VERSION)
                self.assertEqual(meta["mods"], 1 << REGISTRY["hotspot-ota"][0])

    def test_only_expected_regions_change(self):
        original = make_image()
        out = pom.patch(original, self.payload)
        self.assertEqual(len(out), len(original))
        allowed = set(range(pom.RESERVED_OFFSET, pom.RESERVED_OFFSET + pom.RESERVED_LEN))
        allowed.add(len(original) - pom.DIGEST_LEN - 1)
        allowed |= set(range(len(original) - pom.DIGEST_LEN, len(original)))
        changed = {i for i in range(len(original)) if original[i] != out[i]}
        self.assertTrue(changed - allowed == set(), f"unexpected bytes changed: {sorted(changed - allowed)[:8]}")

    def test_xor_shortcut_matches_full_recompute(self):
        # patch() calls verify() on its own output, which re-walks every segment. If the
        # delta shortcut were wrong the checksum would disagree and this would raise.
        pom.verify(pom.patch(make_image(), self.payload), "output")

    def test_double_patch_is_refused(self):
        once = pom.patch(make_image(), self.payload)
        with self.assertRaises(pom.ImageError):
            pom.patch(once, self.payload)

    def test_non_image_is_refused(self):
        with self.assertRaises(pom.ImageError):
            pom.patch(b"\x00" * 4096, self.payload)

    def test_image_without_appended_hash_is_refused(self):
        with self.assertRaises(pom.ImageError):
            pom.patch(make_image(hash_appended=0), self.payload)

    def test_truncated_image_is_refused(self):
        with self.assertRaises(pom.ImageError):
            pom.patch(make_image()[:-64], self.payload)

    def test_multi_segment_image(self):
        out = pom.patch(make_image(segment_count=3), self.payload)
        self.assertEqual(pom.read_metadata(out)["board_role"], "heltec_v4/repeater")


if __name__ == "__main__":
    unittest.main(verbosity=2)
