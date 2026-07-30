#!/usr/bin/env python3
"""
Single source of truth for per-board config, split two ways:

  - "flags"       prints the exact `-D KEY=VALUE` (and, if set,
                   `board_build.partitions = ...`) lines that should exist in
                   a board's variant platformio.ini, derived from
                   variants/<board>/overrides.yaml. Used both to help a human
                   author/update a mod's patch, and by CI to verify the lines
                   actually present in the patch haven't drifted from
                   overrides.yaml.

  - "boards-json"  merges upstream's boards/<board>.json (mcu, flash size,
                   USB hwids, psram -- read directly, never duplicated),
                   variants/<board>/overrides.yaml's `flasher:` facts, and
                   partition byte offsets parsed from an actual built
                   partitions.bin (never hand-entered), into
                   pages/flasher/boards.json. Only the given board/variant
                   entry is touched; everything else in the file is
                   preserved as-is.

Requires PyYAML (pip install pyyaml).
"""
import argparse
import json
import struct
import sys
from pathlib import Path

try:
    import yaml
except ImportError:
    sys.exit("error: PyYAML is required (pip install pyyaml)")

REPO_ROOT = Path(__file__).resolve().parent.parent

# Bootloader isn't part of the partition table itself -- its flash offset is
# a fixed constant per chip family (ESP-IDF/Arduino convention), not encoded
# in partitions.bin.
BOOTLOADER_OFFSET_BY_MCU_PREFIX = {
    "esp32": "0x1000",     # original ESP32 only
    "esp32s2": "0x0",
    "esp32s3": "0x0",
    "esp32c3": "0x0",
    "esp32c6": "0x0",
    "esp32h2": "0x0",
}
# The partition table's own flash offset is always this fixed value by
# convention -- also not encoded inside partitions.bin.
PARTITION_TABLE_OFFSET = "0x8000"

PARTITION_ENTRY_SIZE = 32
PARTITION_MAGIC = b"\xAA\x50"
PARTITION_TYPE_APP = 0x00
PARTITION_TYPE_DATA = 0x01
APP_SUBTYPE_OTA_0 = 0x10
APP_SUBTYPE_OTA_1 = 0x11
DATA_SUBTYPE_OTA = 0x00  # "otadata"


def bootloader_offset_for_mcu(mcu: str) -> str:
    key = mcu.lower()
    if key not in BOOTLOADER_OFFSET_BY_MCU_PREFIX:
        raise ValueError(
            f"unknown mcu '{mcu}' -- add its bootloader flash offset to "
            f"BOOTLOADER_OFFSET_BY_MCU_PREFIX in {Path(__file__).name}"
        )
    return BOOTLOADER_OFFSET_BY_MCU_PREFIX[key]


def parse_partitions_bin(path: Path) -> dict:
    """Parse an ESP32 partition table binary, return offsets/sizes we need."""
    data = path.read_bytes()
    found = {}
    for i in range(0, len(data), PARTITION_ENTRY_SIZE):
        entry = data[i : i + PARTITION_ENTRY_SIZE]
        if len(entry) < PARTITION_ENTRY_SIZE or entry[0:2] != PARTITION_MAGIC:
            break  # end-of-table marker or padding
        ptype, subtype = entry[2], entry[3]
        offset, size = struct.unpack_from("<II", entry, 4)
        if ptype == PARTITION_TYPE_DATA and subtype == DATA_SUBTYPE_OTA:
            found["otadata"] = offset
        elif ptype == PARTITION_TYPE_APP and subtype == APP_SUBTYPE_OTA_0:
            found["app0"] = offset
            found["app0_size"] = size
        elif ptype == PARTITION_TYPE_APP and subtype == APP_SUBTYPE_OTA_1:
            found["app1"] = offset

    missing = {"otadata", "app0", "app1"} - found.keys()
    if missing:
        raise ValueError(
            f"partitions.bin at {path} is missing expected partition(s): {sorted(missing)} "
            "-- expected an otadata + ota_0 + ota_1 (dual-OTA) partition scheme"
        )
    return found


def load_overrides(board: str) -> dict:
    path = REPO_ROOT / "variants" / board / "overrides.yaml"
    if not path.exists():
        raise FileNotFoundError(f"no overrides file for board '{board}': {path}")
    with path.open() as f:
        return yaml.safe_load(f) or {}


def load_upstream_board_json(upstream_dir: Path, board: str) -> dict:
    path = upstream_dir / "boards" / f"{board}.json"
    if not path.exists():
        raise FileNotFoundError(f"upstream board definition not found: {path}")
    with path.open() as f:
        return json.load(f)


def cmd_flags(args):
    overrides = load_overrides(args.board)
    lines = []
    for key, value in (overrides.get("build_flags") or {}).items():
        if isinstance(value, str):
            lines.append(f'-D {key}=\'"{value}"\'')
        else:
            lines.append(f"-D {key}={value}")

    partitions_override = overrides.get("partitions_override")
    if partitions_override:
        lines.append(f"board_build.partitions = {partitions_override}")

    output = "\n".join(lines)
    if args.out:
        Path(args.out).write_text(output + "\n")
    else:
        print(output)


def cmd_verify_flags(args):
    """Fail if any expected line (per overrides.yaml) is missing from the patch file's text."""
    overrides = load_overrides(args.board)
    patch_text = Path(args.patch).read_text()

    expected = []
    for key, value in (overrides.get("build_flags") or {}).items():
        expected.append(f"-D {key}=")
    if overrides.get("partitions_override"):
        expected.append("board_build.partitions =")

    missing = [e for e in expected if e not in patch_text]
    if missing:
        sys.exit(
            f"error: {args.patch} is missing build-flag line(s) expected from "
            f"variants/{args.board}/overrides.yaml: {missing}\n"
            f"Run 'scripts/generate-board-config.py flags --board {args.board}' "
            "and update the patch's platformio.ini section to match."
        )
    print(f"OK: all expected build-flag lines present in {args.patch}")


def cmd_boards_json(args):
    overrides = load_overrides(args.board)
    upstream = load_upstream_board_json(Path(args.upstream_dir), args.board)
    partitions = parse_partitions_bin(Path(args.partitions_bin))

    build = upstream.get("build", {})
    mcu = build.get("mcu")
    if not mcu:
        raise ValueError(f"upstream boards/{args.board}.json has no build.mcu field")

    board_entry = {
        "label": overrides["flasher"]["label"],
        "connectNote": overrides["flasher"]["connect_note"],
        "postFlashNote": overrides["flasher"]["post_flash_note"],
        "flashMode": build.get("flash_mode"),
        "flashFreq": f"{int(build.get('f_flash', '0').rstrip('L')) // 1_000_000}m",
        "flashSize": upstream.get("upload", {}).get("flash_size"),
        "offsets": {
            "bootloader": bootloader_offset_for_mcu(mcu),
            "partitions": PARTITION_TABLE_OFFSET,
            "otadata": hex(partitions["otadata"]),
            "app0": hex(partitions["app0"]),
            "app1": hex(partitions["app1"]),
            "appMaxSize": hex(partitions["app0_size"]),
        },
    }

    output_path = Path(args.output)
    all_boards = json.loads(output_path.read_text()) if output_path.exists() else {}
    existing = all_boards.get(args.board, {})
    existing_variants = existing.get("variants", {})

    board_entry["variants"] = existing_variants
    board_entry["variants"][args.variant_id] = {
        "label": args.variant_label,
        "assetBasename": args.asset_basename,
        "firmwareFile": args.firmware_file,
        "firmwareShaFile": args.firmware_sha_file,
    }
    board_entry["bootApp0"] = existing.get("bootApp0", f"{args.board}/boot_app0.bin")
    board_entry["bootloaderFile"] = existing.get("bootloaderFile", f"{args.board}/bootloader.bin")
    board_entry["partitionsFile"] = existing.get("partitionsFile", f"{args.board}/partitions.bin")

    all_boards[args.board] = board_entry
    output_path.write_text(json.dumps(all_boards, indent=2) + "\n")
    print(f"OK: updated '{args.board}' ({args.variant_id}) in {output_path}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)

    p_flags = sub.add_parser("flags", help="print build-flag lines for a board's overrides")
    p_flags.add_argument("--board", required=True)
    p_flags.add_argument("--out", help="write to this file instead of stdout")
    p_flags.set_defaults(func=cmd_flags)

    p_verify = sub.add_parser("verify-flags", help="check a patch file contains overrides.yaml's expected lines")
    p_verify.add_argument("--board", required=True)
    p_verify.add_argument("--patch", required=True)
    p_verify.set_defaults(func=cmd_verify_flags)

    p_bj = sub.add_parser("boards-json", help="regenerate a board/variant entry in pages/flasher/boards.json")
    p_bj.add_argument("--board", required=True)
    p_bj.add_argument("--upstream-dir", required=True)
    p_bj.add_argument("--partitions-bin", required=True)
    p_bj.add_argument("--variant-id", required=True)
    p_bj.add_argument("--variant-label", required=True)
    p_bj.add_argument("--asset-basename", required=True)
    p_bj.add_argument("--firmware-file", required=True)
    p_bj.add_argument("--firmware-sha-file", required=True)
    p_bj.add_argument("--output", required=True)
    p_bj.set_defaults(func=cmd_boards_json)

    args = parser.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
