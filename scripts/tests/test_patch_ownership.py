"""Only mods/shim may patch the upstream insertion points."""
import glob
import os
import re
import unittest

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

# Upstream files that are insertion points: only shim may patch them.
SHIM_OWNED = {
    "examples/simple_repeater/main.cpp",
    "examples/simple_room_server/main.cpp",
    "src/helpers/CommonCLI.cpp",
    "src/helpers/CommonCLI.h",
}

DIFF_RE = re.compile(r"^diff --git a/(\S+) b/", re.MULTILINE)


def patched_files(patch_path: str) -> set:
    with open(patch_path, encoding="utf-8") as handle:
        return set(DIFF_RE.findall(handle.read()))


class PatchOwnershipTestCase(unittest.TestCase):
    def test_only_shim_touches_the_insertion_points(self):
        for patch in sorted(glob.glob(os.path.join(REPO_ROOT, "mods", "*", "patches", "*.patch"))):
            mod = os.path.basename(os.path.dirname(os.path.dirname(patch)))
            if mod == "shim":
                continue
            trespass = patched_files(patch) & SHIM_OWNED
            self.assertEqual(
                trespass, set(),
                f"{mod}/{os.path.basename(patch)} patches shim-owned insertion point(s) "
                f"{sorted(trespass)}. Add the hook to mods/shim instead, then call it from here.",
            )

    def test_shim_actually_owns_them(self):
        """Without this, a dropped hunk would leave the set above asserting nothing."""
        owned = set()
        for patch in glob.glob(os.path.join(REPO_ROOT, "mods", "shim", "patches", "*.patch")):
            owned |= patched_files(patch)
        self.assertEqual(
            SHIM_OWNED - owned, set(),
            "shim no longer patches every file it is supposed to own; update SHIM_OWNED "
            "or restore the hunk.",
        )


if __name__ == "__main__":
    unittest.main()
