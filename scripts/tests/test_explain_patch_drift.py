#!/usr/bin/env python3
"""Unit tests for explain_patch_drift.py.

Each case builds a throwaway git repo, so the three drift shapes are provoked rather
than described: inserted-between, moved-to-another-file, and deleted outright.

Run: python3 scripts/tests/test_explain_patch_drift.py
"""
import importlib.util
import subprocess
import tempfile
import unittest
from pathlib import Path

SCRIPT = Path(__file__).resolve().parent.parent / "explain_patch_drift.py"
spec = importlib.util.spec_from_file_location("explain_patch_drift", SCRIPT)
epd = importlib.util.module_from_spec(spec)
spec.loader.exec_module(epd)

ORIGINAL = """int main() {
  int dutycycle_value_here = 1;
  another_long_context_line();
  return dutycycle_value_here;
}
"""

PATCH = """diff --git a/app.c b/app.c
--- a/app.c
+++ b/app.c
@@ -1,3 +1,4 @@
 int main() {
+  inserted_by_the_mod();
   int dutycycle_value_here = 1;
   another_long_context_line();
"""


def run_git(tree, *args):
    subprocess.run(["git", "-C", str(tree), *args], check=True,
                   capture_output=True, text=True)


class DriftCase(unittest.TestCase):
    def build(self, after_text, extra_file=None):
        """A repo with ORIGINAL at tag `base`, then after_text on top."""
        self.tmp = tempfile.TemporaryDirectory()
        tree = Path(self.tmp.name) / "repo"
        tree.mkdir()
        run_git(tree, "init", "-q", "-b", "main")
        run_git(tree, "config", "user.email", "t@example.com")
        run_git(tree, "config", "user.name", "t")
        (tree / "app.c").write_text(ORIGINAL)
        run_git(tree, "add", "-A")
        run_git(tree, "commit", "-qm", "base")
        run_git(tree, "tag", "base")
        (tree / "app.c").write_text(after_text)
        if extra_file:
            (tree / extra_file[0]).write_text(extra_file[1])
        run_git(tree, "add", "-A")
        # --allow-empty: the unchanged-tree cases still want a second commit to exist
        run_git(tree, "commit", "-q", "--allow-empty", "-m", "upstream moved the radio CLI out")
        self.patch = Path(self.tmp.name) / "p.patch"
        self.patch.write_text(PATCH)
        return tree

    def explain(self, tree):
        hunk = epd.parse_hunks(self.patch)[0]
        return "\n".join(epd.explain_hunk(tree, "HEAD", "base", hunk))

    def tearDown(self):
        if hasattr(self, "tmp"):
            self.tmp.cleanup()


class TestParse(unittest.TestCase):
    def test_diff_headers_are_not_context(self):
        """`--- a/path` starts with the same char a removal line does."""
        p = Path(tempfile.mkdtemp()) / "p.patch"
        p.write_text(PATCH)
        ctx = epd.parse_hunks(p)[0]["ctx"]
        self.assertTrue(ctx, "expected some context lines")
        for line in ctx:
            self.assertFalse(line.startswith(("a/", "b/", "-- ")), f"header leaked: {line}")

    def test_short_lines_are_not_anchors(self):
        """A line like `}` locates nothing and must not be used as an anchor."""
        p = Path(tempfile.mkdtemp()) / "p.patch"
        p.write_text(PATCH)
        for line in epd.parse_hunks(p)[0]["ctx"]:
            self.assertGreaterEqual(len(line), epd.MIN_ANCHOR_LEN)


class TestShapes(DriftCase):
    def test_inserted_between_anchors(self):
        after = ORIGINAL.replace("int main() {", "int main() {\n  upstream_added_this_line();")
        out = self.explain(self.build(after))
        self.assertIn("inserted between the anchors", out)

    def test_code_moved_to_another_file(self):
        after = ORIGINAL.replace("  int dutycycle_value_here = 1;\n", "")
        moved = ("radio.c", "void f() {\n  int dutycycle_value_here = 1;\n}\n")
        out = self.explain(self.build(after, extra_file=moved))
        self.assertIn("the code moved", out)
        self.assertIn("radio.c", out)

    def test_deleted_outright_names_the_commit(self):
        after = ORIGINAL.replace("  int dutycycle_value_here = 1;\n", "")
        out = self.explain(self.build(after))
        self.assertIn("not found anywhere in the tree", out)
        self.assertIn("upstream moved the radio CLI out", out)

    def test_missing_file_is_reported(self):
        tree = self.build(ORIGINAL)
        run_git(tree, "rm", "-q", "app.c")
        run_git(tree, "commit", "-qm", "drop it")
        self.assertIn("is gone from", self.explain(tree))


class TestHunkApplies(DriftCase):
    def test_clean_hunk_is_silent(self):
        """A hunk that still applies must not be explained -- that was the first bug."""
        tree = self.build(ORIGINAL)
        self.assertTrue(epd.hunk_applies(tree, epd.parse_hunks(self.patch)[0]))

    def test_drifted_hunk_is_caught(self):
        after = ORIGINAL.replace("  another_long_context_line();\n", "")
        tree = self.build(after)
        self.assertFalse(epd.hunk_applies(tree, epd.parse_hunks(self.patch)[0]))


if __name__ == "__main__":
    unittest.main()
