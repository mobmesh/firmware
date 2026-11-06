"""Compile the vendored miniz tinfl that nRF52 builds of sync-settings use, and decode raw DEFLATE made by zlib.

ESP32 decompresses from mask ROM; nRF52 has no ROM copy, so these cases check the vendored decoder against
the same raw-DEFLATE format the ESP32 publisher sends (zlib with no header, as tdefl writes it).
"""

import shutil
import subprocess
import tempfile
import unittest
import zlib
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent.parent
MOD = REPO_ROOT / "mods" / "sync-settings"
SRC = MOD / "files" / "src"


def raw_deflate(data, level):
    packer = zlib.compressobj(level, zlib.DEFLATED, -15)
    return packer.compress(data) + packer.flush()


@unittest.skipIf(shutil.which("g++") is None, "g++ not available")
class SyncTinflTestCase(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.binary = Path(cls.tmp.name) / "test_tinfl"
        built = subprocess.run(
            ["g++", "-std=c++17", "-Wall", "-DNRF52_PLATFORM", "-I", str(SRC), "-o", str(cls.binary),
             str(MOD / "tests" / "test_tinfl.cpp"), str(SRC / "helpers" / "sync_settings" / "SyncTinfl.cpp")],
            capture_output=True, text=True,
        )
        assert built.returncode == 0, built.stderr

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def decode(self, data, level=6):
        packed = Path(self.tmp.name) / "packed.bin"
        expected = Path(self.tmp.name) / "expected.bin"
        packed.write_bytes(raw_deflate(data, level))
        expected.write_bytes(data)
        ran = subprocess.run([str(self.binary), str(packed), str(expected)], capture_output=True, text=True)
        self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)

    def test_region_table_round_trips(self):
        table = b"us|us-southeast|us-gc|us-al|us-al-mob|us-al-fhp|us-al-guf|us-ms|us-fl|us-fl-nw\n" * 3
        self.decode(table)

    def test_every_level_round_trips(self):
        data = bytes(range(256)) * 4 + b"flood:64/64/8 adv:240m/23h path:1 loop:1 ack:1" * 8
        for level in range(0, 10):
            with self.subTest(level=level):
                self.decode(data, level)

    def test_incompressible_payload_round_trips(self):
        self.decode(bytes((i * 151 + 7) % 256 for i in range(700)))

    def test_esp32_builds_compile_nothing(self):
        unit = (SRC / "helpers" / "sync_settings" / "SyncTinfl.cpp").read_text()
        self.assertIn("#if defined(NRF52_PLATFORM)", unit)


if __name__ == "__main__":
    unittest.main()
