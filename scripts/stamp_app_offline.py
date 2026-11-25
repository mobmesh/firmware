#!/usr/bin/env python3
"""Stamp pages/app/mm-sw.js with the app's file list and a content hash, so each published app change is a new offline version."""
import argparse
import fnmatch
import hashlib
import json
import os
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
WORKER = 'mm-sw.js'
# Left out of the precache: loader files the page never fetches, debug symbols, licence text, and CanvasKit, which the worker picks per browser.
SKIP = ('mm-sw.js', 'flutter.js', 'flutter_bootstrap.js', 'flutter_service_worker.js', '*.symbols', 'canvaskit/*', 'assets/NOTICES')


IMPORT = re.compile(r'''(?:\bfrom\s*|\bimport\s*\(?\s*|\bsrc=)['"](\.{1,2}/[^'"]+)['"]''')


def app_files(app):
    return sorted(p.relative_to(app).as_posix() for p in app.rglob('*') if p.is_file() and '__pycache__' not in p.parts)


def shared_files(app, files):
    """Modules outside the app folder that its pages and mm-*.js modules import, followed through their imports, as paths from the app folder."""
    root, found = app.resolve(), set()
    todo = [app / f for f in files if (f.startswith('mm-') and f.endswith('.js')) or ('/' not in f and f.endswith('.html'))]
    while todo:
        source = todo.pop()
        for spec in IMPORT.findall(source.read_text(encoding='utf-8')):
            target = (source.parent / spec).resolve()
            if root in target.parents or target in found:
                continue
            if not target.is_file():
                sys.exit(f'{source}: imports missing {spec}')
            found.add(target)
            todo.append(target)
    return sorted(Path(os.path.relpath(t, root)).as_posix() for t in found)


def precache(files):
    return [f for f in files if not any(fnmatch.fnmatch(f, pattern) for pattern in SKIP)]


def version(app, files):
    digest = hashlib.sha256()
    for f in files:
        if f == WORKER:
            continue
        digest.update(f.encode() + b'\0' + (app / f).resolve().read_bytes() + b'\0')
    return digest.hexdigest()[:16]


def stamp(app):
    worker = app / WORKER
    own = app_files(app)
    files = own + shared_files(app, own)
    text = worker.read_text()
    ver, listed = version(app, files), json.dumps(precache(files))
    text, n_version = re.subn(r"^const VERSION = '[^']*';$", f"const VERSION = '{ver}';", text, flags=re.M)
    text, n_files = re.subn(r'^const FILES = \[.*\];$', lambda _: f'const FILES = {listed};', text, flags=re.M)
    if n_version != 1 or n_files != 1:
        sys.exit(f'{worker}: expected one VERSION line and one FILES line')
    worker.write_text(text)
    return ver, len(precache(files))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('app', nargs='?', type=Path, default=REPO / 'pages/app')
    ver, count = stamp(parser.parse_args().app)
    print(f'app offline version {ver}, {count} files precached plus CanvasKit')


if __name__ == '__main__':
    main()
