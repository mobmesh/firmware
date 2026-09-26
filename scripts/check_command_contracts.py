#!/usr/bin/env python3
"""Check that the upstream CLI commands the mods send or intercept still exist on a ref."""

import argparse
import re
import subprocess
import sys
from collections import defaultdict
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
SEARCH_PATHS = ["src", "examples/simple_repeater", "examples/simple_room_server", "variants"]
# The names upstream parses a command line through; anything else compares values, not verbs.
PARSE_VARS = r"(?:command|config|cmd|cli_command)"
COMPARE_RE = re.compile(r"\b(?:memcmp|strcmp|strncmp)\(\s*" + PARSE_VARS + r"\s*,\s*\"([^\"]*)\"")
# Strings first, so a `//` inside a URL literal is not read as a comment.
TOKEN_RE = re.compile(r"\"(?:[^\"\\\n]|\\.)*\"|'(?:[^'\\\n]|\\.)*'|//[^\n]*|/\*.*?\*/", re.DOTALL)
COMMAND_RE = re.compile(r"^[a-z][a-z0-9.\-]*(?: [a-z][a-z0-9.\-]*){0,2}$")
MAX_HINTS = 5

# Reply shapes and constants the mods parse or mirror: (label, fixed text, mods relying on it).
REPLY_CONTRACTS = [
    ("`get` replies start with `> `", '"> ', "try-settings"),
    ("`tempradio` acknowledges with `OK - temp params`", '"OK - temp params', "try-settings, sync-settings"),
    ("`set radio` acknowledges with `OK - reboot to apply`", '"OK - reboot to apply"', "try-settings, sync-settings"),
    ("default clock epoch 1715770351", "1715770351", "power-guard, sync-settings"),
]


def git(tree, *args):
    return subprocess.run(["git", "-C", str(tree), *args], capture_output=True, text=True).stdout


def key_of(text):
    """The command text with any `get `/`set ` verb and trailing argument separator removed."""
    text = text.split("%", 1)[0].strip()
    for verb in ("get ", "set "):
        if text.startswith(verb):
            return text[len(verb):].strip()
    return text


def mod_candidates(mods_dir):
    """Command-shaped literals in mod source, mapped to the mods using them."""
    found = defaultdict(set)
    for path in sorted(Path(mods_dir).glob("*/files/**/*")):
        if path.suffix not in (".cpp", ".h") or not path.is_file():
            continue
        mod = path.relative_to(mods_dir).parts[0]
        for token in TOKEN_RE.findall(path.read_text(errors="replace")):
            if not token.startswith('"'):
                continue
            key = key_of(token[1:-1])
            if COMMAND_RE.match(key) and key not in ("get", "set"):
                found[key].add(mod)
    return found


def upstream_sites(tree, commit):
    """Every command upstream parses at `commit`, mapped to its (file, line) sites."""
    sites = defaultdict(list)
    out = git(tree, "grep", "-nE", r"(memcmp|strcmp|strncmp)\(", commit, "--", *SEARCH_PATHS)
    for row in out.splitlines():
        _, path, line, text = row.split(":", 3)
        for literal in COMPARE_RE.findall(text):
            key = key_of(literal)
            if key:
                sites[key].append((path, int(line)))
    return sites


def hints(tree, commit, key):
    """Where a missing command's last word still appears, as a lead for a person."""
    word = re.escape(key.split(".")[-1].split(" ")[-1])
    out = git(tree, "grep", "-nE", r"\"[^\"]*" + word + r"[^\"]*\"", commit, "--", *SEARCH_PATHS)
    rows = [r.split(":", 3) for r in out.splitlines()]
    return [f"{p}:{n}" for _, p, n, _ in rows][:MAX_HINTS]


def board_of(path):
    parts = path.split("/")
    return parts[1] if parts[0] == "variants" and len(parts) > 1 else None


def check(tree, base, head, boards, mods_dir):
    """(problems, moved, ok_count, reply_problems) for `head` against the contracts at `base`."""
    wanted = mod_candidates(mods_dir)
    base_sites = upstream_sites(tree, base)
    head_sites = upstream_sites(tree, head)
    problems, moved, ok = [], [], 0
    for key in sorted(k for k in wanted if k in base_sites):
        users = ", ".join(sorted(wanted[key]))
        sites = head_sites.get(key, [])
        ours = [s for s in sites if board_of(s[0]) in (None, *boards)]
        if not sites:
            problems.append((key, users, "no longer parsed anywhere", hints(tree, head, key)))
        elif not ours:
            where = sorted({f"{p}:{n}" for p, n in sites})[:MAX_HINTS]
            problems.append((key, users, f"only handled by boards we do not build ({', '.join(boards)})", where))
        else:
            ok += 1
            before = {p for p, _ in base_sites[key]}
            after = {p for p, _ in ours}
            if before != after:
                moved.append((key, users, sorted(before), sorted(after)))
    reply_problems = []
    for label, text, users in REPLY_CONTRACTS:
        if not git(tree, "grep", "-lF", text, head, "--", *SEARCH_PATHS).strip():
            reply_problems.append((label, users))
    return problems, moved, ok, reply_problems


def report(ref, problems, moved, ok, reply_problems):
    lines = []
    if not problems and not reply_problems:
        lines.append(f"- `{ref}`: all {ok} upstream commands the mods rely on are still handled")
    for key, users, why, where in problems:
        lines.append(f"- `{ref}`: command `{key}` ({users}) **{why}**")
        if where:
            lines.append(f"  - look at: {', '.join(f'`{w}`' for w in where)}")
    for label, users in reply_problems:
        lines.append(f"- `{ref}`: reply contract **gone**: {label} ({users})")
    if moved:
        lines.append(f"<details><summary><code>{ref}</code> -- {len(moved)} command(s) moved but still handled</summary>")
        lines.append("")
        for key, users, before, after in moved:
            lines.append(f"- `{key}` ({users}): {', '.join(before)} → {', '.join(after)}")
        lines.append("")
        lines.append("</details>")
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tree", required=True, help="upstream clone holding both commits")
    parser.add_argument("--base", required=True, help="the release the contracts were taken from")
    parser.add_argument("--head", default="HEAD", help="the commit to check")
    parser.add_argument("--ref", default="HEAD", help="name shown in the report")
    parser.add_argument("--boards", required=True, help="comma-separated variant directories we build")
    parser.add_argument("--mods-dir", default=str(REPO_ROOT / "mods"))
    args = parser.parse_args()
    boards = [b for b in args.boards.split(",") if b]
    problems, moved, ok, reply_problems = check(args.tree, args.base, args.head, boards, args.mods_dir)
    print(report(args.ref, problems, moved, ok, reply_problems))
    return 2 if problems or reply_problems else 0


if __name__ == "__main__":
    sys.exit(main())
