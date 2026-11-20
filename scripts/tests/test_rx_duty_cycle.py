"""Compile and run rx-duty-cycle's host tests; the geometry and the engine are pure, so the shipping files compile on the host unchanged."""

import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent.parent
MOD = REPO_ROOT / "mods" / "rx-duty-cycle"
SOURCES = MOD / "files" / "src" / "helpers" / "rx_duty_cycle"


@unittest.skipIf(shutil.which("g++") is None, "g++ not available")
class RxDutyCycleHostTestCase(unittest.TestCase):
    def run_case(self, test_name, *source_names):
        with tempfile.TemporaryDirectory() as tmp:
            binary = Path(tmp) / test_name
            built = subprocess.run(
                ["g++", "-std=c++17", "-Wall", "-Werror", "-I", str(MOD / "files" / "src"),
                 "-o", str(binary), str(MOD / "tests" / f"{test_name}.cpp"), *[str(SOURCES / name) for name in source_names]],
                capture_output=True, text=True,
            )
            self.assertEqual(built.returncode, 0, built.stderr)
            ran = subprocess.run([str(binary)], capture_output=True, text=True)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)

    def test_geometry(self):
        self.run_case("test_geometry", "RxdcGeometry.cpp")

    def test_engine(self):
        self.run_case("test_engine", "RxdcEngine.cpp", "RxdcGeometry.cpp")


if __name__ == "__main__":
    unittest.main()
