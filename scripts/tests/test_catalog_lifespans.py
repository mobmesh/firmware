"""Console catalog build history: since, until, replacements and retired commands from git history."""
import importlib.util
import json
import subprocess
import tempfile
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("catalog_lifespans", REPO_ROOT / "scripts/console/catalog_lifespans.py")
lifespans = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(lifespans)


def doc(*usages):
    return '# Mod\n\n' + ''.join(f'## {u}\n\n```text\n{u}\n```\n\nDoes {u}.\n\n' for u in usages)


def entry(syntax):
    return {'syntax': syntax, 'path': syntax.split()[:1], 'brief': syntax + ' help'}


class Lifespan(unittest.TestCase):
    def test_last_unbroken_run(self):
        self.assertEqual(lifespans.lifespan([True, True, True]), (0, None))
        self.assertEqual(lifespans.lifespan([False, True, True]), (1, None))
        self.assertEqual(lifespans.lifespan([True, True, False]), (0, 2))
        self.assertEqual(lifespans.lifespan([True, False, True]), (2, None))

    def test_command_words_drop_arguments(self):
        self.assertEqual(lifespans.command_words('ota wan verify [refresh]'), 'ota wan verify')
        self.assertEqual(lifespans.command_words('set x <on|off>'), 'set x')


class Stamp(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.repo = Path(self.tmp.name)
        self.git('init', '-q', '-b', 'main')
        self.git('config', 'user.email', 't@example.com')
        self.git('config', 'user.name', 't')

    def tearDown(self):
        self.tmp.cleanup()

    def git(self, *args):
        return subprocess.run(['git', '-C', str(self.repo), *args], capture_output=True, text=True, check=True).stdout.strip()

    def commit(self, usages, catalog):
        docs = self.repo / 'mods/m/docs'
        docs.mkdir(parents=True, exist_ok=True)
        (docs / 'cli-additions.md').write_text(doc(*usages))
        root = self.repo / lifespans.CATALOG
        root.mkdir(parents=True, exist_ok=True)
        (root / 'mobmesh.json').write_text(json.dumps([entry(s) for s in catalog]))
        self.git('add', '-A')
        self.git('commit', '-q', '-m', 'c')
        return self.git('rev-parse', '--short=7', 'HEAD')

    def test_since_until_and_retired(self):
        first = self.commit(['keep', 'gone', 'old'], ['keep', 'gone', 'old'])
        second = self.commit(['keep', 'gone', 'old [x]', 'new'], ['keep', 'gone', 'old [x]', 'new'])
        third = self.commit(['keep', 'old [x]', 'new'], ['keep', 'gone', 'old [x]', 'new'])
        lifespans.FIRST_BUILD = first
        mods = [entry(s) for s in ('keep', 'gone', 'old [x]', 'new')]
        out, manifest, report = lifespans.stamp(self.repo, mods, {'status': 'x'})
        self.assertEqual([b[0] for b in manifest['builds']], [first, second, third])
        self.assertEqual(manifest['status'], 'x')
        by = {e['syntax']: e for e in out}
        self.assertNotIn('since', by['keep'])
        self.assertEqual(by['new']['since'], second)
        self.assertEqual(by['gone']['until'], third)
        self.assertNotIn('replacedBy', by['gone'])
        self.assertEqual((by['old']['until'], by['old']['replacedBy'], by['old']['brief']), (second, 'old [x]', 'old help'))
        self.assertEqual(report['retired'], ['gone', 'old -> old [x]'])
        again, _, _ = lifespans.stamp(self.repo, out, manifest)
        self.assertEqual(again, out)

    def test_retired_without_curated_entry_is_reported(self):
        first = self.commit(['keep', 'stray'], ['keep'])
        self.commit(['keep'], ['keep'])
        lifespans.FIRST_BUILD = first
        out, _, report = lifespans.stamp(self.repo, [entry('keep')], {})
        self.assertEqual([e['syntax'] for e in out], ['keep'])
        self.assertEqual(report['uncurated'], ['stray'])


if __name__ == '__main__':
    unittest.main()
