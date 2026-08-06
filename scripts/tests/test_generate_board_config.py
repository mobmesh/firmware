#!/usr/bin/env python3
"""Unit tests for generate-board-config.py's inject-env logic.

Run: python3 scripts/tests/test_generate_board_config.py
"""
import argparse
import importlib.util
import tempfile
import unittest
from pathlib import Path

SCRIPT_PATH = Path(__file__).resolve().parent.parent / "generate-board-config.py"
spec = importlib.util.spec_from_file_location("generate_board_config", SCRIPT_PATH)
gbc = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gbc)

SAMPLE_INI = """[env:heltec_v4_repeater]
board = heltec_v4
build_flags =
  -D ADVERT_LON=0.0
  -D MAX_NEIGHBOURS=50
build_src_filter = ${heltec_v4_oled.build_src_filter}
  +<helpers/ui/SSD1306Display.cpp>
lib_deps =
  foo

[env:heltec_v4_room_server]
board = heltec_v4
build_flags =
  -D ADVERT_LON=0.0
build_src_filter = ${heltec_v4_oled.build_src_filter}
  +<helpers/ui/SSD1306Display.cpp>
"""

NO_BUILD_FLAGS_INI = """[env:heltec_v4_repeater]
board = heltec_v4
build_src_filter = foo
"""


class InjectEnvTestCase(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.repo_root = Path(self.tmp.name)
        self._orig_repo_root = gbc.REPO_ROOT
        gbc.REPO_ROOT = self.repo_root
        self.addCleanup(self._restore)
        self.addCleanup(self.tmp.cleanup)

    def _restore(self):
        gbc.REPO_ROOT = self._orig_repo_root

    def _write(self, rel_path: str, content: str) -> Path:
        path = self.repo_root / rel_path
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content)
        return path

    def _make_mod(self, name: str, patches: dict):
        for patch_id, sidecar in patches.items():
            self._write(f"mods/{name}/patches/{patch_id}.meta.yaml", sidecar)

    def _make_overrides(self, board: str, content: str):
        self._write(f"variants/{board}/overrides.yaml", content)

    def _run(self, ini_path: Path, board="heltec_v4", env="heltec_v4_repeater", mods="hotspot-ota"):
        args = argparse.Namespace(
            board=board, env=env, platformio_ini=str(ini_path), mods=mods,
        )
        gbc.cmd_inject_env(args)

    def test_normal_insertion(self):
        self._make_mod("hotspot-ota", {
            "0001": (
                "id: \"0001\"\ntitle: hotspot-fetch-ota\nrequires: []\n"
                "env_flag: WITH_HOTSPOT_OTA\n"
                "build_src_filter: [\"+<helpers/esp32/HotspotOTA.cpp>\"]\n"
            ),
        })
        self._make_overrides("heltec_v4", (
            "build_flags:\n  PIN_HOTSPOT_PWR: 47\npartitions_override: null\n"
        ))
        ini_path = self._write("platformio.ini", SAMPLE_INI)

        self._run(ini_path)

        result = ini_path.read_text()
        repeater_section = result.split("[env:heltec_v4_room_server]")[0]
        self.assertIn("-D WITH_HOTSPOT_OTA=1", repeater_section)
        self.assertIn("-D PIN_HOTSPOT_PWR=47", repeater_section)
        self.assertIn("+<helpers/esp32/HotspotOTA.cpp>", repeater_section)
        # untouched env must not pick up the other env's injected flags
        room_section = result.split("[env:heltec_v4_room_server]")[1]
        self.assertNotIn("WITH_HOTSPOT_OTA", room_section)

    def test_partitions_override_vendors_file_and_uses_project_relative_path(self):
        self._make_mod("hotspot-ota", {"0001": "id: \"0001\"\ntitle: x\nrequires: []\n"})
        self._make_overrides("xiao_c3", (
            "build_flags: {}\npartitions_override: partitions_xiao_c3.csv\n"
        ))
        self._write("variants/xiao_c3/partitions_xiao_c3.csv", "# Name,Type,SubType,Offset,Size\n")
        # Mirrors the real layout: the target ini lives under upstream-src/variants/<board>/,
        # a different directory from this repo's own variants/<board>/ where the CSV is authored.
        ini_path = self._write("upstream-src/variants/xiao_c3/platformio.ini", SAMPLE_INI.replace(
            "heltec_v4", "xiao_c3"
        ))

        self._run(ini_path, board="xiao_c3", env="xiao_c3_repeater")

        result = ini_path.read_text()
        self.assertIn("board_build.partitions = variants/xiao_c3/partitions_xiao_c3.csv", result)
        vendored = ini_path.parent / "partitions_xiao_c3.csv"
        self.assertTrue(vendored.exists(), "partitions CSV was not copied next to the target platformio.ini")

    def test_missing_partitions_file_hard_fails(self):
        self._make_mod("hotspot-ota", {"0001": "id: \"0001\"\ntitle: x\nrequires: []\n"})
        self._make_overrides("xiao_c3", (
            "build_flags: {}\npartitions_override: does_not_exist.csv\n"
        ))
        ini_path = self._write("upstream-src/variants/xiao_c3/platformio.ini", SAMPLE_INI.replace(
            "heltec_v4", "xiao_c3"
        ))

        with self.assertRaises(SystemExit):
            self._run(ini_path, board="xiao_c3", env="xiao_c3_repeater")

    def test_load_upstream_board_json_falls_back_to_vendored_copy(self):
        # Mirrors xiao_c3: upstream ships no boards/<board>.json for this board, but this
        # repo vendors an equivalent fact file at variants/<board>/board.json.
        upstream_dir = self.repo_root / "upstream-src"
        (upstream_dir / "boards").mkdir(parents=True)
        self._write("variants/xiao_c3/board.json", '{"build": {"mcu": "esp32c3"}, "upload": {"flash_size": "4MB"}}')

        result = gbc.load_upstream_board_json(upstream_dir, "xiao_c3")

        self.assertEqual(result["build"]["mcu"], "esp32c3")

    def test_load_upstream_board_json_hard_fails_with_no_fallback(self):
        upstream_dir = self.repo_root / "upstream-src"
        (upstream_dir / "boards").mkdir(parents=True)

        with self.assertRaises(FileNotFoundError):
            gbc.load_upstream_board_json(upstream_dir, "some_board_with_no_data_anywhere")

    def test_missing_section_hard_fails(self):
        self._make_mod("hotspot-ota", {"0001": "id: \"0001\"\ntitle: x\nrequires: []\n"})
        self._make_overrides("heltec_v4", "build_flags: {}\npartitions_override: null\n")
        ini_path = self._write("platformio.ini", SAMPLE_INI)

        with self.assertRaises(SystemExit):
            self._run(ini_path, env="does_not_exist")

    def test_missing_build_flags_key_hard_fails(self):
        self._make_mod("hotspot-ota", {"0001": "id: \"0001\"\ntitle: x\nrequires: []\n"})
        self._make_overrides("heltec_v4", "build_flags: {}\npartitions_override: null\n")
        ini_path = self._write("platformio.ini", NO_BUILD_FLAGS_INI)

        with self.assertRaises(SystemExit):
            self._run(ini_path)

    def test_duplicate_env_flag_across_mods_hard_fails(self):
        self._make_mod("mod-a", {"0001": "id: \"0001\"\ntitle: a\nrequires: []\nenv_flag: WITH_SAME_FLAG\n"})
        self._make_mod("mod-b", {"0001": "id: \"0001\"\ntitle: b\nrequires: []\nenv_flag: WITH_SAME_FLAG\n"})
        self._make_overrides("heltec_v4", "build_flags: {}\npartitions_override: null\n")
        ini_path = self._write("platformio.ini", SAMPLE_INI)

        with self.assertRaises(SystemExit):
            self._run(ini_path, mods="mod-a,mod-b")


if __name__ == "__main__":
    unittest.main()
