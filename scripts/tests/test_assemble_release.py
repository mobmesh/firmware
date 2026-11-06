import importlib.util
import json
import tempfile
import unittest
from pathlib import Path


SCRIPT = Path(__file__).resolve().parents[1] / "assemble-release.py"
SPEC = importlib.util.spec_from_file_location("assemble_release", SCRIPT)
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class AssembleReleaseTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.inputs = self.root / "inputs"
        self.flasher = self.root / "flasher"
        self.output = self.root / "release"
        self.boards = self.flasher / "data/auto_boards.json"
        self.boards.parent.mkdir(parents=True)

    def tearDown(self):
        self.temp.cleanup()

    def write_boards(self, variants=("repeater", "room_server"), boot=b"old"):
        (self.flasher / "bin/boot").mkdir(parents=True, exist_ok=True)
        (self.flasher / "bin/boot/boot_app0.bin").write_bytes(b"app0")
        (self.flasher / "bin/boot/test_bootloader.bin").write_bytes(boot)
        (self.flasher / "bin/boot/test_partitions.bin").write_bytes(boot)
        entry = self.board_entry("v1", "repeater")
        entry["variants"] = {
            role: {"label": role, "version": "v0", "firmwareFile": f"bin/{role}/test.bin"}
            for role in variants
        }
        self.boards.write_text(json.dumps({"test": entry, "_version": "v0"}))

    @staticmethod
    def board_entry(version, role):
        return {
            "label": "Test",
            "offsets": {"app0": "0x10000"},
            "bootApp0": "bin/boot/boot_app0.bin",
            "bootloaderFile": "bin/boot/test_bootloader.bin",
            "partitionsFile": "bin/boot/test_partitions.bin",
            "variants": {
                role: {
                    "label": role,
                    "version": version,
                    "firmwareFile": f"bin/{role}/test.bin",
                }
            },
        }

    def bundle(self, role, owner=False, boot=b"old", app0=b"app0", version="v1"):
        root = self.inputs / f"test-{role}"
        root.mkdir(parents=True)
        files = {
            "app.bin": role.encode(),
            "merged.bin": b"merged-" + role.encode(),
            "bootloader.bin": boot,
            "partitions.bin": boot,
            "boot_app0.bin": app0,
            "release-notes.md": b"notes",
        }
        for name, value in files.items():
            (root / name).write_bytes(value)
        (root / "board-entry.json").write_text(json.dumps(self.board_entry(version, role)))
        (root / "meta.json").write_text(
            json.dumps(
                {
                    "board": "test",
                    "role": role,
                    "version": version,
                    "release_tag": f"{role}-{version}-mobmesh",
                    "release_title": role,
                    "make_latest": role == "repeater",
                    "vendor_flasher_assets": owner,
                    "app_asset": f"test-{role}-{version}.bin",
                    "merged_asset": f"test-{role}-{version}-merged.bin",
                    "firmware_path": f"bin/{role}/test.bin",
                }
            )
        )

    def assemble(self):
        return MODULE.assemble(self.inputs, self.flasher, self.boards, self.output)

    def test_two_matching_roles_publish_together(self):
        self.write_boards()
        self.bundle("repeater", owner=True)
        self.bundle("room_server")
        result = self.assemble()
        self.assertEqual(len(result["web_published"]), 2)
        self.assertEqual((self.flasher / "bin/repeater/test.bin").read_bytes(), b"repeater")
        self.assertEqual((self.flasher / "bin/room_server/test.bin").read_bytes(), b"room_server")
        self.assertEqual(len(result["releases"]), 2)

    def test_role_boot_mismatch_withholds_the_board(self):
        self.write_boards()
        self.bundle("repeater", owner=True)
        self.bundle("room_server", boot=b"different")
        result = self.assemble()
        self.assertEqual(result["web_published"], [])
        self.assertEqual(result["web_skipped"][0]["reason"], "role boot files differ")
        self.assertEqual(len(result["releases"]), 2)

    def test_changed_boot_requires_every_retained_role(self):
        self.write_boards()
        self.bundle("repeater", owner=True, boot=b"new")
        result = self.assemble()
        self.assertEqual(result["web_published"], [])
        self.assertIn("retained role", result["web_skipped"][0]["reason"])
        self.assertEqual((self.flasher / "bin/boot/test_bootloader.bin").read_bytes(), b"old")

    def test_non_owner_uses_matching_vendored_boot_files(self):
        self.write_boards()
        self.bundle("room_server")
        result = self.assemble()
        self.assertEqual(result["web_published"], [{"board": "test", "role": "room_server"}])

    def test_boot_app0_change_requires_complete_existing_build(self):
        self.write_boards()
        self.bundle("repeater", owner=True, app0=b"new-app0")
        result = self.assemble()
        self.assertEqual(result["web_published"], [])
        self.assertIn("boot_app0", result["web_skipped"][0]["reason"])
        self.assertEqual((self.flasher / "bin/boot/boot_app0.bin").read_bytes(), b"app0")

    def test_complete_build_can_replace_boot_app0(self):
        self.write_boards()
        self.bundle("repeater", owner=True, app0=b"new-app0")
        self.bundle("room_server", app0=b"new-app0")
        result = self.assemble()
        self.assertEqual(len(result["web_published"]), 2)
        self.assertEqual(
            (self.flasher / "bin/boot/boot_app0.bin").read_bytes(), b"new-app0"
        )

    def nrf52_bundle(self, version="v1", board="t1"):
        root = self.inputs / f"{board}-repeater"
        root.mkdir(parents=True)
        (root / "app.zip").write_bytes(b"dfu")
        (root / "release-notes.md").write_bytes(b"notes")
        (root / "board-entry.json").write_text(json.dumps({
            "label": "T1",
            "platform": "nrf52",
            "stockDevice": "Heltec Mesh Node T1",
            "variants": {"repeater": {"label": "Repeater", "version": version,
                                      "firmwareFile": f"bin/repeater/{board}.zip"}},
        }))
        (root / "meta.json").write_text(json.dumps({
            "platform": "nrf52",
            "board": board,
            "role": "repeater",
            "version": version,
            "release_tag": f"repeater-{version}-mobmesh",
            "release_title": "repeater",
            "make_latest": True,
            "app_asset": f"{board}-repeater-{version}.zip",
            "firmware_path": f"bin/repeater/{board}.zip",
        }))

    def test_nrf52_publishes_its_package_and_entry_beside_esp32_builds(self):
        self.write_boards()
        self.bundle("repeater", owner=True)
        self.bundle("room_server")
        self.nrf52_bundle()
        result = self.assemble()
        self.assertIn({"board": "t1", "role": "repeater"}, result["web_published"])
        self.assertEqual(len(result["web_published"]), 3)
        self.assertEqual((self.flasher / "bin/repeater/t1.zip").read_bytes(), b"dfu")
        t1 = next(r for r in result["releases"] if r["board"] == "t1")
        self.assertEqual([Path(f).name for f in t1["release_files"]], ["t1-repeater-v1.zip"])
        boards = json.loads(self.boards.read_text())
        self.assertEqual(boards["t1"]["platform"], "nrf52")
        self.assertEqual(boards["t1"]["variants"]["repeater"]["firmwareFile"], "bin/repeater/t1.zip")
        self.assertEqual(boards["test"]["variants"]["repeater"]["version"], "v1")

    def test_nrf52_alone_leaves_esp32_entries_and_boot_files_alone(self):
        self.write_boards()
        before = json.loads(self.boards.read_text())
        boot = (self.flasher / "bin/boot/test_bootloader.bin").read_bytes()
        self.nrf52_bundle()
        result = self.assemble()
        self.assertEqual(result["web_published"], [{"board": "t1", "role": "repeater"}])
        after = json.loads(self.boards.read_text())
        self.assertEqual(after["test"], before["test"])
        self.assertEqual((self.flasher / "bin/boot/test_bootloader.bin").read_bytes(), boot)

    def test_nrf52_keeps_roles_it_did_not_rebuild(self):
        self.write_boards()
        boards = json.loads(self.boards.read_text())
        boards["t1"] = {"label": "T1", "platform": "nrf52", "variants": {
            "room_server": {"label": "Room", "version": "v0", "firmwareFile": "bin/room_server/t1.zip"}}}
        self.boards.write_text(json.dumps(boards))
        self.nrf52_bundle()
        self.assemble()
        self.assertEqual(set(json.loads(self.boards.read_text())["t1"]["variants"]), {"repeater", "room_server"})

    def test_an_esp32_board_name_cannot_become_nrf52(self):
        self.write_boards()
        self.nrf52_bundle(board="test")
        with self.assertRaises(ValueError):
            self.assemble()

    def test_nrf52_boards_never_block_a_boot_app0_change(self):
        self.write_boards()
        boards = json.loads(self.boards.read_text())
        boards["t1"] = {"label": "T1", "platform": "nrf52", "variants": {
            "repeater": {"label": "Repeater", "version": "v0", "firmwareFile": "bin/repeater/t1.zip"}}}
        self.boards.write_text(json.dumps(boards))
        self.bundle("repeater", owner=True, app0=b"new-app0")
        self.bundle("room_server", app0=b"new-app0")
        result = self.assemble()
        self.assertEqual(len(result["web_published"]), 2)
        self.assertEqual((self.flasher / "bin/boot/boot_app0.bin").read_bytes(), b"new-app0")

    def test_nrf52_without_its_package_is_refused(self):
        self.nrf52_bundle()
        (self.inputs / "t1-repeater/app.zip").unlink()
        with self.assertRaises(ValueError):
            self.assemble()

if __name__ == "__main__":
    unittest.main()
