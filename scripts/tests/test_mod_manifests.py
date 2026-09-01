"""Every mod's declarations point at something that exists.

Nothing checked this before: power-guard required hotspot-ota/0002 for a while after
that patch was folded away, and the stack still applied because `requires` is only read
for ordering.
"""
import sys
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO_ROOT / "scripts"))

from mobmesh_tools.model import ProjectModel


class ModManifestTestCase(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.model = ProjectModel.load(REPO_ROOT)

    def test_every_mod_is_in_the_typed_model(self):
        discovered = {path.parent.name for path in (REPO_ROOT / "mods").glob("*/mod.yaml")}
        self.assertEqual(discovered, set(self.model.mods))

    def test_project_has_patch_sidecars(self):
        self.assertTrue(any(mod.patches for mod in self.model.mods.values()))

    def test_every_target_has_valid_patch_order(self):
        for target in self.model.targets:
            self.model.validate_mod_selection(self.model.resolved_mod_names(target))

    def test_mod_source_never_collides(self):
        """Two mods shipping the same path would silently race; copy-src refuses it at build."""
        seen = {}
        for mod in self.model.ordered_mods():
            root = REPO_ROOT / "mods" / mod / "files"
            for path in root.rglob("*"):
                if not path.is_file():
                    continue
                rel = path.relative_to(root)
                self.assertNotIn(rel, seen, f"{rel} shipped by both {seen.get(rel)} and {mod}")
                seen[rel] = mod


if __name__ == "__main__":
    unittest.main(verbosity=2)
