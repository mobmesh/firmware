#!/usr/bin/env python3
"""Detect when the console's F1 command reference has fallen behind its sources.

Two checks, both reported together:
  native  - commands upstream's repeater CLI accepts, compared with the baseline recorded
            when pages/console/src/catalog/upstream.json was last curated
  manual  - pages/console/owners-manual.md against docs/owners-manual.md

    console_reference_check.py check <upstream-checkout>       report drift, exit 1 if any
    console_reference_check.py baseline <upstream-checkout>    rewrite the baseline after curating

The baseline compares upstream with itself, so commands the catalog keeps elsewhere
(MobMesh overrides, handlers in other files) never show up as drift.
"""
import json
import re
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
BASELINE = REPO / "pages/console/src/catalog/upstream-baseline.json"
MANUAL_SOURCE = REPO / "docs/owners-manual.md"
MANUAL_COPY = REPO / "pages/console/owners-manual.md"


def literals(text, variable):
    pattern = r'(?:memcmp|strcmp|strncmp)\(\s*' + variable + r'\s*,\s*"([^"]+)"'
    return {match.strip() for match in re.findall(pattern, text)}


def function_body(text, name):
    start = text.index("void CommonCLI::" + name)
    end = text.find("\nvoid CommonCLI::", start + 10)
    return text[start:end if end > 0 else None]


def upstream_commands(checkout):
    cli = (checkout / "src/helpers/CommonCLI.cpp").read_text()
    repeater = "".join(p.read_text() for p in sorted((checkout / "examples/simple_repeater").glob("*.cpp")))
    found = {c for c in literals(cli + repeater, "command") if c not in ("get", "set")}
    found |= {"get " + k for k in literals(function_body(cli, "handleGetCmd"), "config")}
    found |= {"set " + k for k in literals(function_body(cli, "handleSetCmd"), "config")}
    return sorted(found)


def describe(checkout):
    run = lambda *a: subprocess.run(["git", "-C", str(checkout), *a], capture_output=True, text=True).stdout.strip()
    return {"tag": run("describe", "--tags", "--exact-match", "--match", "repeater-v*") or None, "commit": run("rev-parse", "HEAD")}


def main():
    mode, checkout = sys.argv[1], Path(sys.argv[2])
    commands = upstream_commands(checkout)
    if mode == "baseline":
        BASELINE.write_text(json.dumps({**describe(checkout), "commands": commands}, indent=2) + "\n")
        print(f"baseline written: {len(commands)} commands at {describe(checkout)}")
        return 0
    base = json.loads(BASELINE.read_text())
    added = sorted(set(commands) - set(base["commands"]))
    removed = sorted(set(base["commands"]) - set(commands))
    manual_stale = MANUAL_SOURCE.read_text() != MANUAL_COPY.read_text()
    now = describe(checkout)
    lines = []
    if added or removed:
        lines += [f"### Native commands changed upstream",
                  f"Baseline `{base.get('tag') or base['commit'][:12]}` → latest `{now['tag'] or now['commit'][:12] or 'unknown'}`.", ""]
        lines += [f"- added: `{c}`" for c in added] + [f"- removed: `{c}`" for c in removed] + [""]
        lines += ["Update `pages/console/src/catalog/upstream.json`, then refresh the baseline:",
                  "`scripts/console/console_reference_check.py baseline <upstream checkout>`", ""]
    if manual_stale:
        lines += ["### Manual copy is stale", "`pages/console/owners-manual.md` differs from `docs/owners-manual.md`;",
                  "copy it: `cp docs/owners-manual.md pages/console/owners-manual.md`.", ""]
    report = "\n".join(lines)
    print(report or "console reference up to date")
    if len(sys.argv) > 3:
        Path(sys.argv[3]).write_text(report)
    return 1 if lines else 0


if __name__ == "__main__":
    sys.exit(main())
