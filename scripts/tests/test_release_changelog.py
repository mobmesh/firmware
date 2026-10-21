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

    def write_index(self, rows, subject):
        doc = "## Command index\n\n| Command | Purpose |\n| --- | --- |\n" + "".join(
            f"| [`{c}`](#x) | {p} |\n" for c, p in rows)
        target = self.repo / "mods/m/docs/cli-additions.md"
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(doc)
        self.git("add", "mods/m/docs/cli-additions.md")
        self.git("commit", "-q", "-m", subject)
        return self.git("rev-parse", "HEAD")

    def test_lists_only_firmware_feat_subjects(self):
        self.commit("mods/a.txt", "fix: firmware fix")
        self.commit("pages/p.txt", "feat: page only")
        self.commit("mods/b.txt", "docs: mod docs")
        head = self.commit("variants/v.txt", "feat: board change")
        body = self.compose(f"<!-- mobmesh-build:{self.base} -->", head)
        self.assertIn(f"#### Build `{head[:7]}`, 2026-10-16\n\n- feat: board change\n", body)
        self.assertNotIn("firmware fix", body)
        self.assertNotIn("page only", body)
        self.assertNotIn("mod docs", body)
        self.assertTrue(body.rstrip().endswith(f"<!-- mobmesh-build:{head} -->"))

    def test_reports_new_changed_and_removed_commands(self):
        first = self.write_index([("get a", "Read a"), ("set b <x>", "Save b"), ("old c", "Do c")], "docs: index")
        head = self.write_index([("get a", "Read a"), ("set b <x>", "Save b, now persistent"), ("new d \\| e", "Do d")],
                                "docs: change index")
        body = self.compose(f"<!-- mobmesh-build:{first} -->", head)
        self.assertIn("- cli: new 'new d | e'", body)
        self.assertIn("- cli: changed 'set b <x>'", body)
        self.assertIn("- cli: removed 'old c'", body)
        self.assertNotIn("'get a'", body)

    def test_rebuild_prepends_and_keeps_earlier_entries(self):
        first = self.commit("mods/a.txt", "feat: one")
        body = self.compose(f"<!-- mobmesh-build:{self.base} -->", first)
        second = self.commit("mods/a.txt", "feat: two")
        body = self.compose(body, second)
        self.assertLess(body.index("Build `" + second[:7]), body.index("Build `" + first[:7]))
        self.assertEqual(body.count("## MeshCore Enhanced+ Changes"), 1)
        self.assertEqual(body.count("## Upstream release notes"), 1)
        self.assertEqual(body.count("<!-- mobmesh-build:"), 1)

    def test_same_commit_adds_no_entry(self):
        head = self.commit("mods/a.txt", "feat: one")
        body = self.compose(f"<!-- mobmesh-build:{self.base} -->", head)
        self.assertEqual(self.compose(body, head), body)

    def test_new_release_starts_from_previous_release_marker(self):
        head = self.commit("mods/a.txt", "feat: since last release")
        body = self.compose("", head, previous=f"<!-- mobmesh-build:{self.base} -->")
        self.assertIn("- feat: since last release", body)

    def test_no_marker_anywhere_adds_no_section(self):
        head = self.commit("mods/a.txt", "feat: unseen")
        body = self.compose("", head)
        self.assertNotIn("MeshCore Enhanced+ Changes", body)
        self.assertTrue(body.rstrip().endswith(f"<!-- mobmesh-build:{head} -->"))

    def test_rebuild_with_nothing_new_adds_no_entry(self):
        first = self.commit("mods/a.txt", "feat: one")
        body = self.compose(f"<!-- mobmesh-build:{self.base} -->", first)
        head = self.commit("mods/a.txt", "fix: just a fix")
        rebuilt = self.compose(body, head)
        self.assertEqual(rebuilt.count("#### Build"), 1)
        self.assertNotIn("just a fix", rebuilt)
        self.assertTrue(rebuilt.rstrip().endswith(f"<!-- mobmesh-build:{head} -->"))

    def test_hand_edited_body_with_repeated_heading_keeps_its_list(self):
        edited = ("Built from x.\n\n## MeshCore Enhanced+ Changes\n\n## MeshCore Enhanced+ Changes\n\n"
                  "- feat: kept by hand\n\n\n## Upstream release notes\n\nNotes.\n\n## Upstream release notes\n\n"
                  f"Notes.\n\n<!-- mobmesh-build:{self.base} -->")
        head = self.commit("mods/a.txt", "fix: nothing to list")
        body = self.compose(edited, head)
        self.assertIn("- feat: kept by hand", body)
        self.assertEqual(body.count("## MeshCore Enhanced+ Changes"), 1)
        self.assertEqual(body.count("## Upstream release notes"), 1)

    def test_entries_use_level_four_headings(self):
        head = self.commit("mods/a.txt", "feat: one")
        body = self.compose(f"<!-- mobmesh-build:{self.base} -->", head)
        self.assertIn(f"\n#### Build `{head[:7]}`", body)
        self.assertNotIn("\n### ", body)


if __name__ == "__main__":
    unittest.main()
