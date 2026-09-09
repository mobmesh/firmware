"""Run the OTA page's browser cases through pytest.

Kept out of the mod's own directory so `pytest scripts/tests` stays the single entry
point. Skips when Playwright or a Chromium build is absent, so a machine without the rig
still runs the rest of the suite.
"""

import glob
import os
import subprocess
import sys
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent.parent
TESTS = REPO_ROOT / "mods" / "hotspot-ota" / "tests"
VENV = REPO_ROOT / "docs" / "flasher 2.0" / "testrig" / "esptool-venv" / "bin" / "python"


def _has_chromium():
    return bool(glob.glob(os.path.expanduser(
        "~/.cache/ms-playwright/chromium-*/chrome-linux64/chrome")))


def _python():
    # The rig's venv carries playwright; the ambient interpreter generally does not.
    if VENV.exists():
        return str(VENV)
    return sys.executable


@unittest.skipUnless(_has_chromium(), "no playwright chromium installed")
class OtaPageBrowserTestCase(unittest.TestCase):
    def test_browser_cases(self):
        result = subprocess.run(
            [_python(), "-m", "unittest", "test_ota_page", "-v"],
            cwd=str(TESTS), capture_output=True, text=True, timeout=600,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
