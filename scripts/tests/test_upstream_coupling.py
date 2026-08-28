"""Mods reach upstream only from the adapter or from variant mechanism code.

Anywhere else is coupling that breaks silently when upstream refactors: on 2026-08-27 a
deleted MainBoard virtual stopped power-guard compiling while its patch still applied
(issue #21). ALLOWED_REACHES holds the only two exceptions, each with its reason.
"""
import glob
import os
import re
import unittest

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

# Upstream objects a mod might reach through. Matched in `x->y` and `x.y` form both --
# power-guard reaches enterDeepSleep by the global `board` and by `_board->`, so a grep
# for the arrow form alone sees half the surface.
HOLDERS = ["board", "_board", "prefs", "_prefs", "callbacks", "_callbacks",
           "rtc_clock", "radio_driver", "sensors", r"getRTCClock\(\)"]

CALL_RE = re.compile(
    r"(?<![A-Za-z0-9_.>])(" + "|".join(HOLDERS) + r")\s*(->|\.)\s*([A-Za-z_][A-Za-z0-9_]*)"
)
COMMENT_RE = re.compile(r"//.*$|/\*.*?\*/", re.DOTALL)

# Reaching upstream is the job of these files, not a violation in them. ModHooks is the
# adapter; variants/ is board mechanism, which conventions.md puts on the hardware side
# of the line deliberately. Any mod may add a body to either.
ADAPTER_FILES = {"src/helpers/ModHooks.cpp", "src/helpers/ModHooks.h"}


def is_adapter(path):
    return path in ADAPTER_FILES or path.startswith("variants/")


# The two reaches that stay. CommonCLICallbacks is the CLI's own accessor for its own
# strings, reached from inside a CommonCLI method body -- a free hook cannot see `_callbacks`
# without inventing a global, and the interface exists to be called from exactly here.
ALLOWED_REACHES = {
    "hotspot-ota/0001 src/helpers/esp32/CommonCliMods.cpp _callbacks->getFirmwareVer",
    "hotspot-ota/0001 src/helpers/esp32/CommonCliMods.cpp _callbacks->getBuildDate",
}


def reaches_in_source(path, repo_rel):
    """(file, call) for each direct upstream reach in a mod's own source file."""
    out = set()
    with open(path, encoding="utf-8") as handle:
        for line in handle:
            for holder, op, member in CALL_RE.findall(COMMENT_RE.sub("", line)):
                out.add((repo_rel, f"{holder}{op}{member}"))
    return out


def reaches(patch_path):
    """(file, call) for each direct upstream reach in the lines this patch adds.

    A call already on a removed line in the same hunk is upstream's own code being
    edited in place, not a mod reaching out -- timing-safety rewrites
    `getRTCClock()->getCurrentTime() - neighbour->heard_timestamp` into a safe helper
    and adds no coupling by doing it.
    """
    out, removed, added, target = set(), [], [], None

    def flush():
        prior = "\n".join(removed).replace(" ", "")
        for line in added:
            for holder, op, member in CALL_RE.findall(COMMENT_RE.sub("", line)):
                if f"{holder}{op}{member}" not in prior:
                    out.add((target, f"{holder}{op}{member}"))

    with open(patch_path, encoding="utf-8") as handle:
        for line in handle:
            if line.startswith("+++ b/"):
                flush()
                removed, added = [], []
                target = line[6:].strip()
            elif line.startswith("@@"):
                flush()
                removed, added = [], []
            elif line.startswith("-") and not line.startswith("---"):
                removed.append(line[1:].rstrip())
            elif line.startswith("+") and not line.startswith("+++"):
                added.append(line[1:].rstrip())
    flush()
    return out


def survey():
    """Every non-adapter reach a mod makes, from its patches and from its own source.

    Both halves are needed: mods ship code two ways now, and scanning only patches went
    quiet the moment shim's adapter moved into mods/shim/files/.
    """
    found = set()
    for patch in sorted(glob.glob(os.path.join(REPO_ROOT, "mods", "*", "patches", "*.patch"))):
        mod = os.path.basename(os.path.dirname(os.path.dirname(patch)))
        if mod == "shim":
            continue
        pid = os.path.basename(patch).split("_")[0]
        for target, call in reaches(patch):
            if not is_adapter(target):
                found.add(f"{mod}/{pid} {target} {call}")
    for src in sorted(glob.glob(os.path.join(REPO_ROOT, "mods", "*", "files", "**", "*"), recursive=True)):
        if not os.path.isfile(src):
            continue
        mod = src.split(os.sep + "mods" + os.sep)[1].split(os.sep)[0]
        if mod == "shim":
            continue
        rel = src.split(os.sep + "files" + os.sep)[1]
        for target, call in reaches_in_source(src, rel):
            if not is_adapter(target):
                found.add(f"{mod}/files {target} {call}")
    return found


class UpstreamCouplingTestCase(unittest.TestCase):
    def test_no_new_direct_reach(self):
        new = sorted(survey() - ALLOWED_REACHES)
        self.assertEqual(
            new, [],
            "mod code reaches upstream outside the adapter:\n  " + "\n  ".join(new) +
            "\nAdd a hook to mods/shim's ModHooks.h and call that instead.",
        )

    def test_no_stale_exception(self):
        """An exception left behind after its call moves would let the next one in unnoticed."""
        stale = sorted(ALLOWED_REACHES - survey())
        self.assertEqual(stale, [], f"permitted reaches no longer found in any patch: {stale}")

    def test_the_pattern_still_matches(self):
        """Guards the regex and the source scan: the adapter reaches upstream by definition."""
        adapter = os.path.join(REPO_ROOT, "mods", "shim", "files", "src", "helpers", "ModHooks.cpp")
        self.assertTrue(os.path.isfile(adapter), f"shim's adapter is not at {adapter}")
        hits = reaches_in_source(adapter, "src/helpers/ModHooks.cpp")
        self.assertTrue(hits, "regex matches nothing even in ModHooks.cpp")


if __name__ == "__main__":
    unittest.main(verbosity=2)
