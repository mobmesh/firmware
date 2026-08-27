#!/usr/bin/env bash
#
# Rebase mods/*/patches/*.patch from one upstream ref onto another.
#
# Hand-editing 30+ hunks across a dozen files is how this used to go. Instead we
# replay each patch as a commit on the OLD ref -- where it applies by definition --
# and let git's three-way merge machinery move the commits to the NEW ref. Conflicts
# then arrive as normal rebase conflicts with real context, not as "does not apply".
#
#   ./scripts/rebase-patches.sh start repeater-v1.16.0 repeater-v1.17.0
#     ... resolve conflicts, git -C <workdir> rebase --continue, repeat ...
#   ./scripts/rebase-patches.sh finish
#
# `finish` rewrites mods/*/patches/*.patch in place from the rebased commits.
# It does NOT build. Always build all envs before committing.
set -euo pipefail

UPSTREAM_REPO="${UPSTREAM_REPO:-meshcore-dev/MeshCore}"
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WORKDIR="${WORKDIR:-${TMPDIR:-/tmp}/mobmesh-patch-rebase}"
STATE="$WORKDIR/.rebase-state"

# Same order CI applies them in: mods in build-targets.yaml order (union across
# targets, first appearance wins), then each mod's patches sorted by filename.
patch_list() {
  python3 - "$REPO_ROOT" <<'PY'
import sys, glob, os, yaml
root = sys.argv[1]
data = yaml.safe_load(open(os.path.join(root, "build-targets.yaml")))
mods = list(data.get("core_mods") or [])
for t in data["targets"]:
    for m in (t.get("mods") or []):
        if m not in mods:
            mods.append(m)
for m in mods:
    for p in sorted(glob.glob(os.path.join(root, "mods", m, "patches", "*.patch"))):
        # path relative to mods/, so "mods/$name" resolves and the commit subject
        # round-trips back to the same file in `finish`
        print(f"{m}/patches/{os.path.basename(p)}")
PY
}

cmd_start() {
  local old="${1:?usage: start <old-ref> <new-ref>}" new="${2:?usage: start <old-ref> <new-ref>}"

  rm -rf "$WORKDIR"
  echo "==> cloning $UPSTREAM_REPO"
  git clone --quiet --depth 50 "https://github.com/${UPSTREAM_REPO}.git" "$WORKDIR"
  git -C "$WORKDIR" fetch --quiet --depth 50 origin "tag" "$old" "tag" "$new" 2>/dev/null || true
  git -C "$WORKDIR" config user.email "patch-rebase@localhost"
  git -C "$WORKDIR" config user.name "patch-rebase"

  echo "==> replaying patches as commits on $old"
  git -C "$WORKDIR" checkout --quiet "$old"
  git -C "$WORKDIR" checkout --quiet -B mods
  while read -r name; do
    if ! git -C "$WORKDIR" apply "$REPO_ROOT/mods/$name"; then
      echo "!! $name does not apply to $old -- is $old really the ref these patches are current for?" >&2
      exit 1
    fi
    git -C "$WORKDIR" add -A
    git -C "$WORKDIR" commit --quiet -m "$name"
    echo "    committed $name"
  done < <(patch_list)

  printf 'old=%s\nnew=%s\n' "$old" "$new" > "$STATE"

  echo "==> rebasing onto $new"
  if git -C "$WORKDIR" rebase --onto "$new" "$old" mods; then
    echo
    echo "Clean rebase. Now run:  $0 finish"
  else
    cat <<EOF

Conflicts. Resolve them in:
    $WORKDIR

then:  git -C "$WORKDIR" add -A && git -C "$WORKDIR" rebase --continue
(repeat until the rebase completes), then:  $0 finish

Reminder: a conflict is a question about intent, not just text. Upstream may have
fixed the thing a hunk exists to fix, or rewritten the code it patches -- in which
case the hunk should be DROPPED, not merged. Read the surrounding code.
EOF
    exit 1
  fi
}

cmd_finish() {
  [ -f "$STATE" ] || { echo "no rebase in progress under $WORKDIR (run 'start' first)" >&2; exit 1; }
  # shellcheck disable=SC1090
  source "$STATE"

  if [ -d "$WORKDIR/.git/rebase-merge" ] || [ -d "$WORKDIR/.git/rebase-apply" ]; then
    echo "rebase still in progress in $WORKDIR -- finish it first" >&2
    exit 1
  fi

  echo "==> regenerating patch files from rebased commits"
  local n=0
  while read -r sha subject; do
    [ -f "$REPO_ROOT/mods/$subject" ] || { echo "!! commit '$subject' has no matching patch file" >&2; exit 1; }
    git -C "$WORKDIR" diff "$sha~1" "$sha" > "$REPO_ROOT/mods/$subject"
    echo "    wrote mods/$subject"
    n=$((n + 1))
  done < <(git -C "$WORKDIR" log --reverse --format='%H %s' "$new..mods")

  echo
  echo "Regenerated $n patch file(s) against $new."
  echo "NOT verified: build every env in build-targets.yaml before committing."
}

case "${1:-}" in
  start)  shift; cmd_start "$@" ;;
  finish) shift; cmd_finish "$@" ;;
  *) echo "usage: $0 start <old-ref> <new-ref> | $0 finish" >&2; exit 2 ;;
esac
