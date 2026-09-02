"""Enforce docs/conventions.md: no first or second person in comments.

The convention scopes itself to code comments, workflow comments, operator-facing strings
and commit messages. Comments and Python docstrings are what is mechanically checkable, so
that is the scope here; README prose sits outside the rule and outside this test.

A comment written in the first person names no actor, and stops being true as soon as
someone else reads it. Naming the actor, or the passive voice, survives.
"""

import ast
import re
import subprocess
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent.parent

# Case-insensitive, so a pronoun opening a sentence is caught too. The first-person
# singular stays case-sensitive: a lowercase standalone letter is nearly always an index.
BANNED = re.compile(r"\b(me|my|mine|we|us|our|ours|you|your|yours|human|humans)\b", re.I)
BANNED_FIRST_PERSON = re.compile(r"\bI\b")


def offending_word(text):
    found = BANNED.search(text) or BANNED_FIRST_PERSON.search(text)
    return found.group(0) if found else None

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


def docstrings(path):
    """Module, class and function docstrings, with the line each one starts on."""
    try:
        tree = ast.parse(path.read_text(errors="replace"))
    except SyntaxError:
        return []
    found = []
    for node in ast.walk(tree):
        if not isinstance(node, (ast.Module, ast.ClassDef, ast.FunctionDef, ast.AsyncFunctionDef)):
            continue
        text = ast.get_docstring(node, clean=False)
        if text:
            found.append((getattr(node, "lineno", 1), text))
    return found


def tracked_files():
    out = subprocess.run(["git", "ls-files"], cwd=REPO_ROOT,
                         capture_output=True, text=True, check=True).stdout
    return [f for f in out.split("\n") if f.startswith(SCOPE) and Path(f).suffix in CHECKED]


class CommentVoiceTestCase(unittest.TestCase):
    def test_no_first_or_second_person_in_comments(self):
        violations = []
        for name in tracked_files():
            path = REPO_ROOT / name
            if path.suffix == ".py":
                for start, text in docstrings(path):
                    for offset, line in enumerate(text.splitlines()):
                        word = offending_word(line)
                        if word:
                            violations.append(
                                f"{name}:{start + offset}: [{word}] {line.strip()[:80]}")
            for number, line in enumerate(path.read_text(errors="replace").splitlines(), 1):
                text = comment_text(line, path.suffix)
                if not text:
                    continue
                word = offending_word(text)
                if word:
                    violations.append(f"{name}:{number}: [{word}] {line.strip()[:80]}")
        self.assertEqual(violations, [], "docs/conventions.md forbids first or second person "
                                         "in comments:\n" + "\n".join(violations))


if __name__ == "__main__":
    unittest.main()
