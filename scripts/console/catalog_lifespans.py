#!/usr/bin/env python3
"""Stamp the console catalog with the builds on main and the builds each mod command exists in."""
import argparse
import fnmatch
import json
import os
import re
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
CATALOG = Path('pages/console/src/catalog')
FIRST_BUILD = '91ee01a'
ADDITIONS = 'mods/*/docs/cli-additions.md'


def git(repo, *args):
    return subprocess.run(['git', '-C', str(repo), *args], capture_output=True, text=True).stdout.strip()


def additions(repo, worktree=False, rev='HEAD'):
    """(relative path, text) of each cli-additions.md at rev, or uncommitted if worktree is set."""
    if worktree:
        return [(str(p.relative_to(repo)), p.read_text()) for p in sorted(repo.glob(ADDITIONS))]
    listed = [f for f in git(repo, 'ls-tree', '-r', '--name-only', rev, 'mods').split()
              if fnmatch.fnmatch(f, ADDITIONS)]
    return [(rel, subprocess.run(['git', '-C', str(repo), 'show', f'{rev}:{rel}'], capture_output=True, text=True).stdout)
            for rel in sorted(listed)]


def expand(syntax, section):
    """Expand a `<name>` placeholder inside a literal word using its parameter-table values."""
    head = syntax.split(' ')
    for index, word in enumerate(head[:2]):
        hole = re.search(r'<(\w+)>', word)
        if not hole or word.startswith('<'):
            continue
        row = re.search(r'^\| `' + hole.group(1) + r'` \| (.+?) \|$', section, re.M)
        values = re.findall(r'`([\w.-]+)`', row.group(1)) if row else []
        if values:
            return [' '.join(head[:index] + [word.replace(hole.group(0), v)] + head[index + 1:]) for v in values]
    return [syntax]


def mod_usages(repo, worktree=False, rev='HEAD'):
    """One row per usage line inside a section's ```text fence, as the catalog was first built."""
    rows = []
    for rel, text in additions(repo, worktree, rev):
        mod = Path(rel).parts[1]
        lines = text.split('\n')
        heads = [i for i, line in enumerate(lines) if line.startswith('## ')]
        for index, head in enumerate(heads):
            title = lines[head][3:].strip()
            if title.lower() == 'command index':
                continue
            end = heads[index + 1] if index + 1 < len(heads) else len(lines)
            section = '\n'.join(lines[head:end]).rstrip('\n')
            details = section + '\n' if end < len(lines) else section
            fence = False
            for at in range(head + 1, end):
                line = lines[at]
                if line.startswith('```'):
                    fence = line.strip() == '```text' and not fence
                    continue
                first = line.strip().split(' ')[0] if line.strip() else ''
                if fence and first and ':' not in first and first[0] not in '<>-' and line[0] != ' ':
                    for syntax in expand(line.strip(), section):
                        rows.append({'syntax': syntax, 'summary': title, 'details': details,
                                     'path': rel, 'line': at + 1, 'mod': mod})
    return rows


def builds(repo):
    """[sha7, UTC commit date] for each first-parent commit since the first catalogued build, oldest first."""
    if subprocess.run(['git', '-C', str(repo), 'cat-file', '-e', FIRST_BUILD + '^{commit}']).returncode:
        raise SystemExit(f'{FIRST_BUILD} not in history; check out with full depth')
    out = subprocess.run(['git', '-C', str(repo), 'log', '--first-parent', '--reverse', '--abbrev=7',
                          '--date=format-local:%Y-%m-%d', '--format=%h %cd', 'HEAD', '--not', f'{FIRST_BUILD}^@'],
                         capture_output=True, text=True, env={**os.environ, 'TZ': 'UTC'}).stdout
    return [line.split() for line in out.splitlines()]


def lifespan(present):
    """(first, end) of the last unbroken run of builds that have the command; end is None while it still exists."""
    last = max(i for i, p in enumerate(present) if p)
    first = last
    while first and present[first - 1]:
        first -= 1
    return first, None if last == len(present) - 1 else last + 1


def command_words(syntax):
    """The command without its arguments, as release notes pair a changed signature."""
    return ' '.join(w for w in syntax.split() if not w.startswith(('<', '[', '-')))


def catalog_history(repo):
    """Syntax -> newest committed mobmesh.json entry, so a retired command keeps its curated help."""
    rel = str(CATALOG / 'mobmesh.json')
    seen = {}
    for rev in git(repo, 'log', '--format=%H', '--', rel).split():
        for entry in json.loads(git(repo, 'show', f'{rev}:{rel}') or '[]'):
            seen.setdefault(entry['syntax'], entry)
    return seen


def stamp(repo, mods, manifest, worktree=False):
    """Mod entries with since/until/replacedBy and retired commands appended, the manifest with builds, and a report."""
    history = builds(repo)
    current = {r['syntax'] for r in mod_usages(repo, worktree)}
    present = {}
    for index, (sha, _) in enumerate(history):
        syntaxes = current if index == len(history) - 1 else {r['syntax'] for r in mod_usages(repo, False, sha)}
        for syntax in syntaxes:
            present.setdefault(syntax, [False] * len(history))[index] = True
    replacements = {command_words(s): s for s in current}
    report = {'retired': [], 'uncurated': []}

    def dated(entry):
        entry = {k: v for k, v in entry.items() if k not in ('since', 'until')}
        if entry['syntax'] not in present:
            return entry
        first, end = lifespan(present[entry['syntax']])
        if first:
            entry['since'] = history[first][0]
        if end is not None:
            entry['until'] = history[end][0]
            if 'replacedBy' not in entry and replacements.get(command_words(entry['syntax'])):
                entry['replacedBy'] = replacements[command_words(entry['syntax'])]
            report['retired'].append(entry['syntax'] + (f" -> {entry['replacedBy']}" if 'replacedBy' in entry else ''))
        return entry

    out = [dated(e) for e in mods]
    known = {e['syntax'] for e in mods}
    # A command gone from cli-additions is retired, not dropped, so older builds still complete and explain it.
    curated = catalog_history(repo)
    for syntax in sorted(s for s in present if s not in current and s not in known):
        if syntax in curated:
            out.append(dated(curated[syntax]))
        else:
            report['uncurated'].append(syntax)
    return out, {**manifest, 'builds': history}, report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--catalog', type=Path, default=REPO / CATALOG, help='catalog directory to stamp in place')
    parser.add_argument('--worktree', action='store_true', help='read uncommitted cli-additions edits')
    args = parser.parse_args()
    mods = json.loads((args.catalog / 'mobmesh.json').read_text())
    manifest = json.loads((args.catalog / 'manifest.json').read_text())
    mods, manifest, report = stamp(REPO, mods, manifest, args.worktree)
    for name, data in (('mobmesh.json', mods), ('manifest.json', manifest)):
        (args.catalog / name).write_text(json.dumps(data, indent=2, ensure_ascii=False) + '\n')
    print(f"stamped {len(manifest['builds'])} builds, newest {manifest['builds'][-1][0]}")
    for line in report['retired']:
        print('retired: ' + line)
    for line in report['uncurated']:
        print('retired without curated help (left out): ' + line)
    return 0


if __name__ == '__main__':
    sys.exit(main())
