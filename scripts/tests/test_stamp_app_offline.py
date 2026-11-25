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

    def test_modules_imported_from_outside_the_app_are_precached_and_versioned(self):
        with tempfile.TemporaryDirectory() as tmp:
            pages = Path(tmp)
            root = pages / 'app'
            root.mkdir()
            make_app(root)
            (pages / 'console/src').mkdir(parents=True)
            (root / 'mm-page.js').write_text("import {a} from '../console/src/a.js';\n")
            (root / 'index.html').write_text('<script type="module">import {bg} from "../console/src/bg.js";</script>')
            (pages / 'console/src/bg.js').write_text('export const bg = 1;\n')
            (pages / 'console/src/a.js').write_text("export {b} from './b.js';\nimport('./lazy.js');\n")
            (pages / 'console/src/b.js').write_text('export const b = 1;\n')
            (pages / 'console/src/lazy.js').write_text('export default 1;\n')
            stamper.stamp(root)
            first, files = stamped(root)
            self.assertEqual([f for f in files if f.startswith('../')], ['../console/src/a.js', '../console/src/b.js', '../console/src/bg.js', '../console/src/lazy.js'])
            (pages / 'console/src/b.js').write_text('export const b = 2;\n')
            stamper.stamp(root)
            self.assertNotEqual(stamped(root)[0], first, 'a shared module change is a new offline version')

    def test_missing_imported_module_is_refused(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp) / 'app'
            root.mkdir()
            make_app(root)
            (root / 'mm-page.js').write_text("import {a} from '../console/src/gone.js';\n")
            with self.assertRaises(SystemExit):
                stamper.stamp(root)

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
