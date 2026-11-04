"""Compile and run direct-retry's host tests against the shipping core."""

import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent.parent
MOD = REPO_ROOT / "mods" / "direct-retry"


@unittest.skipIf(shutil.which("g++") is None, "g++ not available")
class DirectRetryHostTestCase(unittest.TestCase):
    def test_core(self):
        with tempfile.TemporaryDirectory() as tmp:
            binary = Path(tmp) / "test_direct_retry"
            built = subprocess.run(
                ["g++", "-std=c++17", "-Wall", "-I", str(MOD / "files" / "src"), "-o", str(binary),
                 str(MOD / "tests" / "test_direct_retry.cpp"), str(MOD / "files/src/helpers/direct_retry/DirectRetryCore.cpp")],
                capture_output=True, text=True,
            )
            self.assertEqual(built.returncode, 0, built.stderr)
            ran = subprocess.run([str(binary)], capture_output=True, text=True)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)


if __name__ == "__main__":
    unittest.main()
