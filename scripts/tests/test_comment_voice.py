"""Enforce docs/conventions.md: no first or second person in comments.

The convention scopes itself to code comments, workflow comments, operator-facing strings
and commit messages. Only comments are mechanically checkable here, so that is what this
covers -- README prose is outside the rule's stated scope and outside this test's.

The rule exists because a comment that says "we apply the patch first" names no actor and
stops being true the moment someone else reads it. Naming the actor, or the passive voice,
survives.
"""

import re
import subprocess
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent.parent

BANNED = re.compile(r"\b(I|me|my|mine|we|us|our|ours|you|your|yours|human|humans)\b")

# Prefixes that actually start a comment. C preprocessor directives also begin with '#',
# so hash comments are only recognised in file types that have no preprocessor.
HASH_TYPES = {".py", ".yml", ".yaml"}
SLASH_TYPES = {".cpp", ".h", ".js"}
CHECKED = HASH_TYPES | SLASH_TYPES

# Only the trees the convention governs. Vendored and upstream-derived text keeps its own
# voice: auto_cli_commands.md is upstream's file with this repo's additions appended.
SCOPE = ("mods/", "scripts/", ".github/")


def comment_text(line, suffix):
    stripped = line.strip()
    if suffix in HASH_TYPES and stripped.startswith("#"):
        return stripped
    if suffix in SLASH_TYPES and (stripped.startswith("//") or stripped.startswith("*")):
        return stripped
    # A trailing comment after code.
    if suffix in SLASH_TYPES and "//" in line:
        return line.split("//", 1)[1]
    if suffix in HASH_TYPES and "#" in line and not stripped.startswith("#"):
        return line.split("#", 1)[1]
    return ""


def tracked_files():
    out = subprocess.run(["git", "ls-files"], cwd=REPO_ROOT,
                         capture_output=True, text=True, check=True).stdout
    return [f for f in out.split("\n") if f.startswith(SCOPE) and Path(f).suffix in CHECKED]


class CommentVoiceTestCase(unittest.TestCase):
    def test_no_first_or_second_person_in_comments(self):
        violations = []
        for name in tracked_files():
            path = REPO_ROOT / name
            for number, line in enumerate(path.read_text(errors="replace").splitlines(), 1):
                text = comment_text(line, path.suffix)
                if not text:
                    continue
                found = BANNED.search(text)
                if found:
                    violations.append(f"{name}:{number}: [{found.group(0)}] {line.strip()[:80]}")
        self.assertEqual(violations, [], "docs/conventions.md forbids first or second person "
                                         "in comments:\n" + "\n".join(violations))


if __name__ == "__main__":
    unittest.main()
