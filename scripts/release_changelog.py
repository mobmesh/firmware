#!/usr/bin/env python3
"""Compose a release body: upstream notes plus a MeshCore Enhanced+ list of new features and CLI changes."""
import argparse
import re
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path

MARKER = re.compile(r'<!-- mobmesh-build:([0-9a-f]{7,40}) -->')
SECTION = '## MeshCore Enhanced+ Changes'
UPSTREAM = '## Upstream release notes'
# Paths whose changes alter what is inside a published image.
FIRMWARE_PATHS = ('mods', 'variants', 'build-targets.yaml', 'scripts/generate-board-config.py',
                  'scripts/assemble-release.py', 'scripts/merge_flash_image.py',
                  'scripts/patch_ota_metadata.py')
SUBJECT = re.compile(r'^feat(\([^)]*\))?!?: ')
INDEX_ROW = re.compile(r'^\| \[`(.+?)`\]\([^)]*\) \| (.+?) \|\s*$')


def marker(body):
    found = MARKER.search(body or '')
    return found.group(1) if found else None


def entries(body):
    """The existing changes entries, newest first; a repeated section heading from a hand edit is dropped."""
    if not body or SECTION not in body:
        return ''
    block = body.split(SECTION, 1)[1]
    block = re.split(r'\n' + re.escape(UPSTREAM) + r'|\n<!-- mobmesh-build:', block, maxsplit=1)[0]
    lines = [line for line in block.splitlines() if line.strip() != SECTION]
    return re.sub(r'\n{3,}', '\n\n', '\n'.join(lines)).strip()


def subjects(repo, since, head):
    """feat subjects touching firmware paths in since..head, newest first."""
    out = subprocess.run(['git', '-C', str(repo), 'log', '--no-merges', '--format=%s',
                          f'{since}..{head}', '--', *FIRMWARE_PATHS],
                         capture_output=True, text=True, check=True).stdout
    return [s for s in out.splitlines() if SUBJECT.match(s)]


def cli_index(repo, rev):
    """Command -> purpose from every mod's cli-additions.md command index at rev."""
    files = subprocess.run(['git', '-C', str(repo), 'ls-tree', '-r', '--name-only', rev, 'mods'],
                           capture_output=True, text=True, check=True).stdout.split()
    index = {}
    for path in (f for f in files if f.endswith('docs/cli-additions.md')):
        text = subprocess.run(['git', '-C', str(repo), 'show', f'{rev}:{path}'],
                              capture_output=True, text=True, check=True).stdout
        for line in text.splitlines():
            row = INDEX_ROW.match(line)
            if row:
                index[row.group(1).replace('\\|', '|')] = row.group(2)
    return index


def cli_changes(repo, since, head):
    before, after = cli_index(repo, since), cli_index(repo, head)
    lines = [f"- cli: new '{c}'" for c in after if c not in before]
    lines += [f"- cli: changed '{c}'" for c in after if c in before and after[c] != before[c]]
    lines += [f"- cli: removed '{c}'" for c in before if c not in after]
    return lines


def compose(notes, existing, previous, head, repo, date):
    prior = entries(existing)
    since = marker(existing) or marker(previous)
    block = prior
    if since and not (head.startswith(since) or since.startswith(head)):
        lines = [f'- {s}' for s in subjects(repo, since, head)] + cli_changes(repo, since, head)
        if lines:
            block = '\n'.join([f'#### Build `{head[:7]}`, {date}', ''] + lines + ([''] + [prior] if prior else []))
    intro, _, upstream = notes.partition(UPSTREAM)
    parts = [intro.rstrip(), '']
    if block:
        parts += [SECTION, '', block.rstrip(), '']
    if upstream:
        parts += [UPSTREAM + upstream.rstrip(), '']
    parts.append(f'<!-- mobmesh-build:{head} -->')
    return '\n'.join(parts) + '\n'


def read(path):
    return Path(path).read_text() if path and Path(path).exists() else ''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--notes', required=True, help='upstream-notes file built by the build job')
    parser.add_argument('--existing', help='current body of this release, if it exists')
    parser.add_argument('--previous', help='body of the previous release for the same role')
    parser.add_argument('--head', required=True, help='commit the images were built from')
    parser.add_argument('--repo', default='.')
    parser.add_argument('--date', default=datetime.now(timezone.utc).strftime('%Y-%m-%d'))
    args = parser.parse_args()
    sys.stdout.write(compose(read(args.notes), read(args.existing), read(args.previous),
                             args.head, args.repo, args.date))
    return 0


if __name__ == '__main__':
    sys.exit(main())
