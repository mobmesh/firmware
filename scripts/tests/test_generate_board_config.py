#!/usr/bin/env python3
"""Unit tests for generate-board-config.py's inject-env logic.

Run: python3 scripts/tests/test_generate_board_config.py
"""
import argparse
import importlib.util
import json
import struct
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

# Mirrors the real upstream variant: commented-out flags sit at column 0 between
# the last real flag and the next key.
COMMENTED_FLAGS_INI = """[env:heltec_v4_repeater]
board = heltec_v4
build_flags =
  ${heltec_v4_oled.build_flags}
  -D DISPLAY_CLASS=SSD1306Display
  -D MAX_NEIGHBOURS=50
;  -D MESH_PACKET_LOGGING=1
;  -D MESH_DEBUG=1
build_src_filter = ${heltec_v4_oled.build_src_filter}
  +<helpers/ui/SSD1306Display.cpp>
lib_deps =
  foo
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
        self._write(f"mods/{name}/mod.yaml", f"name: {name}\n")
        for patch_id, sidecar in patches.items():
            self._write(f"mods/{name}/patches/{patch_id}.meta.yaml", sidecar)
            self._write(f"mods/{name}/patches/{patch_id}_test.patch", "")

    def _make_overrides(self, board: str, content: str):
        if "capabilities:" not in content:
            content = "capabilities: {}\n" + content
        if "qemu:" not in content:
            content += "qemu:\n  enabled: false\n"
        if "flasher:" not in content:
            content += "flasher:\n  label: test\n  connect_note: test\n  post_flash_note: test\n"
        self._write(f"variants/{board}/overrides.yaml", content)

    def _run(self, ini_path: Path, board="heltec_v4", env="heltec_v4_repeater", mods="hotspot-ota",
             append_flags=""):
        args = argparse.Namespace(
            board=board, env=env, platformio_ini=str(ini_path), mods=mods,
            append_flags=append_flags,
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
            "build_values:\n  PIN_HOTSPOT_PWR: 47\npartitions_override: null\n"
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

    def _minimal_mod_and_overrides(self):
        self._make_mod("hotspot-ota", {"0001": "id: \"0001\"\ntitle: x\nrequires: []\n"})
        self._make_overrides("heltec_v4", "build_values: {}\npartitions_override: null\n")

    def _flag_order(self, section: str, *needles):
        return [section.index(n) for n in needles]

    def test_appended_flag_lands_after_upstreams_own(self):
        # The whole point of appending: a later -U beats the env's earlier -D.
        # Prepended, it would be silently re-defined and the override would do nothing.
        self._minimal_mod_and_overrides()
        ini_path = self._write("platformio.ini", COMMENTED_FLAGS_INI)

        self._run(ini_path, append_flags="-UDISPLAY_CLASS")

        section = ini_path.read_text()
        define_at, undef_at = self._flag_order(
            section, "-D DISPLAY_CLASS=SSD1306Display", "-UDISPLAY_CLASS"
        )
        self.assertLess(define_at, undef_at)

    def test_commented_flags_do_not_truncate_the_block(self):
        # ";  -D ..." lines sit at column 0, so a naive "stop at the first
        # unindented line" would insert before the last real flag.
        self._minimal_mod_and_overrides()
        ini_path = self._write("platformio.ini", COMMENTED_FLAGS_INI)

        self._run(ini_path, append_flags="-UDISPLAY_CLASS")

        section = ini_path.read_text()
        last_real_at, undef_at, next_key_at = self._flag_order(
            section, "-D MAX_NEIGHBOURS=50", "-UDISPLAY_CLASS", "build_src_filter"
        )
        self.assertLess(last_real_at, undef_at)
        self.assertLess(undef_at, next_key_at)

    def test_real_flag_after_a_comment_still_counts_as_the_block_end(self):
        # The case that makes comment-skipping load-bearing: stopping at the first
        # comment would insert before -D DISPLAY_CLASS, so the -U would not win.
        self._minimal_mod_and_overrides()
        ini_path = self._write("platformio.ini", COMMENTED_FLAGS_INI.replace(
            ";  -D MESH_DEBUG=1\n",
            ";  -D MESH_DEBUG=1\n  -D DISPLAY_CLASS=SSD1306Display\n",
        ))

        self._run(ini_path, append_flags="-UDISPLAY_CLASS")

        section = ini_path.read_text()
        last_define_at = section.rindex("-D DISPLAY_CLASS=SSD1306Display")
        self.assertLess(last_define_at, section.index("-UDISPLAY_CLASS"))

    def test_multiple_appended_flags_keep_their_order(self):
        self._minimal_mod_and_overrides()
        ini_path = self._write("platformio.ini", COMMENTED_FLAGS_INI)

        self._run(ini_path, append_flags="-UDISPLAY_CLASS,-UFOO")

        section = ini_path.read_text()
        first_at, second_at = self._flag_order(section, "-UDISPLAY_CLASS", "-UFOO")
        self.assertLess(first_at, second_at)

    def test_no_append_flags_leaves_the_block_alone(self):
        self._minimal_mod_and_overrides()
        ini_path = self._write("platformio.ini", COMMENTED_FLAGS_INI)

        self._run(ini_path, append_flags="")

        self.assertNotIn("-U", ini_path.read_text())

    def test_board_level_append_from_overrides_yaml(self):
        # Same mechanism, declared per-board instead of per-target.
        self._make_mod("hotspot-ota", {"0001": "id: \"0001\"\ntitle: x\nrequires: []\n"})
        self._make_overrides("heltec_v4", (
            "build_values: {}\nbuild_flags_append: [\"-UDISPLAY_CLASS\"]\n"
            "partitions_override: null\n"
        ))
        ini_path = self._write("platformio.ini", COMMENTED_FLAGS_INI)

        self._run(ini_path)

        section = ini_path.read_text()
        define_at, undef_at = self._flag_order(
            section, "-D DISPLAY_CLASS=SSD1306Display", "-UDISPLAY_CLASS"
        )
        self.assertLess(define_at, undef_at)

    def test_partitions_override_vendors_file_and_uses_project_relative_path(self):
        self._make_mod("hotspot-ota", {"0001": "id: \"0001\"\ntitle: x\nrequires: []\n"})
        self._make_overrides("xiao_c3", (
            "build_values: {}\npartitions_override: partitions_xiao_c3.csv\n"
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
            "build_values: {}\npartitions_override: does_not_exist.csv\n"
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
        self._make_overrides("heltec_v4", "build_values: {}\npartitions_override: null\n")
        ini_path = self._write("platformio.ini", SAMPLE_INI)

        with self.assertRaises(SystemExit):
            self._run(ini_path, env="does_not_exist")

    def test_missing_build_flags_key_hard_fails(self):
        self._make_mod("hotspot-ota", {"0001": "id: \"0001\"\ntitle: x\nrequires: []\n"})
        self._make_overrides("heltec_v4", "build_values: {}\npartitions_override: null\n")
        ini_path = self._write("platformio.ini", NO_BUILD_FLAGS_INI)

        with self.assertRaises(SystemExit):
            self._run(ini_path)

    def test_duplicate_env_flag_across_mods_hard_fails(self):
        self._make_mod("mod-a", {"0001": "id: \"0001\"\ntitle: a\nrequires: []\nenv_flag: WITH_SAME_FLAG\n"})
        self._make_mod("mod-b", {"0001": "id: \"0001\"\ntitle: b\nrequires: []\nenv_flag: WITH_SAME_FLAG\n"})
        self._make_overrides("heltec_v4", "build_values: {}\npartitions_override: null\n")
        ini_path = self._write("platformio.ini", SAMPLE_INI)

        with self.assertRaises(SystemExit):
            self._run(ini_path, mods="mod-a,mod-b")

    def test_generated_composition_sources_are_injected(self):
        self._write(
            "mods/shim/mod.yaml",
            "name: shim\ncomposition:\n  outputs:\n"
            "    hooks: src/helpers/ModHooks.cpp\n"
            "    cli: src/helpers/esp32/CommonCliMods.cpp\n",
        )
        self._make_overrides("heltec_v4", "build_values: {}\npartitions_override: null\n")
        ini_path = self._write("platformio.ini", SAMPLE_INI)

        self._run(ini_path, mods="shim")

        result = ini_path.read_text()
        self.assertIn("+<helpers/ModHooks.cpp>", result)
        self.assertIn("+<helpers/esp32/CommonCliMods.cpp>", result)


class BoardsJsonTestCase(unittest.TestCase):
    """cmd_boards_json: the per-variant postFlashCommands passthrough."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.repo_root = Path(self.tmp.name)
        self._orig_repo_root = gbc.REPO_ROOT
        gbc.REPO_ROOT = self.repo_root
        self.addCleanup(self._restore)
        self.addCleanup(self.tmp.cleanup)

    def _restore(self):
        gbc.REPO_ROOT = self._orig_repo_root

    def _partitions_bin(self) -> Path:
        """Minimal dual-OTA table: otadata + ota_0 + ota_1, then a stop byte."""
        entries = [
            (gbc.PARTITION_TYPE_DATA, gbc.DATA_SUBTYPE_OTA, 0xE000, 0x2000),
            (gbc.PARTITION_TYPE_APP, gbc.APP_SUBTYPE_OTA_0, 0x10000, 0x640000),
            (gbc.PARTITION_TYPE_APP, gbc.APP_SUBTYPE_OTA_1, 0x650000, 0x640000),
        ]
        blob = b""
        for ptype, subtype, offset, size in entries:
            blob += gbc.PARTITION_MAGIC + bytes([ptype, subtype])
            blob += struct.pack("<II", offset, size)
            blob += b"\x00" * (gbc.PARTITION_ENTRY_SIZE - 12)
        blob += b"\xff" * gbc.PARTITION_ENTRY_SIZE
        path = self.repo_root / "partitions.bin"
        path.write_bytes(blob)
        return path

    def _run(self, overrides_yaml: str, variant_id: str) -> dict:
        (self.repo_root / "variants" / "heltec_v4").mkdir(parents=True, exist_ok=True)
        (self.repo_root / "variants" / "heltec_v4" / "overrides.yaml").write_text(overrides_yaml)

        upstream = self.repo_root / "upstream" / "boards"
        upstream.mkdir(parents=True, exist_ok=True)
        (upstream / "heltec_v4.json").write_text('{"build": {"mcu": "esp32s3"}}')

        out = self.repo_root / "boards.json"
        gbc.cmd_boards_json(argparse.Namespace(
            board="heltec_v4",
            upstream_dir=str(self.repo_root / "upstream"),
            partitions_bin=str(self._partitions_bin()),
            variant_id=variant_id,
            variant_label="Repeater",
            asset_basename="heltec_v4_rep_mobmesh",
            version="1.16.0",
            firmware_file="heltec_v4/repeater/firmware.bin",
            output=str(out),
        ))
        return json.loads(out.read_text())["heltec_v4"]["variants"][variant_id]

    FLASHER_BASE = (
        "capabilities: {}\nbuild_values: {}\npartitions_override: null\n"
        "qemu:\n  enabled: false\n"
        "flasher:\n  label: \"Heltec V4\"\n  connect_note: \"c\"\n  post_flash_note: \"p\"\n"
    )

    def test_post_flash_commands_reach_the_variant(self):
        variant = self._run(
            self.FLASHER_BASE
            + "  post_flash_commands:\n    repeater:\n      - \"set ota.fw.url https://x/f.bin\"\n",
            "repeater",
        )
        self.assertEqual(
            variant["postFlashCommands"],
            ["set ota.fw.url https://x/f.bin"],
        )

    def test_variant_without_commands_omits_the_key(self):
        # flasher.js reads a missing key as an empty list, so don't emit noise.
        variant = self._run(
            self.FLASHER_BASE
            + "  post_flash_commands:\n    repeater:\n      - \"set a b\"\n",
            "room_server",
        )
        self.assertNotIn("postFlashCommands", variant)

    def test_absent_post_flash_commands_block_is_fine(self):
        variant = self._run(self.FLASHER_BASE, "repeater")
        self.assertNotIn("postFlashCommands", variant)


class ResolveTargetsTestCase(unittest.TestCase):
    """cmd_resolve_targets: mod resolution and the fixed asset_basename."""

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

    def _make_mod(self, name: str):
        self._write(f"mods/{name}/mod.yaml", f"name: {name}\n")

    def _run(self, mods, core=None) -> dict:
        head = f"core_mods: [{', '.join(core or [])}]\n"
        self._write("variants/heltec_v4/overrides.yaml", (
            "capabilities: {}\nbuild_values: {}\npartitions_override: null\n"
            "qemu:\n  enabled: false\n"
            "flasher:\n  label: test\n  connect_note: test\n  post_flash_note: test\n"
        ))
        self._write("build-targets.yaml", head + (
            "roles:\n"
            "  repeater:\n"
            "    asset_role_abbrev: rep\n"
            "    release_channel: repeater\n"
            "release_channels:\n"
            "  repeater:\n"
            "    upstream_tag_prefix: repeater\n"
            "    release_title: Repeater\n"
            "    make_latest: true\n"
            "targets:\n"
            "  - board: heltec_v4\n"
            "    role: repeater\n"
            "    build_env: heltec_v4_repeater\n"
            "    vendor_flasher_assets: true\n"
            "    qemu_boot_check: false\n"
            f"    mods: [{', '.join(mods)}]\n"
        ))
        out = self.repo_root / "out.json"
        gbc.cmd_resolve_targets(argparse.Namespace(out=str(out), plan_out=None))
        return json.loads(out.read_text())["include"][0]

    def test_asset_basename_does_not_vary_with_the_mod_set(self):
        self._make_mod("hotspot-ota")
        self._make_mod("shim")

        one = self._run(["shim"])
        both = self._run(["hotspot-ota", "shim"])

        self.assertEqual(one["asset_basename"], "heltec_v4_rep_mobmesh")
        self.assertEqual(both["asset_basename"], "heltec_v4_rep_mobmesh")

    def test_core_mods_apply_before_target_mods(self):
        self._make_mod("shim")
        self._make_mod("hotspot-ota")
        self._make_mod("batt-saver")

        row = self._run(["batt-saver"], core=["shim", "hotspot-ota"])

        self.assertEqual(row["mods"], ["shim", "hotspot-ota", "batt-saver"])

    def test_core_mods_alone_are_enough(self):
        self._make_mod("shim")
        self._make_mod("hotspot-ota")

        row = self._run([], core=["shim", "hotspot-ota"])

        self.assertEqual(row["mods"], ["shim", "hotspot-ota"])

    def test_target_repeating_a_core_mod_does_not_duplicate_or_reorder(self):
        self._make_mod("shim")
        self._make_mod("hotspot-ota")

        row = self._run(["hotspot-ota"], core=["shim", "hotspot-ota"])

        self.assertEqual(row["mods"], ["shim", "hotspot-ota"])

    def test_no_mods_anywhere_is_an_error(self):
        with self.assertRaises(ValueError):
            self._run([])


if __name__ == "__main__":
    unittest.main()
