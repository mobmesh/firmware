import sys
import tempfile
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO_ROOT / "scripts"))

from mobmesh_tools.model import ProjectModel, ProjectModelError


class CurrentProjectModelTestCase(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.model = ProjectModel.load(REPO_ROOT)

    def test_resolved_targets_preserve_release_identity(self):
        rows = {
            (target.board_id, target.role): target
            for target in self.model.build_plan.targets
        }
        self.assertEqual(set(rows), {
            ("heltec_v4", "repeater"),
            ("heltec_v4", "room_server"),
            ("xiao_c3", "repeater"),
            ("xiao_c3", "room_server"),
        })
        self.assertEqual(rows[("heltec_v4", "repeater")].asset_basename, "heltec_v4_rep_mobmesh")
        self.assertEqual(rows[("xiao_c3", "room_server")].asset_basename, "xiao_c3_room_mobmesh")
        self.assertEqual(rows[("heltec_v4", "repeater")].mods,
                         ("shim", "hotspot-ota", "timing-safety", "power-guard"))
        self.assertEqual(rows[("xiao_c3", "repeater")].mods,
                         ("shim", "hotspot-ota", "timing-safety"))

    def test_build_plan_serialization_is_deterministic(self):
        self.assertEqual(self.model.build_plan.to_json(), ProjectModel.load(REPO_ROOT).build_plan.to_json())

    def test_build_plan_has_no_unused_schema(self):
        self.assertNotIn("schema", self.model.build_plan.as_dict())

    def test_capability_state_is_present_in_every_target(self):
        for target in self.model.build_plan.targets:
            self.assertEqual(set(target.capabilities), {
                "wifi_station",
                "dual_ota",
                "battery_measurement",
                "external_power_control",
                "fem_lna_control",
                "deep_sleep_rail_shutdown",
            })


class InvalidProjectModelTestCase(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.addCleanup(self.temp.cleanup)

    def write(self, relative: str, content: str):
        path = self.root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content)

    def project(self, capability="unverified", requirement="required", core_mods="[shim, feature]"):
        self.write("build-targets.yaml", f"""
core_mods: {core_mods}
roles:
  repeater:
    asset_role_abbrev: rep
    upstream_tag_prefix: repeater
    release_title: Repeater
    make_latest: true
targets:
  - board: board
    role: repeater
    build_env: board_repeater
    vendor_flasher_assets: true
    mods: []
    qemu_boot_check: false
""")
        self.write("variants/board/overrides.yaml", f"""
capabilities:
  battery_measurement: {capability}
build_values: {{}}
qemu:
  enabled: false
flasher:
  label: Board
  connect_note: Connect
  post_flash_note: Reset
""")
        self.write("mods/shim/mod.yaml", "name: shim\n")
        self.write("mods/feature/mod.yaml", f"""
name: feature
requirements:
  {requirement}: [battery_measurement]
""")

    def test_unverified_capability_does_not_satisfy_required_contract(self):
        self.project()
        with self.assertRaisesRegex(ProjectModelError, "board capabilities do not satisfy"):
            ProjectModel.load(self.root)

    def test_unverified_capability_is_allowed_for_optional_contract(self):
        self.project(requirement="optional")
        plan = ProjectModel.load(self.root).build_plan
        self.assertEqual(plan.targets[0].capabilities["battery_measurement"], "unverified")

    def test_calibrated_capability_is_rejected(self):
        self.project(capability="calibrated", requirement="optional")
        with self.assertRaisesRegex(ProjectModelError, "expected true, false, or unverified"):
            ProjectModel.load(self.root)

    def test_duplicate_tag_prefix_is_rejected(self):
        self.project(requirement="optional")
        path = self.root / "build-targets.yaml"
        text = path.read_text().replace(
            "targets:\n",
            "  room_server:\n"
            "    asset_role_abbrev: room\n"
            "    upstream_tag_prefix: repeater\n"
            "    release_title: Room\n"
            "    make_latest: false\n"
            "targets:\n",
        )
        path.write_text(text)
        with self.assertRaisesRegex(ProjectModelError, "upstream_tag_prefix: also belongs to 'repeater'"):
            ProjectModel.load(self.root)

    def test_unknown_field_reports_its_source(self):
        self.project(requirement="optional")
        path = self.root / "variants/board/overrides.yaml"
        path.write_text(path.read_text() + "build_flags: {}\n")
        with self.assertRaisesRegex(ProjectModelError, r"overrides.yaml:root: unknown field.*build_flags"):
            ProjectModel.load(self.root)

    def test_patch_dependency_must_precede_dependent_patch(self):
        self.project(requirement="optional", core_mods="[feature, shim]")
        self.write("mods/shim/patches/0001_test.patch", "")
        self.write("mods/shim/patches/0001.meta.yaml", 'id: "0001"\ntitle: shim\n')
        self.write("mods/feature/patches/0001_test.patch", "")
        self.write(
            "mods/feature/patches/0001.meta.yaml",
            'id: "0001"\ntitle: feature\nrequires: [shim/0001]\n',
        )
        with self.assertRaisesRegex(ProjectModelError, "must be applied before feature/0001"):
            ProjectModel.load(self.root)

    def test_mod_bits_are_globally_unique(self):
        self.project(requirement="optional")
        self.write("mods/shim/mod.yaml", 'name: shim\nbit: 1\nimage_marker: "SHIM"\n')
        self.write(
            "mods/feature/mod.yaml",
            'name: feature\nbit: 1\nimage_marker: "FEATURE"\n',
        )
        with self.assertRaisesRegex(ProjectModelError, "bit: 1 also belongs to 'feature'"):
            ProjectModel.load(self.root)

    def test_mod_build_flags_are_globally_unique(self):
        self.project(requirement="optional")
        self.write("mods/shim/mod.yaml", "name: shim\nenv_flags: [WITH_FEATURE]\n")
        self.write("mods/feature/mod.yaml", "name: feature\nenv_flags: [WITH_FEATURE]\n")
        with self.assertRaisesRegex(ProjectModelError, "env_flags: 'WITH_FEATURE' also belongs"):
            ProjectModel.load(self.root)


if __name__ == "__main__":
    unittest.main(verbosity=2)
