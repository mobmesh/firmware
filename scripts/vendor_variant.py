#!/usr/bin/env python3
"""Copy a board variant from a MeshCore fork into variants/<board>/vendor/, recording its source."""
import argparse
import hashlib
import json
import sys
import urllib.request
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
API = 'https://api.github.com/repos/{repo}/{path}'
RAW = 'https://raw.githubusercontent.com/{repo}/{commit}/{path}'


def fetch(url):
    request = urllib.request.Request(url, headers={'User-Agent': 'mobmesh-vendor-variant'})
    with urllib.request.urlopen(request, timeout=30) as response:
        return response.read()


def sections(text, keep):
    """The ini text cut to the named sections, in their original order and wording."""
    out, current, held = [], None, []
    for line in text.splitlines(keepends=True):
        # Comments and blank lines just above a header describe the section that follows it.
        if not line.strip() or line.lstrip().startswith((';', '#')):
            held.append(line)
            continue
        if line.startswith('['):
            current = line.strip()[1:-1]
        if current in keep:
            out.extend(held)
            out.append(line)
        held = []
    missing = [name for name in keep if f'[{name}]' not in text]
    if missing:
        sys.exit(f'ERROR: platformio.ini has no section(s) {", ".join(missing)}')
    return ''.join(out).rstrip('\n') + '\n'


def vendor(repo, ref, board, files, keep, dest, getter=fetch):
    commit = json.loads(getter(API.format(repo=repo, path=f'commits/{ref}')))['sha']
    listing = json.loads(getter(API.format(repo=repo, path=f'contents/variants/{board}?ref={commit}')))
    present = sorted(entry['name'] for entry in listing if entry['type'] == 'file')
    absent = [name for name in files if name not in present]
    if absent:
        sys.exit(f'ERROR: {repo}@{ref} variants/{board} has no {", ".join(absent)}')
    dest.mkdir(parents=True, exist_ok=True)
    for old in dest.iterdir():
        old.unlink()
    record = {'repo': repo, 'ref': ref, 'commit': commit, 'files': {}, 'sections': keep,
              'not_copied': [name for name in present if name not in files]}
    for name in files:
        data = getter(RAW.format(repo=repo, commit=commit, path=f'variants/{board}/{name}'))
        if name == 'platformio.ini':
            data = sections(data.decode(), keep).encode()
        (dest / name).write_bytes(data)
        record['files'][name] = hashlib.sha256(data).hexdigest()
    # The fork's licence travels with the copied files; it is not built.
    licence = json.loads(getter(API.format(repo=repo, path='license')))['path']
    (dest / 'LICENSE').write_bytes(getter(RAW.format(repo=repo, commit=commit, path=licence)))
    (dest.parent / 'SOURCE.json').write_text(json.dumps(record, indent=2) + '\n')
    return record


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo', required=True, help='GitHub owner/name, e.g. Cloud-121/MeshCore')
    parser.add_argument('--ref', required=True, help='tag or commit to copy from')
    parser.add_argument('--board', required=True, help='variant folder name, e.g. grumpy_node')
    parser.add_argument('--files', required=True, help='comma-separated file names to copy')
    parser.add_argument('--sections', required=True, help='comma-separated platformio.ini sections to keep')
    args = parser.parse_args()
    dest = REPO_ROOT / 'variants' / args.board / 'vendor'
    record = vendor(args.repo, args.ref, args.board, args.files.split(','), args.sections.split(','), dest)
    print(f"OK: copied {len(record['files'])} file(s) from {args.repo}@{record['commit'][:7]} into {dest}")
    for name in record['not_copied']:
        print(f'  not copied: {name}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
