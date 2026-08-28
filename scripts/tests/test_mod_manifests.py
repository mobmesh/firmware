"""Every mod's declarations point at something that exists.

Nothing checked this before: power-guard required hotspot-ota/0002 for a while after
that patch was folded away, and the stack still applied because `requires` is only read
for ordering.
"""
import glob
import os
import unittest

import yaml

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def mods():
    return sorted(os.path.basename(p) for p in glob.glob(os.path.join(REPO_ROOT, "mods", "*"))
                  if os.path.isdir(p))


def sidecars(mod):
    out = {}
    for path in sorted(glob.glob(os.path.join(REPO_ROOT, "mods", mod, "patches", "*.meta.yaml"))):
        with open(path) as handle:
            out[os.path.basename(path).split(".")[0]] = yaml.safe_load(handle) or {}
    return out


class ModManifestTestCase(unittest.TestCase):
    def test_every_requires_target_exists(self):
        known = {f"{mod}/{pid}" for mod in mods() for pid in sidecars(mod)}
        self.assertTrue(known, "no sidecars found at all")
        for mod in mods():
            for pid, meta in sidecars(mod).items():
                for dep in meta.get("requires") or []:
                    self.assertIn(dep, known, f"{mod}/{pid} requires {dep}, which does not exist")

    def test_every_patch_has_a_sidecar_and_the_reverse(self):
        for mod in mods():
            patches = {os.path.basename(p).split("_")[0]
                       for p in glob.glob(os.path.join(REPO_ROOT, "mods", mod, "patches", "*.patch"))}
            self.assertEqual(patches, set(sidecars(mod)),
                             f"{mod}: patches and sidecars disagree")

    def test_env_flags_are_claimed_once(self):
        owner = {}
        for mod in mods():
            path = os.path.join(REPO_ROOT, "mods", mod, "mod.yaml")
            declared = list((yaml.safe_load(open(path)) or {}).get("env_flags") or []) if os.path.exists(path) else []
            for meta in sidecars(mod).values():
                if meta.get("env_flag"):
                    declared.append(meta["env_flag"])
            for flag in declared:
                self.assertNotIn(flag, owner, f"{flag} claimed by both {owner.get(flag)} and {mod}")
                owner[flag] = mod

    def test_mod_source_never_collides(self):
        """Two mods shipping the same path would silently race; copy-src refuses it at build."""
        seen = {}
        for mod in mods():
            root = os.path.join(REPO_ROOT, "mods", mod, "files")
            for path in glob.glob(os.path.join(root, "**", "*"), recursive=True):
                if not os.path.isfile(path):
                    continue
                rel = os.path.relpath(path, root)
                self.assertNotIn(rel, seen, f"{rel} shipped by both {seen.get(rel)} and {mod}")
                seen[rel] = mod


if __name__ == "__main__":
    unittest.main(verbosity=2)
