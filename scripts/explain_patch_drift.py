#!/usr/bin/env python3
"""Explain why a patch stopped applying, in terms of where the code went.

`git apply` reports that a hunk failed and stops there. Every failure is one of three
shapes, and each wants a different fix: upstream inserted between the anchors, the code
moved to another file, or it was deleted outright. All three are answerable from the
target tree plus its history, so this asks rather than leaving it to a person.

Emits markdown for the canary's issue body. Findings are hints, not verdicts: context is
matched on exact stripped text, so reindentation or a renamed variable reads as a
deletion.
"""

import argparse
import re
import subprocess
import sys
from pathlib import Path

# A context line short enough to appear all over the tree ("}", "else {") locates nothing.
MIN_ANCHOR_LEN = 12
MAX_HUNKS_REPORTED = 6
MAX_MISSING_REPORTED = 3


def git(tree, *args):
    r = subprocess.run(["git", "-C", str(tree), *args], capture_output=True, text=True)
    return r.stdout


def parse_hunks(patch_path):
    """Hunks as {file, line, ctx, raw}, raw being a standalone one-hunk patch.

    The `---`/`+++` headers start with the same characters a hunk body uses, so they are
    matched before the body and skipped; a first pass at this read one as context.
    """
    hunks, header, cur = [], [], None
    for line in Path(patch_path).read_text(errors="replace").splitlines():
        if line.startswith("diff --git"):
            header = [line]
            continue
        if line.startswith(("index ", "--- ", "+++ ", "new file", "deleted file", "similarity")):
            header.append(line)
            continue
        m = re.match(r"@@ -(\d+)", line)
        if m:
            if cur:
                hunks.append(cur)
            target = next((h[6:].strip() for h in header if h.startswith("+++ ")), None)
            cur = {"file": target, "line": int(m.group(1)), "ctx": [],
                   "raw": [h for h in header if not h.startswith("index ")] + [line]}
            continue
        if cur is None:
            continue
        cur["raw"].append(line)
        if line[:1] in (" ", "-"):
            s = line[1:].strip()
            if len(s) >= MIN_ANCHOR_LEN:
                cur["ctx"].append(s)
    if cur:
        hunks.append(cur)
    return hunks


def hunk_applies(tree, hunk):
    """Each hunk checked on its own -- git apply names only the first failure per file."""
    patch = "\n".join(hunk["raw"]) + "\n"
    r = subprocess.run(["git", "-C", str(tree), "apply", "--check", "-"],
                       input=patch, capture_output=True, text=True)
    return r.returncode == 0


def failing_files(tree, patch_path):
    """Files git apply names in `patch failed: <file>:<line>`; empty means every file."""
    r = subprocess.run(["git", "-C", str(tree), "apply", "--check", str(patch_path)],
                       capture_output=True, text=True)
    return {m.group(1) for m in re.finditer(r"patch failed: (\S+):\d+", r.stderr)}


def locate(body, ctx):
    return [i + 1 for i, l in enumerate(body) if l.strip() == ctx]


def explain_hunk(tree, ref, base, hunk):
    out = []
    body = git(tree, "show", f"{ref}:{hunk['file']}").splitlines()
    if not body:
        return [f"    - `{hunk['file']}` is gone from `{ref}`"]

    found = {c: locate(body, c) for c in hunk["ctx"]}
    present = [c for c, at in found.items() if at]
    missing = [c for c, at in found.items() if not at]

    if present:
        first = present[0]
        out.append(f"    - anchor `{first[:58]}` still here, now line {found[first][0]}")

    if not missing:
        lines = sorted(at[0] for at in found.values() if at)
        out.append(f"    - all context present, spanning {lines[0]}-{lines[-1]} "
                   f"(patch expected {hunk['line']}) -- upstream inserted between the anchors")
        return out

    # One line surviving in another file explains the whole block better than each line
    # reporting itself missing, so the destination is looked for before the pickaxe.
    dest = {}
    for c in missing:
        for f in git(tree, "grep", "-l", "--fixed-strings", c).splitlines():
            if f != hunk["file"]:
                dest[f] = dest.get(f, 0) + 1
    if dest:
        where = max(dest, key=dest.get)
        out.append(f"    - {len(missing)} context line(s) left this file; "
                   f"{dest[where]} of them now in `{where}` -- the code moved")
    else:
        out.append(f"    - {len(missing)} context line(s) not found anywhere in the tree")

    # Without a base the whole history is searched; the newest commit to touch the line is
    # the culprit either way, a range only trims noise.
    span = f"{base}..{ref}" if base else ref
    if True:
        seen = []
        for c in missing[:MAX_MISSING_REPORTED]:
            log = git(tree, "log", "--oneline", "-1", "-S", c, span).strip()
            sha = log.split(" ", 1)[0] if log else ""
            if sha and sha not in seen:
                seen.append(sha)
                subject = log.split(" ", 1)[1].strip("* ") if " " in log else ""
                out.append(f"    - changed by {sha} {subject[:72]}")
    for c in missing[:MAX_MISSING_REPORTED]:
        out.append(f"      gone: `{c[:66]}`")
    return out


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--tree", required=True, help="checkout of the ref the patch failed against")
    p.add_argument("--patch", required=True)
    p.add_argument("--ref", default="HEAD", help="ref name inside --tree to read files from")
    p.add_argument("--base", default="", help="ref the patch is current for; enables the pickaxe")
    args = p.parse_args()

    reported = 0
    for hunk in parse_hunks(args.patch):
        if not hunk["file"] or hunk_applies(args.tree, hunk):
            continue
        if reported >= MAX_HUNKS_REPORTED:
            print("  - (further hunks not analysed)")
            break
        print(f"  - `{hunk['file']}` hunk at line {hunk['line']}")
        for l in explain_hunk(args.tree, args.ref, args.base, hunk):
            print(l)
        reported += 1
    if not reported:
        print("  - every hunk applies on its own; the patch fails only as a sequence")
    return 0


if __name__ == "__main__":
    sys.exit(main())
