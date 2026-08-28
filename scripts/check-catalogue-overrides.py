#!/usr/bin/env python3
"""Report override keys that no longer match upstream's catalogue.

Overrides key on the maker slug and the exact device name, the only identities
mc_config.json gives. An upstream rename drops the override silently, so this turns
that into a visible failure.
"""
import json
import pathlib
import re
import urllib.error
import urllib.request
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
CATALOGUE = ROOT / "pages/flasher/mc_config.json"
OVERRIDES = ROOT / "pages/flasher2/data/catalogue-overrides.json"

REMOTE_RE = re.compile(r"^(?:[a-z][a-z0-9+.-]*:)?//", re.I)
REMOTE_TIMEOUT_S = 4


def remote_status(url):
    """None if the URL serves, else why not. A protocol-relative URL is fetched as https."""
    if url.startswith("//"):
        url = "https:" + url
    req = urllib.request.Request(url, method="HEAD")
    try:
        with urllib.request.urlopen(req, timeout=REMOTE_TIMEOUT_S) as r:
            return None if r.status < 400 else f"HTTP {r.status}"
    except urllib.error.HTTPError as e:
        return f"HTTP {e.code}"
    except Exception as e:  # DNS, TLS, timeout -- offline is not a missing asset
        return f"unreachable ({type(e).__name__})"

# Kept in step with IMAGE_FILTERS in flash-plan.js.
FILTERS = {"invert", "white", "black"}


def orphaned(catalogue, overrides):
    """Override keys upstream no longer has, as (device names, maker keys)."""
    devices = {d["name"] for d in catalogue.get("device", []) if d.get("name")}
    makers = {d["maker"] for d in catalogue.get("device", []) if d.get("maker")}
    return ([name for name in overrides.get("devices", {}) if name not in devices],
            [key for key in overrides.get("makers", {}) if key not in makers])


def orphaned_hides(catalogue, overrides):
    """Orphans carrying hidden:true. These put a deliberately removed device back in
    the catalogue, so they are the one override failure that is not cosmetic."""
    dead, _ = orphaned(catalogue, overrides)
    return [n for n in dead if overrides["devices"][n].get("hidden") is True]


def main(check_remote=remote_status) -> int:
    catalogue = json.loads(CATALOGUE.read_text())
    overrides = json.loads(OVERRIDES.read_text())

    dead_devices, dead_makers = orphaned(catalogue, overrides)
    orphans = [f"device {name!r}" for name in dead_devices]
    orphans += [f"maker {key!r}" for key in dead_makers]

    # Art resolves with new URL(path, base), so an override may point off-site. Local paths
    # are stat'd, remote ones fetched; either way a dead one falls back to placeholder art.
    missing = []
    bad_filters = []
    for scope in ("makers", "devices"):
        for key, entry in overrides.get(scope, {}).items():
            for field in ("icon", "image"):
                path = entry.get(field)
                if not path:
                    continue
                if REMOTE_RE.match(path):
                    why = check_remote(path)
                    if why:
                        missing.append(f"{scope[:-1]} {key!r} {field}: {path} -- {why}")
                elif not (OVERRIDES.parent / path).exists():
                    missing.append(f"{scope[:-1]} {key!r} {field}: {path}")
            name = entry.get("filter")
            if name is not None and name not in FILTERS:
                bad_filters.append(f"{scope[:-1]} {key!r} filter: {name!r}")
            dim = entry.get("dim")
            if dim is not None and not isinstance(dim, bool):
                bad_filters.append(f"{scope[:-1]} {key!r} dim: {dim!r} (expected true/false)")

    for line in orphans:
        print(f"orphaned override: {line}")
    for line in missing:
        print(f"missing asset: {line}")
    for line in bad_filters:
        print(f"unknown filter: {line}")

    if orphans or missing or bad_filters:
        print(f"\n{len(orphans)} orphaned, {len(missing)} missing, {len(bad_filters)} bad filters")
        return 1
    counts = f"{len(overrides.get('makers', {}))} maker, {len(overrides.get('devices', {}))} device"
    print(f"all overrides match ({counts})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
