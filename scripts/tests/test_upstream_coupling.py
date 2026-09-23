"""Mods reach upstream only from the adapter or from variant mechanism code.

Anywhere else is coupling that breaks silently when upstream refactors: on 2026-08-27 a
deleted MainBoard virtual stopped power-guard compiling while its patch still applied
(issue #21). ALLOWED_REACHES is the exception list, and is currently empty.
"""
import argparse
import glob
import importlib.util
import os
import re
import tempfile
import unittest
from pathlib import Path

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
GENERATOR_PATH = Path(REPO_ROOT) / "scripts/generate-board-config.py"
GENERATOR_SPEC = importlib.util.spec_from_file_location("generate_board_config_coupling", GENERATOR_PATH)
generator = importlib.util.module_from_spec(GENERATOR_SPEC)
GENERATOR_SPEC.loader.exec_module(generator)

# Matched in `x->y` and `x.y` form both: power-guard reaches enterDeepSleep by the global
# `board` and by `_board->`, so the arrow form alone sees half the surface.
HOLDERS = ["board", "_board", "prefs", "_prefs", "callbacks", "_callbacks",
           "rtc_clock", "radio_driver", "sensors", r"getRTCClock\(\)"]

CALL_RE = re.compile(
    r"(?<![A-Za-z0-9_.>])(" + "|".join(HOLDERS) + r")\s*(->|\.)\s*([A-Za-z_][A-Za-z0-9_]*)"
)
COMMENT_RE = re.compile(r"//.*$|/\*.*?\*/", re.DOTALL)

# Reaching upstream is the job of these files, not a violation in them: ModHooks is the
# adapter, variants/ is board mechanism. Any mod may add a body to either.
ADAPTER_FILES = {"src/helpers/ModHooks.cpp", "src/helpers/ModHooks.h"}


def is_adapter(path):
    return path in ADAPTER_FILES or path.startswith("variants/")


ALLOWED_REACHES = set()


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


def added_source(patch_path):
    """Added source lines grouped by their upstream target."""
    out, target = {}, None
    with open(patch_path, encoding="utf-8") as handle:
        for line in handle:
            if line.startswith("+++ b/"):
                target = line[6:].strip()
            elif line.startswith("+") and not line.startswith("+++") and target:
                out.setdefault(target, []).append(line[1:])
    return {target: "".join(lines) for target, lines in out.items()}


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
        with tempfile.TemporaryDirectory() as temp:
            generator.cmd_compose_mods(argparse.Namespace(upstream=temp, mods="shim"))
            adapter = os.path.join(temp, "src", "helpers", "ModHooks.cpp")
            hits = reaches_in_source(adapter, "src/helpers/ModHooks.cpp")
        self.assertTrue(hits, "regex matches nothing even in ModHooks.cpp")

    def test_group_send_reports_upstream_airtime_in_both_roles(self):
        header = Path(REPO_ROOT, "mods/shim/files/src/helpers/ModHooks.h").read_text()
        self.assertRegex(
            header,
            r"modSendGroup\([^;]+uint32_t\* packet_id, uint32_t\* airtime_ms\);",
        )

        patch = Path(REPO_ROOT, "mods/shim/patches/0001_mod-hook-points.patch")
        source = added_source(patch)
        for role in ("simple_repeater", "simple_room_server"):
            target = f"examples/{role}/MyMesh.cpp"
            self.assertIn(target, source)
            self.assertRegex(
                source[target],
                re.compile(
                    r"bool modSendGroup\(.*?uint32_t\* packet_id, uint32_t\* airtime_ms\)"
                    r".*?sendFlood(?:Scoped)?\([^;]+;.*?"
                    r"\*airtime_ms = radio_driver\.getEstAirtimeFor"
                    r"\(packet->getRawLength\(\)\);",
                    re.DOTALL,
                ),
                f"{target} no longer returns the configured packet's upstream airtime",
            )

    def test_tempradio_snapshot_tracks_cli_and_loop_transitions(self):
        patch = Path(REPO_ROOT, "mods/shim/patches/0001_mod-hook-points.patch")
        source = added_source(patch)
        snapshot = re.compile(
            r"mod_temp_radio\s*=\s*\{\s*pending_freq,\s*pending_bw,\s*"
            r"pending_sf,\s*pending_cr,\s*set_radio_at\s*==\s*0\s*&&\s*"
            r"revert_radio_at\s*!=\s*0\s*&&\s*"
            r"!millisHasNowPassed\(revert_radio_at\)\s*\};",
            re.DOTALL,
        )
        for role in ("simple_repeater", "simple_room_server"):
            target = f"examples/{role}/MyMesh.cpp"
            body = source[target]
            matches = list(snapshot.finditer(body))
            self.assertEqual(len(matches), 2, f"{target} must refresh after CLI and timers")
            dispatch = body.index("if (!modHandleCliCommand")
            self.assertGreater(matches[0].start(), dispatch)

        patch_text = patch.read_text()
        self.assertEqual(
            patch_text.count('MESH_DEBUG_PRINTLN("Radio params restored");\n   }\n \n+  mod_temp_radio'),
            2,
            "both role loops refresh only after the revert timer is processed",
        )

    def test_drift_canary_builds_sync_settings_for_both_roles(self):
        workflow = Path(REPO_ROOT, ".github/workflows/patch-drift-canary.yml").read_text()
        self.assertIn("CANARY_EXTRA_MODS: sync-settings", workflow)
        self.assertIn("for role in sorted({t['role'] for t in targets})", workflow)
        self.assertIn("t['mods'] + extras", workflow)

    def test_drift_canary_probes_the_stats_serial_gate(self):
        """power-guard's remote stats forward breaks silently; only the canary grep sees it."""
        workflow = Path(REPO_ROOT, ".github/workflows/patch-drift-canary.yml").read_text()
        source = Path(REPO_ROOT, "mods/power-guard/files/src/helpers/esp32/PowerGuardIntegration.cpp").read_text()
        for cmd in ("stats-core", "stats-radio", "stats-packets"):
            self.assertIn(f'"{cmd}"', source)
            self.assertIn(cmd, workflow)



if __name__ == "__main__":
    unittest.main(verbosity=2)
