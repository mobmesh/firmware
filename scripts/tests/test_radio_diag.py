"""Compile and run radio-diag's host tests.

LnaStudy.cpp, RfScan.cpp, LinkReport.cpp, NoiseLog.cpp and DiagSummary.cpp are pure
arithmetic and text, with no radio, clock or storage, so the shipping sources compile on
the host unchanged and the verdict and finding rules are checked against what ships.
"""

import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent.parent
MOD = REPO_ROOT / "mods" / "radio-diag"
SOURCES = MOD / "files" / "src" / "helpers" / "radio_diag"


@unittest.skipIf(shutil.which("g++") is None, "g++ not available")
class RadioDiagHostTestCase(unittest.TestCase):
    def run_case(self, test_name, *source_names):
        with tempfile.TemporaryDirectory() as tmp:
            binary = Path(tmp) / test_name
            built = subprocess.run(
                ["g++", "-std=c++17", "-Wall", "-I", str(MOD / "files" / "src"), "-o", str(binary),
                 str(MOD / "tests" / f"{test_name}.cpp"), *[str(SOURCES / name) for name in source_names]],
                capture_output=True, text=True,
            )
            self.assertEqual(built.returncode, 0, built.stderr)
            ran = subprocess.run([str(binary)], capture_output=True, text=True)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)

    def test_lna_study_host_cases(self):
        self.run_case("test_lna_study", "LnaStudy.cpp")

    def test_rfscan_host_cases(self):
        self.run_case("test_rfscan", "RfScan.cpp")

    def test_link_report_host_cases(self):
        self.run_case("test_link_report", "LinkReport.cpp")

    def test_diag_summary_host_cases(self):
        self.run_case("test_diag_summary", "DiagSummary.cpp", "LinkReport.cpp", "RfScan.cpp")

    def test_noise_log_host_cases(self):
        self.run_case("test_noise_log", "NoiseLog.cpp")

if __name__ == "__main__":
    unittest.main()
