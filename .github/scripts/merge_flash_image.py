#!/usr/bin/env python3
# Builds the whole-flash -merged.bin published alongside the app-only image: one file a
# user writes at offset 0 to a blank board, rather than four written at four offsets.
#
# Chip and flash size are read out of the built images' own headers rather than declared
# in config, so they cannot drift from what was actually built. esptool rewrites the flash
# size into the bootloader header only -- the app is copied through untouched, so its
# appended SHA-256 stays valid whatever size is passed.

import argparse
import json
import re
import shutil
import subprocess
import sys
from pathlib import Path

# esp_image_header_t: byte 3's high nibble is the flash size, bytes 12-13 the chip id.
CHIP_IDS = {0: "esp32", 2: "esp32s2", 5: "esp32c3", 9: "esp32s3", 12: "esp32c6"}
FLASH_SIZES = {0: "1MB", 1: "2MB", 2: "4MB", 3: "8MB", 4: "16MB", 5: "32MB"}


def read_image_header(path):
    with path.open("rb") as f:
        head = f.read(16)
    if len(head) < 16 or head[0] != 0xE9:
        sys.exit(f"error: {path} is not an ESP32 image (magic {head[:1].hex()}, expected e9)")
    size_code = head[3] >> 4
    chip_code = int.from_bytes(head[12:14], "little")
    if size_code not in FLASH_SIZES:
        sys.exit(f"error: {path} declares unknown flash size code {size_code}")
    if chip_code not in CHIP_IDS:
        sys.exit(f"error: {path} declares unknown chip id {chip_code}")
    return CHIP_IDS[chip_code], FLASH_SIZES[size_code]


def find_esptool():
    if shutil.which("esptool"):
        return ["esptool"]
    if shutil.which("esptool.py"):
        return ["esptool.py"]
    probe = subprocess.run([sys.executable, "-m", "esptool", "version"],
                           capture_output=True, text=True)
    if probe.returncode == 0:
        return [sys.executable, "-m", "esptool"]
    sys.exit("error: esptool not found -- pip install esptool")


def merge(chip, flash_size, parts, out_path):
    cmd = find_esptool() + ["--chip", chip, "merge-bin",
                            "--flash-size", flash_size, "-o", str(out_path)]
    for offset, path in parts:
        cmd += [hex(offset), str(path)]
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        sys.exit(f"error: esptool merge-bin failed\n{result.stdout}\n{result.stderr}")


def verify(out_path, parts, flash_size):
    """The merged file must carry each part at its offset and end at the last byte written.
    esptool rewrites byte 3 of the bootloader header to the flash size and copies every
    other part through unchanged, so the app keeps its appended SHA-256."""
    data = out_path.read_bytes()
    problems = []

    expected_size_code = next(k for k, v in FLASH_SIZES.items() if v == flash_size)
    for offset, path in parts:
        source = path.read_bytes()
        chunk = data[offset:offset + len(source)]
        if len(chunk) < len(source):
            problems.append(f"{path.name} does not fit at {offset:#x}")
            continue
        if offset == 0:
            if chunk[3] >> 4 != expected_size_code:
                problems.append(f"bootloader was not patched to {flash_size}")
            if chunk[:3] != source[:3] or chunk[4:] != source[4:]:
                problems.append(f"{path.name} differs from its source beyond the flash-size byte")
        elif chunk != source:
            problems.append(f"{path.name} at {offset:#x} differs from its source")

    last_written = max(off + len(p.read_bytes()) for off, p in parts)
    if len(data) != last_written:
        problems.append(f"file is {len(data)} bytes, expected {last_written} "
                        f"-- padding or truncation past the last part")

    if problems:
        print(f"FAIL ({out_path.name}):")
        for p in problems:
            print(f"  - {p}")
        sys.exit(1)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--board-id", required=True)
    parser.add_argument("--variant", required=True)
    parser.add_argument("--boards-json", type=Path, required=True)
    parser.add_argument("--flasher-dir", type=Path, required=True)
    parser.add_argument("--firmware", type=Path, required=True,
                        help="app image to merge, after its OTA metadata has been written")
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()

    boards = json.loads(args.boards_json.read_text())
    board = boards[args.board_id]
    offsets = {k: int(v, 16) for k, v in board["offsets"].items()}

    parts = [
        (offsets["bootloader"], args.flasher_dir / board["bootloaderFile"]),
        (offsets["partitions"], args.flasher_dir / board["partitionsFile"]),
        (offsets["otadata"], args.flasher_dir / board["bootApp0"]),
        (offsets["app0"], args.firmware),
    ]
    for _, path in parts:
        if not path.exists():
            sys.exit(f"error: missing {path}")

    chip, flash_size = read_image_header(args.firmware)
    boot_chip, boot_flash = read_image_header(parts[0][1])
    if (chip, flash_size) != (boot_chip, boot_flash):
        sys.exit(f"error: app declares {chip}/{flash_size} but bootloader declares "
                 f"{boot_chip}/{boot_flash} -- these were not built for the same board")

    merge(chip, flash_size, parts, args.out)
    verify(args.out, parts, flash_size)

    print(f"OK: {args.out.name} -- {chip}, {flash_size} flash, "
          f"{args.out.stat().st_size:,} bytes")


if __name__ == "__main__":
    main()
