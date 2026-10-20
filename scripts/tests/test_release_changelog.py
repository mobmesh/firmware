"""Release body composition: firmware-only fix/feat subjects, entries kept across rebuilds."""
import importlib.util
import os
import subprocess
import tempfile
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("release_changelog", REPO_ROOT / "scripts/release_changelog.py")
changelog = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(changelog)

NOTES = "Built from `repeater-v9`.\n\n## Upstream release notes\n\nUpstream text.\n"


class ReleaseChangelogTestCase(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.repo = Path(self.temp.name)
        self.git("init", "-q")
        self.git("config", "user.email", "t@example.invalid")
        self.git("config", "user.name", "t")
        self.base = self.commit("mods/a.txt", "chore: start")

    def tearDown(self):
        self.temp.cleanup()

    def git(self, *args):
        return subprocess.run(["git", "-C", str(self.repo), *args], capture_output=True,
                              text=True, check=True).stdout.strip()

    def commit(self, path, subject):
        target = self.repo / path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(target.read_text() + "x" if target.exists() else "x")
        self.git("add", path)
        self.git("commit", "-q", "-m", subject)
        return self.git("rev-parse", "HEAD")

    def compose(self, existing, head, previous=""):
        return changelog.compose(NOTES, existing, previous, head, self.repo, "2026-10-16")

    def test_lists_only_firmware_fix_and_feat_subjects(self):
        self.commit("mods/a.txt", "fix: firmware fix")
        self.commit("pages/p.txt", "feat: page only")
        self.commit("mods/b.txt", "docs: mod docs")
        head = self.commit("variants/v.txt", "feat: board change")
        body = self.compose(f"<!-- mobmesh-build:{self.base} -->", head)
        self.assertIn("- feat: board change\n- fix: firmware fix", body)
        self.assertNotIn("page only", body)
        self.assertNotIn("mod docs", body)
        self.assertTrue(body.rstrip().endswith(f"<!-- mobmesh-build:{head} -->"))

    def test_rebuild_prepends_and_keeps_earlier_entries(self):
        first = self.commit("mods/a.txt", "fix: one")
        body = self.compose(f"<!-- mobmesh-build:{self.base} -->", first)
        second = self.commit("mods/a.txt", "fix: two")
        body = self.compose(body, second)
        self.assertLess(body.index("Rebuild `" + second[:7]), body.index("Build `" + first[:7]))
        self.assertEqual(body.count("## MeshCore Enhanced+ Changes"), 1)
        self.assertEqual(body.count("## Upstream release notes"), 1)
        self.assertEqual(body.count("<!-- mobmesh-build:"), 1)

    def test_entries_use_level_four_headings(self):
        head = self.commit("mods/a.txt", "fix: one")
        body = self.compose(f"<!-- mobmesh-build:{self.base} -->", head)
        self.assertIn(f"\n#### Build `{head[:7]}`", body)
        self.assertNotIn("\n### ", body)

    def test_same_commit_adds_no_entry(self):
        head = self.commit("mods/a.txt", "fix: one")
        body = self.compose(f"<!-- mobmesh-build:{self.base} -->", head)
        self.assertEqual(self.compose(body, head), body)

    def test_new_release_starts_from_previous_release_marker(self):
        head = self.commit("mods/a.txt", "fix: since last release")
        body = self.compose("", head, previous=f"<!-- mobmesh-build:{self.base} -->")
        self.assertIn("- fix: since last release", body)

    def test_no_marker_anywhere_records_a_first_build(self):
        head = self.commit("mods/a.txt", "fix: unseen")
        body = self.compose("", head)
        self.assertIn("First MeshCore Enhanced+ build", body)
        self.assertNotIn("unseen", body)

    def test_rebuild_without_firmware_changes_says_so(self):
        head = self.commit("pages/p.txt", "feat: page only")
        body = self.compose(f"<!-- mobmesh-build:{self.base} -->", head)
        self.assertIn("- No firmware changes.", body)


if __name__ == "__main__":
    unittest.main()
