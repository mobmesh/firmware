"""App offline stamp: the precache list, and a version that changes with any app file but not with the worker itself."""
import importlib.util
import json
import re
import tempfile
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location('stamp_app_offline', REPO_ROOT / 'scripts/stamp_app_offline.py')
stamper = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(stamper)

WORKER = "// worker\nconst VERSION = 'dev';\nconst FILES = [];\nconst CACHE = `mm-app-${VERSION}`;\n"


def make_app(root):
    for name, body in {'index.html': 'page', 'main.dart.js': 'code', 'mm-sw.js': WORKER, 'flutter.js': 'x',
                       'canvaskit/canvaskit.wasm': 'ck', 'canvaskit/canvaskit.js.symbols': 's', 'assets/NOTICES': 'n',
                       'assets/fonts/Inter-var.ttf': 'font'}.items():
        (root / name).parent.mkdir(parents=True, exist_ok=True)
        (root / name).write_text(body)


def stamped(root):
    text = (root / 'mm-sw.js').read_text()
    return re.search(r"const VERSION = '(\w+)';", text).group(1), json.loads(re.search(r'const FILES = (\[.*\]);', text).group(1))


class Stamp(unittest.TestCase):
    def test_precache_list_skips_loader_symbols_licences_and_canvaskit(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            make_app(root)
            stamper.stamp(root)
            self.assertEqual(stamped(root)[1], ['assets/fonts/Inter-var.ttf', 'index.html', 'main.dart.js'])

    def test_version_follows_app_files_including_canvaskit(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            make_app(root)
            stamper.stamp(root)
            first = stamped(root)[0]
            stamper.stamp(root)
            self.assertEqual(stamped(root)[0], first, 'restamping an unchanged app keeps its version')
            (root / 'canvaskit/canvaskit.wasm').write_text('ck2')
            stamper.stamp(root)
            self.assertNotEqual(stamped(root)[0], first)

    def test_worker_without_stamp_lines_is_refused(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            make_app(root)
            (root / 'mm-sw.js').write_text('// no stamp lines\n')
            with self.assertRaises(SystemExit):
                stamper.stamp(root)

    def test_vendored_worker_has_stamp_lines(self):
        text = (REPO_ROOT / 'pages/app/mm-sw.js').read_text()
        self.assertEqual(len(re.findall(r"^const VERSION = '[^']*';$", text, re.M)), 1)
        self.assertEqual(len(re.findall(r'^const FILES = \[.*\];$', text, re.M)), 1)


if __name__ == '__main__':
    unittest.main()
