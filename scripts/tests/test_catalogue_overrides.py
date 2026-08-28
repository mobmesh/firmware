#!/usr/bin/env python3
"""Unit tests for check-catalogue-overrides.py.

The first test warns when the real files drift apart; overrides are cosmetic, so it
never fails. The rest cover the checker's own logic and do fail.

Run: python3 scripts/tests/test_catalogue_overrides.py
"""
import importlib.util
import re
import io
import json
import tempfile
import unittest
import warnings
from contextlib import redirect_stdout
from pathlib import Path
from unittest import mock

SCRIPT_PATH = Path(__file__).resolve().parent.parent / "check-catalogue-overrides.py"
spec = importlib.util.spec_from_file_location("check_catalogue_overrides", SCRIPT_PATH)
cco = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cco)

CATALOGUE = {"device": [{"name": "Heltec V4", "maker": "heltec"}]}


def run(catalogue, overrides, art=(), remote=lambda url: None):
    """Run main() against synthetic files; returns (exit code, stdout).

    `remote` stands in for the network -- return a reason string to fail a URL."""
    with tempfile.TemporaryDirectory() as td:
        root = Path(td)
        cat_path = root / "mc_config.json"
        ovr_path = root / "data" / "catalogue-overrides.json"
        ovr_path.parent.mkdir()
        cat_path.write_text(json.dumps(catalogue))
        ovr_path.write_text(json.dumps(overrides))
        for name in art:
            (ovr_path.parent / name).write_bytes(b"")
        buf = io.StringIO()
        with mock.patch.object(cco, "CATALOGUE", cat_path), \
             mock.patch.object(cco, "OVERRIDES", ovr_path), \
             redirect_stdout(buf):
            return cco.main(check_remote=remote), buf.getvalue()


class TestRealFiles(unittest.TestCase):
    def test_shipped_overrides_all_resolve(self):
        """Warns, never fails. Every override is cosmetic -- a stale one falls back to
        upstream's own name or art, so an upstream rename is not worth a red suite."""
        buf = io.StringIO()
        with redirect_stdout(buf):
            cco.main()
        report = buf.getvalue().strip()
        if not report.startswith("all overrides match"):
            warnings.warn("catalogue overrides need attention:\n" + report, stacklevel=2)

    def test_no_orphaned_hide(self):
        """The one hard failure against the real files: an orphaned hidden:true puts a
        device back in the catalogue, which is not cosmetic."""
        catalogue = json.loads(cco.CATALOGUE.read_text())
        overrides = json.loads(cco.OVERRIDES.read_text())
        self.assertEqual(cco.orphaned_hides(catalogue, overrides), [])


class TestDetection(unittest.TestCase):
    def test_clean_overrides_pass(self):
        code, out = run(CATALOGUE, {"makers": {"heltec": {"dim": True}}})
        self.assertEqual(code, 0, out)

    def test_renamed_device_is_orphaned(self):
        code, out = run(CATALOGUE, {"devices": {"Heltec V3": {"dim": True}}})
        self.assertEqual(code, 1)
        self.assertIn("orphaned override: device 'Heltec V3'", out)

    def test_renamed_maker_is_orphaned(self):
        code, out = run(CATALOGUE, {"makers": {"heltek": {"dim": True}}})
        self.assertEqual(code, 1)
        self.assertIn("orphaned override: maker 'heltek'", out)

    def test_live_remote_url_passes(self):
        code, out = run(CATALOGUE, {"makers": {"heltec": {"icon": "https://cdn.example/l.svg"}}})
        self.assertEqual(code, 0, out)

    def test_dead_remote_url_is_reported(self):
        code, out = run(CATALOGUE, {"makers": {"heltec": {"icon": "https://cdn.example/l.svg"}}},
                        remote=lambda url: "HTTP 404")
        self.assertEqual(code, 1)
        self.assertIn("https://cdn.example/l.svg -- HTTP 404", out)

    def test_protocol_relative_url_is_fetched(self):
        seen = []
        run(CATALOGUE, {"makers": {"heltec": {"icon": "//cdn.example/l.svg"}}},
            remote=lambda url: seen.append(url))
        self.assertEqual(seen, ["//cdn.example/l.svg"])

    def test_offline_reports_unreachable_not_missing(self):
        code, out = run(CATALOGUE, {"makers": {"heltec": {"icon": "https://cdn.example/l.svg"}}},
                        remote=lambda url: "unreachable (URLError)")
        self.assertEqual(code, 1)
        self.assertIn("unreachable (URLError)", out)

    def test_remote_status_upgrades_protocol_relative(self):
        seen = []
        with mock.patch.object(cco.urllib.request, "urlopen",
                               side_effect=lambda req, timeout: seen.append(req.full_url)):
            cco.remote_status("//cdn.example/l.svg")
        self.assertEqual(seen, ["https://cdn.example/l.svg"])

    def test_dead_art_path_is_reported(self):
        code, out = run(CATALOGUE, {"makers": {"heltec": {"icon": "art/gone.svg"}}})
        self.assertEqual(code, 1)
        self.assertIn("missing asset: maker 'heltec' icon: art/gone.svg", out)

    def test_present_art_path_passes(self):
        code, out = run(CATALOGUE, {"makers": {"heltec": {"icon": "here.svg"}}}, art=["here.svg"])
        self.assertEqual(code, 0, out)

    def test_unknown_filter_is_reported(self):
        code, out = run(CATALOGUE, {"makers": {"heltec": {"filter": "sepia"}}})
        self.assertEqual(code, 1)
        self.assertIn("unknown filter: maker 'heltec' filter: 'sepia'", out)

    def test_non_boolean_dim_is_reported(self):
        code, out = run(CATALOGUE, {"makers": {"heltec": {"dim": "yes"}}})
        self.assertEqual(code, 1)
        self.assertIn("expected true/false", out)

    def test_orphaned_hide_is_singled_out(self):
        overrides = {"devices": {"Heltec V3": {"hidden": True}, "Heltec V9": {"dim": True}}}
        self.assertEqual(cco.orphaned_hides(CATALOGUE, overrides), ["Heltec V3"])

    def test_live_hide_is_not_an_orphan(self):
        overrides = {"devices": {"Heltec V4": {"hidden": True}}}
        self.assertEqual(cco.orphaned_hides(CATALOGUE, overrides), [])

    def test_filters_match_flash_plan(self):
        """FILTERS is hand-synced with IMAGE_FILTERS; drift makes a live filter look unknown."""
        js = (cco.ROOT / "pages/flasher2/src/flash-plan.js").read_text()
        block = re.search(r"const IMAGE_FILTERS = \{(.*?)\};", js, re.S)
        self.assertIsNotNone(block, "IMAGE_FILTERS not found in flash-plan.js")
        self.assertEqual(set(re.findall(r"(\w+):", block.group(1))), cco.FILTERS)


if __name__ == "__main__":
    unittest.main()
