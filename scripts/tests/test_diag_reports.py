"""Compile and run diag-reports' host tests.

Every source but the integration is pure arithmetic and text, so the shipping files compile on the host unchanged.
"""

import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent.parent
MOD = REPO_ROOT / "mods" / "diag-reports"
SOURCES = MOD / "files" / "src" / "helpers" / "diag_reports"


@unittest.skipIf(shutil.which("g++") is None, "g++ not available")
class DiagReportsHostTestCase(unittest.TestCase):
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

    def test_out(self):
        self.run_case("test_out", "DiagOut.cpp")

    def test_store(self):
        self.run_case("test_store", "DiagStore.cpp", "DiagLnaAuto.cpp")

    def test_sched(self):
        self.run_case("test_sched", "DiagStore.cpp", "DiagSched.cpp", "DiagLnaAuto.cpp")

    def test_relate(self):
        self.run_case("test_relate", "DiagOut.cpp", "DiagStore.cpp", "DiagNoise.cpp", "DiagBatt.cpp", "DiagLinks.cpp", "DiagScan.cpp",
                      "DiagLna.cpp", "DiagEvidence.cpp", "DiagRelate.cpp", "DiagEngine.cpp", "DiagLnaAuto.cpp", "DiagTrend.cpp")

    def test_lna_auto(self):
        self.run_case("test_lna_auto", "DiagLnaAuto.cpp")

    def test_trend(self):
        self.run_case("test_trend", "DiagOut.cpp", "DiagStore.cpp", "DiagLnaAuto.cpp", "DiagNoise.cpp", "DiagTrend.cpp")

    def test_links(self):
        self.run_case("test_links", "DiagOut.cpp", "DiagLinks.cpp")

    def test_engine(self):
        self.run_case("test_engine", "DiagOut.cpp", "DiagStore.cpp", "DiagNoise.cpp", "DiagBatt.cpp", "DiagLinks.cpp", "DiagScan.cpp",
                      "DiagLna.cpp", "DiagEvidence.cpp", "DiagRelate.cpp", "DiagEngine.cpp", "DiagLnaAuto.cpp", "DiagTrend.cpp")


if __name__ == "__main__":
    unittest.main()
