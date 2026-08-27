"""Header parsing and output verification for scripts/merge_flash_image.py.

esptool itself is not exercised here -- these cover the parts that decide what esptool is
told and whether its output is acceptable, which is where a wrong answer ships a file that
will not flash.
"""

import contextlib
import io
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import merge_flash_image as mfi


def image(chip_id=5, size_code=2, body=b"\x00" * 64):
    """A minimal esp_image_header_t: magic, then flash size in byte 3's high nibble and
    chip id at bytes 12-13."""
    head = bytearray(16)
    head[0] = 0xE9
    head[3] = (size_code << 4) | 0x0F
    head[12:14] = chip_id.to_bytes(2, "little")
    return bytes(head) + body


class ReadImageHeaderTestCase(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.dir = Path(self.tmp.name)

    def _write(self, data, name="img.bin"):
        p = self.dir / name
        p.write_bytes(data)
        return p

    def test_reads_chip_and_flash_size(self):
        self.assertEqual(mfi.read_image_header(self._write(image(5, 2))), ("esp32c3", "4MB"))
        self.assertEqual(mfi.read_image_header(self._write(image(9, 4))), ("esp32s3", "16MB"))
        self.assertEqual(mfi.read_image_header(self._write(image(0, 3))), ("esp32", "8MB"))

    def test_rejects_a_file_that_is_not_an_esp32_image(self):
        with self.assertRaises(SystemExit):
            mfi.read_image_header(self._write(b"\x7fELF" + b"\x00" * 60))

    def test_rejects_a_truncated_header(self):
        with self.assertRaises(SystemExit):
            mfi.read_image_header(self._write(b"\xe9\x01\x02"))

    def test_rejects_an_unknown_chip_id(self):
        with self.assertRaises(SystemExit):
            mfi.read_image_header(self._write(image(chip_id=0x7FF)))

    def test_rejects_an_unknown_flash_size_code(self):
        with self.assertRaises(SystemExit):
            mfi.read_image_header(self._write(image(size_code=9)))


class VerifyTestCase(unittest.TestCase):
    """verify() is the gate between esptool's output and a published asset."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.dir = Path(self.tmp.name)
        self.boot = self.dir / "bootloader.bin"
        self.boot.write_bytes(image(5, 2, b"\xaa" * 48))
        self.part = self.dir / "partitions.bin"
        self.part.write_bytes(b"\xaaP" + b"\xbb" * 62)
        self.app = self.dir / "app.bin"
        self.app.write_bytes(image(5, 2, b"\xcc" * 256))
        self.parts = [(0x0, self.boot), (0x8000, self.part), (0x10000, self.app)]

    def assertRejects(self, merged):
        """verify() prints its reasons before exiting; swallow them so a passing run stays
        quiet -- repo-checks quotes test output into its failure issue."""
        with self.assertRaises(SystemExit), contextlib.redirect_stdout(io.StringIO()):
            mfi.verify(merged, self.parts, "4MB")

    def _merged(self, **kw):
        """Assemble what a correct esptool run would produce, so each test can spoil one
        property of it."""
        end = max(off + len(p.read_bytes()) for off, p in self.parts)
        img = bytearray(b"\xff" * end)
        for off, p in self.parts:
            img[off:off + len(p.read_bytes())] = p.read_bytes()
        if kw.get("pad"):
            img += b"\xff" * kw["pad"]
        if kw.get("truncate"):
            img = img[:-kw["truncate"]]
        if kw.get("wrong_size_byte"):
            img[3] = (kw["wrong_size_byte"] << 4) | 0x0F
        if kw.get("corrupt_at") is not None:
            img[kw["corrupt_at"]] ^= 0xFF
        out = self.dir / "merged.bin"
        out.write_bytes(bytes(img))
        return out

    def test_accepts_a_correctly_merged_image(self):
        mfi.verify(self._merged(), self.parts, "4MB")

    def test_rejects_padding_past_the_last_part(self):
        self.assertRejects(self._merged(pad=1024))

    def test_rejects_truncation(self):
        self.assertRejects(self._merged(truncate=16))

    def test_rejects_a_header_not_patched_to_the_expected_flash_size(self):
        self.assertRejects(self._merged(wrong_size_byte=4))

    def test_rejects_a_part_whose_body_was_altered(self):
        self.assertRejects(self._merged(corrupt_at=0x10040))

    def test_rejects_an_altered_non_image_part(self):
        self.assertRejects(self._merged(corrupt_at=0x8010))

    def test_rejects_an_app_header_rewritten_by_esptool(self):
        """Only the bootloader's flash-size byte may change; touching the app's would
        invalidate its appended SHA-256."""
        self.assertRejects(self._merged(corrupt_at=0x10003))


if __name__ == "__main__":
    unittest.main()
