"""Compile and run try-settings' host tests.

The mod's logic is reachable on the host: TrySetIntegration.cpp talks to the world only
through the shim's reverse hooks and TrySet's persistence, both of which the harness
fakes. Compiling the shipping source unmodified means these cases cover what ships
rather than a transcription of it.
"""

import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent.parent
TESTS = REPO_ROOT / "mods" / "try-settings" / "tests"
SOURCE = REPO_ROOT / "mods" / "try-settings" / "files" / "src" / "helpers" / "esp32" / "TrySetIntegration.cpp"


@unittest.skipIf(shutil.which("g++") is None, "g++ not available")
class TrySetBehaviourTestCase(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls._tmp = tempfile.TemporaryDirectory()
        cls.binary = Path(cls._tmp.name) / "tryset_test"
        result = subprocess.run(
            [
                "g++", "-std=c++17",
                "-I", str(TESTS / "stubs"),
                "-I", str(TESTS),
                "-o", str(cls.binary),
                str(TESTS / "test_tryset.cpp"),
                str(TESTS / "harness.cpp"),
                str(SOURCE),
            ],
            capture_output=True,
            text=True,
        )
        if result.returncode:
            raise AssertionError(f"host test did not compile:\n{result.stderr}")

    @classmethod
    def tearDownClass(cls):
        cls._tmp.cleanup()

    def test_behaviour(self):
        result = subprocess.run([str(self.binary)], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_reboot_reverts_rather_than_resumes(self):
        # A separate process: `booted` is file-static, so a reboot is a fresh start.
        result = subprocess.run([str(self.binary), "reboot"], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
