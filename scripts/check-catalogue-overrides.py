#!/usr/bin/env python3
"""Report override keys that no longer match upstream's catalogue.

Overrides key on the maker slug and the exact device name, the only identities
mc-config.json gives. An upstream rename drops the override silently, so this turns
that into a visible failure.
"""
import json
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
CATALOGUE = ROOT / "pages/flasher/mc-config.json"
OVERRIDES = ROOT / "pages/flasher2/data/catalogue-overrides.json"


def main() -> int:
    catalogue = json.loads(CATALOGUE.read_text())
    overrides = json.loads(OVERRIDES.read_text())

    devices = {d["name"] for d in catalogue.get("device", []) if d.get("name")}
    makers = {d["maker"] for d in catalogue.get("device", []) if d.get("maker")}

    orphans = [f"device {name!r}" for name in overrides.get("devices", {}) if name not in devices]
    orphans += [f"maker {key!r}" for key in overrides.get("makers", {}) if key not in makers]

    # Art is addressed relative to the override file; a dead path renders as a broken tile.
    missing = []
    for scope in ("makers", "devices"):
        for key, entry in overrides.get(scope, {}).items():
            for field in ("icon", "image"):
                path = entry.get(field)
                if path and not (OVERRIDES.parent / path).exists():
                    missing.append(f"{scope[:-1]} {key!r} {field}: {path}")

    for line in orphans:
        print(f"orphaned override: {line}")
    for line in missing:
        print(f"missing asset: {line}")

    if orphans or missing:
        print(f"\n{len(orphans)} orphaned, {len(missing)} missing")
        return 1
    counts = f"{len(overrides.get('makers', {}))} maker, {len(overrides.get('devices', {}))} device"
    print(f"all overrides match ({counts})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
