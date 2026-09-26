"""Compile and run hotspot-ota's radio-init failure policy host test."""

import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent.parent
MOD = REPO_ROOT / "mods" / "hotspot-ota"


@unittest.skipIf(shutil.which("g++") is None, "g++ not available")
class RadioFailPolicyTestCase(unittest.TestCase):
    def test_policy(self):
        with tempfile.TemporaryDirectory() as tmp:
            binary = Path(tmp) / "radio_fail_policy"
            build = subprocess.run(
                ["g++", "-std=c++17", "-I", str(MOD / "files" / "src"), "-o", str(binary),
                 str(MOD / "tests" / "test_radio_fail_policy.cpp")],
                capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stderr)
            run = subprocess.run([str(binary)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)


if __name__ == "__main__":
    unittest.main()
