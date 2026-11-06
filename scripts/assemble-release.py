#!/usr/bin/env python3
"""Assemble successful matrix outputs into one web-flasher and release update."""

import argparse
import hashlib
import json
import shutil
import sys
from collections import defaultdict
from pathlib import Path


REQUIRED_FILES = (
    "app.bin",
    "merged.bin",
    "bootloader.bin",
    "partitions.bin",
    "boot_app0.bin",
    "board-entry.json",
    "release-notes.md",
)
# nRF52 builds ship a DFU package and their flasher entry; they have no boot files.
NRF52_REQUIRED_FILES = ("app.zip", "board-entry.json", "release-notes.md")


def digest(path: Path) -> str:
    value = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(65536), b""):
            value.update(block)
    return value.hexdigest()


def same_file(left: Path, right: Path) -> bool:
    return left.is_file() and right.is_file() and digest(left) == digest(right)


def load_bundles(inputs: Path) -> list[dict]:
    bundles = []
    seen = set()
    if not inputs.exists():
        return bundles
    for meta_path in sorted(inputs.glob("*/meta.json")):
        root = meta_path.parent
        meta = json.loads(meta_path.read_text())
        key = (meta["board"], meta["role"])
        if key in seen:
            raise ValueError(f"duplicate release input for {key[0]}/{key[1]}")
        seen.add(key)
        meta.setdefault("platform", "esp32")
        required = NRF52_REQUIRED_FILES if meta["platform"] == "nrf52" else REQUIRED_FILES
        missing = [name for name in required if not (root / name).is_file()]
        if missing:
            raise ValueError(f"{root} is missing {', '.join(missing)}")
        meta["root"] = root
        board_entry = json.loads((root / "board-entry.json").read_text())
        if not isinstance(board_entry, dict):
            raise ValueError(f"{root}/board-entry.json is not an object")
        meta["board_entry"] = board_entry
        bundles.append(meta)
    return bundles


def public_meta(bundle: dict) -> dict:
    return {key: value for key, value in bundle.items() if key not in {"root", "board_entry"}}


def common_board_fields(entry: dict) -> dict:
    return {key: value for key, value in entry.items() if key != "variants"}


def check_board_entries(board: str, bundles: list[dict]) -> None:
    expected = common_board_fields(bundles[0]["board_entry"])
    for bundle in bundles[1:]:
        if common_board_fields(bundle["board_entry"]) != expected:
            raise ValueError(f"inconsistent board metadata for {board}")
    for bundle in bundles:
        role = bundle["role"]
        if role not in bundle["board_entry"].get("variants", {}):
            raise ValueError(f"{board}/{role} board entry has no matching variant")


def check_release_groups(bundles: list[dict]) -> None:
    groups = defaultdict(list)
    for bundle in bundles:
        groups[bundle["release_tag"]].append(bundle)
    for tag, members in groups.items():
        titles = {member["release_title"] for member in members}
        latest = {member["make_latest"] for member in members}
        notes = {digest(member["root"] / "release-notes.md") for member in members}
        assets = [
            name
            for member in members
            for name in (member["app_asset"], member.get("merged_asset"))
            if name
        ]
        if len(titles) != 1 or len(latest) != 1 or len(notes) != 1:
            raise ValueError(f"inconsistent release metadata for {tag}")
        if len(assets) != len(set(assets)):
            raise ValueError(f"duplicate asset name in release {tag}")


def copy_release_assets(bundle: dict, output: Path) -> dict:
    bundle_output = output / f"{bundle['board']}-{bundle['role']}"
    bundle_output.mkdir(parents=True, exist_ok=True)
    if bundle["platform"] == "nrf52":
        name_map = {"app.zip": bundle["app_asset"]}
    else:
        name_map = {
            "app.bin": bundle["app_asset"],
            "merged.bin": bundle["merged_asset"],
        }
    copied = []
    for source_name, target_name in name_map.items():
        target = bundle_output / target_name
        shutil.copy2(bundle["root"] / source_name, target)
        copied.append(str(target))
    notes = bundle_output / "release-notes.md"
    shutil.copy2(bundle["root"] / "release-notes.md", notes)
    return {**public_meta(bundle), "release_files": copied, "notes_file": str(notes)}


def publish_esp32(bundles: list[dict], boards: dict, flasher: Path, summary: dict) -> None:
    grouped = defaultdict(list)
    for bundle in bundles:
        grouped[bundle["board"]].append(bundle)

    app0_hashes = {digest(bundle["root"] / "boot_app0.bin") for bundle in bundles}
    if len(app0_hashes) != 1:
        raise ValueError("successful builds produced different boot_app0.bin files")
    built_app0 = bundles[0]["root"] / "boot_app0.bin"
    app0_path = flasher / "bin/boot/boot_app0.bin"
    # nRF52 boards carry no boot files, so a boot_app0 change never needs them rebuilt.
    all_existing = {
        (board, role)
        for board, entry in boards.items()
        if not board.startswith("_") and entry.get("platform") != "nrf52"
        for role in entry.get("variants", {})
    }
    plans = []
    for board, board_bundles in sorted(grouped.items()):
        check_board_entries(board, board_bundles)
        owner = next((item for item in board_bundles if item["vendor_flasher_assets"]), None)
        existing = boards.get(board)
        if owner:
            source = owner["root"]
        elif existing:
            source = None
        else:
            summary["web_skipped"].append({"board": board, "reason": "owner build missing"})
            continue

        boot_path = flasher / f"bin/boot/{board}_bootloader.bin"
        partitions_path = flasher / f"bin/boot/{board}_partitions.bin"
        candidate_boot = source / "bootloader.bin" if source else boot_path
        candidate_partitions = source / "partitions.bin" if source else partitions_path
        if not candidate_boot.is_file() or not candidate_partitions.is_file():
            summary["web_skipped"].append({"board": board, "reason": "no trusted boot files"})
            continue
        if any(
            not same_file(bundle["root"] / "bootloader.bin", candidate_boot)
            or not same_file(bundle["root"] / "partitions.bin", candidate_partitions)
            for bundle in board_bundles
        ):
            summary["web_skipped"].append({"board": board, "reason": "role boot files differ"})
            continue
        boot_changes = not same_file(candidate_boot, boot_path) or not same_file(
            candidate_partitions, partitions_path
        )
        existing_roles = set(existing.get("variants", {})) if existing else set()
        built_roles = {bundle["role"] for bundle in board_bundles}
        if boot_changes and not existing_roles <= built_roles:
            summary["web_skipped"].append(
                {"board": board, "reason": "boot files changed while a retained role was not rebuilt"}
            )
            continue

        plans.append(
            (board, board_bundles, existing, boot_path, partitions_path,
             candidate_boot, candidate_partitions)
        )

    app0_changes = not same_file(built_app0, app0_path)
    planned_keys = {
        (board, bundle["role"])
        for board, board_bundles, *_ in plans
        for bundle in board_bundles
    }
    if app0_changes and not all_existing <= planned_keys:
        for board, *_ in plans:
            summary["web_skipped"].append(
                {"board": board, "reason": "boot_app0 change needs every retained variant"}
            )
        plans = []
    elif app0_changes:
        app0_path.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(built_app0, app0_path)

    for (board, board_bundles, existing, boot_path, partitions_path,
         candidate_boot, candidate_partitions) in plans:
        boot_path.parent.mkdir(parents=True, exist_ok=True)
        if candidate_boot != boot_path:
            shutil.copy2(candidate_boot, boot_path)
        if candidate_partitions != partitions_path:
            shutil.copy2(candidate_partitions, partitions_path)
        new_entry = dict(common_board_fields(board_bundles[0]["board_entry"]))
        new_entry["variants"] = dict(existing.get("variants", {})) if existing else {}
        for bundle in board_bundles:
            role = bundle["role"]
            new_entry["variants"][role] = bundle["board_entry"]["variants"][role]
            firmware = flasher / bundle["firmware_path"]
            firmware.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(bundle["root"] / "app.bin", firmware)
            summary["web_published"].append({"board": board, "role": role})
        boards[board] = new_entry


def publish_nrf52(bundles: list[dict], boards: dict, flasher: Path, summary: dict) -> None:
    """An nRF52 board has no boot files: its DFU package goes to the flasher and its entry into the catalogue."""
    grouped = defaultdict(list)
    for bundle in bundles:
        grouped[bundle["board"]].append(bundle)
    for board, board_bundles in sorted(grouped.items()):
        check_board_entries(board, board_bundles)
        if board_bundles[0]["board_entry"].get("platform") != "nrf52":
            raise ValueError(f"{board} ships an nRF52 package but its flasher entry is not nRF52")
        existing = boards.get(board)
        if existing and existing.get("platform") != "nrf52":
            raise ValueError(f"{board} is already an ESP32 board in the flasher catalogue")
        new_entry = dict(common_board_fields(board_bundles[0]["board_entry"]))
        new_entry["variants"] = dict(existing.get("variants", {})) if existing else {}
        for bundle in board_bundles:
            role = bundle["role"]
            new_entry["variants"][role] = bundle["board_entry"]["variants"][role]
            firmware = flasher / bundle["firmware_path"]
            firmware.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(bundle["root"] / "app.zip", firmware)
            summary["web_published"].append({"board": board, "role": role})
        boards[board] = new_entry


def assemble(inputs: Path, flasher: Path, boards_path: Path, output: Path) -> dict:
    bundles = load_bundles(inputs)
    check_release_groups(bundles)
    output.mkdir(parents=True, exist_ok=True)
    release_records = [copy_release_assets(bundle, output) for bundle in bundles]
    summary = {"releases": release_records, "web_published": [], "web_skipped": []}
    if not bundles:
        return summary

    boards = json.loads(boards_path.read_text())
    esp32 = [bundle for bundle in bundles if bundle["platform"] != "nrf52"]
    if esp32:
        publish_esp32(esp32, boards, flasher, summary)
    publish_nrf52([bundle for bundle in bundles if bundle["platform"] == "nrf52"], boards, flasher, summary)

    latest_versions = {bundle["version"] for bundle in bundles if bundle["make_latest"]}
    if len(latest_versions) > 1:
        raise ValueError("latest release inputs disagree on the displayed version")
    if latest_versions:
        boards["_version"] = latest_versions.pop()
    boards_path.write_text(json.dumps(boards, indent=2) + "\n")
    return summary


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--inputs", type=Path, required=True)
    parser.add_argument("--flasher-dir", type=Path, required=True)
    parser.add_argument("--boards-json", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--summary", type=Path, required=True)
    args = parser.parse_args()
    try:
        summary = assemble(
            args.inputs, args.flasher_dir, args.boards_json, args.output_dir
        )
        args.summary.write_text(json.dumps(summary, indent=2) + "\n")
    except (KeyError, OSError, ValueError, json.JSONDecodeError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
