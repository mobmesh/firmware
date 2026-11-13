"""Compile and run core's ping/trace host tests.

ModProbe.cpp is pure text and arithmetic, with no radio, so the shipping source compiles
on the host unchanged and parsing, the TRACE path, per-link SNRs and reply packing are
checked against what ships.
"""

import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent.parent
MOD = REPO_ROOT / "mods" / "core"


@unittest.skipIf(shutil.which("g++") is None, "g++ not available")
class CoreProbeHostTestCase(unittest.TestCase):
    def test_probe_host_cases(self):
        with tempfile.TemporaryDirectory() as tmp:
            binary = Path(tmp) / "test_probe"
            built = subprocess.run(
                ["g++", "-std=c++17", "-Wall", "-I", str(MOD / "files" / "src"), "-o", str(binary),
                 str(MOD / "tests" / "test_probe.cpp"), str(MOD / "files" / "src" / "helpers" / "ModProbe.cpp")],
                capture_output=True, text=True,
            )
            self.assertEqual(built.returncode, 0, built.stderr)
            ran = subprocess.run([str(binary)], capture_output=True, text=True)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)


if __name__ == "__main__":
    unittest.main()
