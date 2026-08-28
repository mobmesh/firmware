"""Only mods/shim may add hook call sites to upstream files.

File-level ownership stopped expressing that when the CLI hook moved into MyMesh.cpp,
which timing-safety also patches for unrelated reasons. The rule is about regions, so the
check is too: a non-shim patch may edit a shared upstream file, but may not be the thing
that introduces a call into the mod hook surface.
"""
import glob
import os
import re
import unittest

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

# Upstream files only shim may patch at all -- nothing else has business in an entry point.
SHIM_OWNED = {
    "examples/simple_repeater/main.cpp",
    "examples/simple_room_server/main.cpp",
}

# Files shim shares with another mod, where ownership is per region rather than per file.
SHIM_SHARED = {
    "examples/simple_repeater/MyMesh.cpp",
    "examples/simple_room_server/MyMesh.cpp",
}

# Files the mods ship themselves; a hook call in one of these is expected, not a trespass.
MOD_OWNED_PREFIXES = ("src/helpers/ModHooks.", "src/helpers/esp32/CommonCliMods.cpp")

DIFF_RE = re.compile(r"^diff --git a/(\S+) b/", re.MULTILINE)
# The hook surface: every function ModHooks.h declares is modXxx().
HOOK_CALL_RE = re.compile(r"\bmod[A-Z][A-Za-z0-9_]*\s*\(")


def patched_files(patch_path: str) -> set:
    with open(patch_path, encoding="utf-8") as handle:
        return set(DIFF_RE.findall(handle.read()))


def added_hook_calls(patch_path: str) -> dict:
    """Added lines that call a mod hook, keyed by the file they land in."""
    found, current = {}, None
    with open(patch_path, encoding="utf-8") as handle:
        for line in handle:
            if line.startswith("+++ "):
                current = line[6:].strip()
            elif line.startswith("+") and not line.startswith("+++") and current:
                if current.startswith(MOD_OWNED_PREFIXES):
                    continue
                if HOOK_CALL_RE.search(line):
                    found.setdefault(current, []).append(line[1:].strip())
    return found


class PatchOwnershipTestCase(unittest.TestCase):
    def test_only_shim_touches_the_entry_points(self):
        for patch in sorted(glob.glob(os.path.join(REPO_ROOT, "mods", "*", "patches", "*.patch"))):
            mod = os.path.basename(os.path.dirname(os.path.dirname(patch)))
            if mod == "shim":
                continue
            trespass = patched_files(patch) & SHIM_OWNED
            self.assertEqual(
                trespass, set(),
                f"{mod}/{os.path.basename(patch)} patches shim-owned entry point(s) "
                f"{sorted(trespass)}. Add the hook to mods/shim instead, then call it from here.",
            )

    def test_only_shim_adds_hook_calls_to_upstream(self):
        """The region rule: a shared file is fine, adding the hook call there is not."""
        for patch in sorted(glob.glob(os.path.join(REPO_ROOT, "mods", "*", "patches", "*.patch"))):
            mod = os.path.basename(os.path.dirname(os.path.dirname(patch)))
            if mod == "shim":
                continue
            calls = added_hook_calls(patch)
            self.assertEqual(
                calls, {},
                f"{mod}/{os.path.basename(patch)} adds a mod hook call to an upstream file: "
                f"{ {k: v for k, v in calls.items()} }. Only shim places call sites; add the "
                f"hook to mods/shim and let it dispatch.",
            )

    def test_shim_actually_owns_them(self):
        """Without this, a dropped hunk would leave the sets above asserting nothing."""
        owned = set()
        for patch in glob.glob(os.path.join(REPO_ROOT, "mods", "shim", "patches", "*.patch")):
            owned |= patched_files(patch)
        self.assertEqual(
            (SHIM_OWNED | SHIM_SHARED) - owned, set(),
            "shim no longer patches every file it is supposed to own; update SHIM_OWNED / "
            "SHIM_SHARED or restore the hunk.",
        )

    def test_shim_is_the_one_placing_the_call_sites(self):
        """The mirror of the region rule: shim must still be adding them."""
        placed = {}
        for patch in glob.glob(os.path.join(REPO_ROOT, "mods", "shim", "patches", "*.patch")):
            placed.update(added_hook_calls(patch))
        for f in SHIM_SHARED:
            self.assertIn(
                f, placed,
                f"shim adds no hook call to {f}; the dispatch was dropped or moved.",
            )


if __name__ == "__main__":
    unittest.main()
