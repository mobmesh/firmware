#!/usr/bin/env python3
# Boots a real, unmodified release firmware image in QEMU and watches for a crash/boot loop.
# No special build required -- the ESP32 boot ROM always prints to UART0 regardless of the
# app's own console config (native USB-CDC vs UART0), so this works on the exact shipped bits.
#
# radio_init() is expected to fail in QEMU (no real LoRa hardware) and RollbackGuard will retry
# and eventually esp_restart() -- that's normal, not a bug. What this actually distinguishes is
# that controlled, expected restart path (reset reason RTC_SW_CPU_RST) from a genuine crash: a
# panic/Guru-Meditation block, or any reset reason other than POWERON/RTC_SW_CPU_RST (watchdog
# timeout, brownout, etc.), or an abnormal number of resets in the window.

import argparse
import json
import re
import select
import subprocess
import sys
import time
from pathlib import Path

RUN_SECONDS = 90
MAX_RESETS = 5
CRASH_SIGNATURES = ("Guru Meditation Error", "Backtrace:", "abort() was called")
EXPECTED_RESET_REASONS = ("POWERON", "RTC_SW_CPU_RST")


def merge_flash_image(board, variant, flasher_dir, out_path):
    offsets = {k: int(v, 16) for k, v in board["offsets"].items()}
    # 16MB regardless of the board's real flash size -- both machine models expect it,
    # and a correctly-sized image fails esp_flash_init() during IDF startup.
    flash_size = 16 * 1024 * 1024
    img = bytearray([0xFF] * flash_size)

    def load_at(rel_path, offset):
        data = (flasher_dir / rel_path).read_bytes()
        img[offset:offset + len(data)] = data

    load_at(board["bootloaderFile"], offsets["bootloader"])
    load_at(board["partitionsFile"], offsets["partitions"])
    load_at(board["bootApp0"], offsets["otadata"])
    load_at(variant["firmwareFile"], offsets["app0"])
    out_path.write_bytes(img)


def boot_and_capture(qemu_bin, machine, mem, rom_dir, flash_image):
    cmd = [
        str(qemu_bin), "-nographic", "-machine", machine,
        "-drive", f"file={flash_image},if=mtd,format=raw",
        "-L", str(rom_dir),
    ]
    if mem:
        cmd += ["-m", mem]
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    buf = b""
    start = time.time()
    while time.time() - start < RUN_SECONDS:
        r, _, _ = select.select([proc.stdout], [], [], 0.5)
        if proc.stdout in r:
            chunk = proc.stdout.read1(4096)
            if chunk:
                buf += chunk
    proc.terminate()
    try:
        proc.wait(timeout=5)
    except Exception:
        proc.kill()
    return buf.decode(errors="replace")


def evaluate(output):
    failures = []

    for sig in CRASH_SIGNATURES:
        if sig in output:
            failures.append(f"crash signature found: {sig!r}")

    reset_reasons = re.findall(r"rst:0x[0-9a-fA-F]+ \(([A-Z0-9_]+)\)", output)
    if not reset_reasons:
        failures.append("no ROM boot banner seen at all -- firmware never booted")
    if len(reset_reasons) > MAX_RESETS:
        failures.append(f"{len(reset_reasons)} resets in {RUN_SECONDS}s -- abnormally fast reset loop")
    unexpected = [r for r in reset_reasons if r not in EXPECTED_RESET_REASONS]
    if unexpected:
        failures.append(f"unexpected reset reason(s): {sorted(set(unexpected))}")

    return failures


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--board-id", required=True)
    parser.add_argument("--variant", required=True)
    parser.add_argument("--boards-json", type=Path, required=True)
    parser.add_argument("--flasher-dir", type=Path, required=True)
    parser.add_argument("--qemu-bin", type=Path, required=True)
    parser.add_argument("--rom-dir", type=Path, required=True)
    parser.add_argument("--machine", required=True)
    # Only the S3 machine takes one; the C3 has no PSRAM.
    parser.add_argument("--mem", default="")
    parser.add_argument("--workdir", type=Path, required=True)
    args = parser.parse_args()

    boards = json.loads(args.boards_json.read_text())
    board = boards[args.board_id]
    variant = board["variants"][args.variant]

    flash_image = args.workdir / "flash_image.bin"
    merge_flash_image(board, variant, args.flasher_dir, flash_image)

    output = boot_and_capture(args.qemu_bin, args.machine, args.mem,
                              args.rom_dir, flash_image)
    log_path = args.workdir / "serial_capture.log"
    log_path.write_text(output)
    print(output)

    failures = evaluate(output)
    if failures:
        print(f"\nFAIL ({args.board_id}/{args.variant}):")
        for f in failures:
            print(f"  - {f}")
        sys.exit(1)

    print(f"\nPASS ({args.board_id}/{args.variant}): booted cleanly, no crash/loop signature in {RUN_SECONDS}s")


if __name__ == "__main__":
    main()
