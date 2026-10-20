#!/usr/bin/env python3
"""Compose a release body: upstream notes plus a MeshCore Enhanced+ changes list kept across rebuilds."""
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
SUBJECT = re.compile(r'^(fix|feat)(\([^)]*\))?!?: ')


def marker(body):
    found = MARKER.search(body or '')
    return found.group(1) if found else None


def entries(body):
    """The existing changes entries, newest first, as one block of text."""
    if not body or SECTION not in body:
        return ''
    block = body.split(SECTION, 1)[1]
    block = re.split(r'\n## |\n<!-- mobmesh-build:', block, maxsplit=1)[0]
    return block.strip()


def subjects(repo, since, head):
    """fix/feat subjects touching firmware paths in since..head, newest first."""
    out = subprocess.run(['git', '-C', str(repo), 'log', '--no-merges', '--format=%s',
                          f'{since}..{head}', '--', *FIRMWARE_PATHS],
                         capture_output=True, text=True, check=True).stdout
    return [s for s in out.splitlines() if SUBJECT.match(s)]


def compose(notes, existing, previous, head, repo, date):
    prior = entries(existing)
    since = marker(existing) or marker(previous)
    if since and head.startswith(since) or (since and since.startswith(head)):
        block = prior
    else:
        listed = subjects(repo, since, head) if since else []
        heading = ('Rebuild' if prior else 'Build') + f' `{head[:7]}`, {date}'
        lines = [f'- {s}' for s in listed] or (['- No firmware changes.'] if since else
                                               ['- First MeshCore Enhanced+ build of this upstream release.'])
        block = '\n'.join([f'#### {heading}', ''] + lines + ([''] + [prior] if prior else []))
    intro, _, upstream = notes.partition(UPSTREAM)
    parts = [intro.rstrip(), '', SECTION, '', block.rstrip(), '']
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
