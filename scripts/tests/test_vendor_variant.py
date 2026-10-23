"""Vendored board variants: the copy, its source record, and how copy-src brings it into the upstream tree."""
import hashlib
import importlib.util
import json
import tempfile
import unittest
from pathlib import Path
from unittest import mock

REPO_ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("vendor_variant", REPO_ROOT / "scripts/vendor_variant.py")
vendor_variant = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(vendor_variant)
GEN_SPEC = importlib.util.spec_from_file_location("generate_board_config", REPO_ROOT / "scripts/generate-board-config.py")
generate = importlib.util.module_from_spec(GEN_SPEC)
GEN_SPEC.loader.exec_module(generate)

INI = """[Board]
extends = esp32_base
build_flags =
  -D PIN=1

[env:Board_repeater]
extends = Board
build_flags = ${Board.build_flags}

; A setup build that is not wanted.
[env:Board_web_setup]
extends = Board

[env:Board_room_server]
extends = Board
"""


def fake_fork(files):
    """A getter answering the three GitHub calls vendor() makes, from an in-memory folder."""
    def get(url):
        if '/commits/' in url:
            return json.dumps({'sha': 'c0ffee' * 6 + 'abcd'}).encode()
        if url.endswith('/license'):
            return json.dumps({'path': 'license.txt'}).encode()
        if url.endswith('/license.txt'):
            return b'MIT License\n'
        if '/contents/' in url:
            return json.dumps([{'name': name, 'type': 'file'} for name in files]).encode()
        return files[url.rsplit('/', 1)[1]]
    return get


class Sections(unittest.TestCase):
    def test_keeps_named_sections_and_drops_the_rest_with_their_comments(self):
        out = vendor_variant.sections(INI, ['Board', 'env:Board_repeater', 'env:Board_room_server'])
        self.assertIn('[env:Board_room_server]', out)
        self.assertNotIn('web_setup', out)
        self.assertNotIn('A setup build', out)
        self.assertIn('  -D PIN=1', out)

    def test_missing_section_stops(self):
        with self.assertRaises(SystemExit):
            vendor_variant.sections(INI, ['Board', 'env:Board_companion'])


class Vendor(unittest.TestCase):
    def test_copies_listed_files_and_records_their_source(self):
        files = {'Board.cpp': b'int x;\n', 'platformio.ini': INI.encode(), 'Setup.cpp': b'wizard\n'}
        with tempfile.TemporaryDirectory() as tmp:
            dest = Path(tmp) / 'board' / 'vendor'
            dest.mkdir(parents=True)
            (dest / 'stale.cpp').write_text('old')
            record = vendor_variant.vendor('owner/fork', 'v1', 'board', ['Board.cpp', 'platformio.ini'],
                                           ['Board', 'env:Board_repeater'], dest, fake_fork(files))
            self.assertEqual(sorted(p.name for p in dest.iterdir()), ['Board.cpp', 'LICENSE', 'platformio.ini'])
            self.assertEqual(record['not_copied'], ['Setup.cpp'])
            source = json.loads((dest.parent / 'SOURCE.json').read_text())
            self.assertEqual(source['files']['Board.cpp'], hashlib.sha256(b'int x;\n').hexdigest())
            self.assertEqual(source['files']['platformio.ini'],
                             hashlib.sha256((dest / 'platformio.ini').read_bytes()).hexdigest())

    def test_a_listed_file_missing_upstream_stops_before_writing(self):
        with tempfile.TemporaryDirectory() as tmp:
            dest = Path(tmp) / 'vendor'
            with self.assertRaises(SystemExit):
                vendor_variant.vendor('owner/fork', 'v1', 'board', ['Gone.cpp'], ['Board'], dest,
                                      fake_fork({'Board.cpp': b''}))
            self.assertFalse(dest.exists())


class CopySrc(unittest.TestCase):
    def test_grumpy_vendor_matches_its_source_record(self):
        folder = REPO_ROOT / 'variants/grumpy_node'
        source = json.loads((folder / 'SOURCE.json').read_text())
        for name, digest in source['files'].items():
            self.assertEqual(hashlib.sha256((folder / 'vendor' / name).read_bytes()).hexdigest(), digest, name)

    def test_copies_vendored_variant_without_its_licence(self):
        with tempfile.TemporaryDirectory() as tmp:
            upstream = Path(tmp)
            generate.copy_vendored_variant(upstream, 'grumpy_node')
            copied = sorted(p.name for p in (upstream / 'variants/grumpy_node').iterdir())
            self.assertIn('platformio.ini', copied)
            self.assertNotIn('LICENSE', copied)

    def test_stops_when_upstream_already_has_the_variant(self):
        with tempfile.TemporaryDirectory() as tmp:
            (Path(tmp) / 'variants/grumpy_node').mkdir(parents=True)
            with self.assertRaises(SystemExit):
                generate.copy_vendored_variant(Path(tmp), 'grumpy_node')

    def test_board_without_vendor_folder_is_left_alone(self):
        with tempfile.TemporaryDirectory() as tmp:
            generate.copy_vendored_variant(Path(tmp), 'xiao_c3')
            self.assertFalse((Path(tmp) / 'variants').exists())


if __name__ == '__main__':
    unittest.main()
