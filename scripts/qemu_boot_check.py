#!/usr/bin/env python3
# Boots a real, unmodified release firmware image in QEMU and holds a conversation with it.
#
# A pass means the node came up and answered: the emulator models the LoRa radio, the flash
# and the SoC's USB console, so the firmware reaches loop() and serves its CLI exactly as it
# does on hardware. Absence of a crash is not a pass -- an image that never executes an
# application instruction produces no crash either.
#
# Which commands run depends on the image. The mod bit register (mods/bit-registry.md) says
# which mods were built in, so a mod's CLI is only exercised when its bit is set.
#
# The image under test is never opened for writing: booting a node writes to its flash --
# SPIFFS, NVS, the identity it generates on first boot -- so QEMU is always pointed at a
# freshly composed copy in the workdir. The vendored artifacts are hashed before and after
# to prove it.

import argparse
import hashlib
import json
import re
import socket
import subprocess
import sys
import time
from pathlib import Path

BOOT_TIMEOUT_SECONDS = 120     # generous: an emulated boot is a few seconds, a stall is not
COMMAND_TIMEOUT_SECONDS = 30
CONSOLE_PORT = 45080
CRASH_SIGNATURES = ("Guru Meditation Error", "Backtrace:", "abort() was called")

# Mod bit register: file offset of the u32, and the bits this script knows how to exercise.
MOD_BITS_OFFSET = 272
MOD_BIT_HOTSPOT_OTA = 0x00000002

# Board wiring the emulator needs as run-time properties. These are facts about the physical
# board -- the radio's chip-select pin and the SPI controller it hangs off -- that the device
# models take as qdev properties so one binary serves every board. strap-mode selects SPI boot
# and differs per chip; the wrong value drops the ROM into download mode.
BOARD_QEMU_ARGS = {
    "heltec_v4": [
        "-global", "driver=bramble.gpio,property=strap-mode,value=4",
        "-global", "driver=bramble.gpspi2,property=cs-gpio,value=8",
        "-global", "driver=bramble.gpspi2,property=spi-base,value=0x60025000",
    ],
    "xiao_c3": [
        "-global", "driver=bramble.gpio,property=strap-mode,value=8",
        "-global", "driver=bramble.gpspi2,property=cs-gpio,value=6",
        "-global", "driver=bramble.gpspi2,property=spi-base,value=0x60024000",
    ],
}


def merge_flash_image(board, variant, flasher_dir, out_path):
    """Compose a fresh flash image from the vendored parts. Never modifies its inputs."""
    offsets = {k: int(v, 16) for k, v in board["offsets"].items()}
    # 16MB regardless of the board's real flash size. QEMU picks the emulated chip from the
    # drive size (4MB->gd25q32, 8MB->gd25q64, 16MB->is25lp128) and only the ISSI part gets past
    # esp_flash_init_default_chip() -- 4 and 8MB assert in do_core_init and boot-loop, upstream's
    # own images included. Measured 2026-08-27; the size is a chip selector, not a size check.
    flash_size = 16 * 1024 * 1024
    img = bytearray([0xFF] * flash_size)

    sources = {}

    def load_at(rel_path, offset):
        path = flasher_dir / rel_path
        data = path.read_bytes()
        sources[path] = hashlib.sha256(data).hexdigest()
        img[offset:offset + len(data)] = data

    load_at(board["bootloaderFile"], offsets["bootloader"])
    load_at(board["partitionsFile"], offsets["partitions"])
    load_at(board["bootApp0"], offsets["otadata"])
    load_at(variant["firmwareFile"], offsets["app0"])
    out_path.write_bytes(img)
    return sources


def read_mod_bits(flasher_dir, variant):
    """The u32 the build stamps into the image naming which mods are present."""
    data = (flasher_dir / variant["firmwareFile"]).read_bytes()
    if len(data) < MOD_BITS_OFFSET + 4:
        return 0
    return int.from_bytes(data[MOD_BITS_OFFSET:MOD_BITS_OFFSET + 4], "little")


class Console:
    """The SoC's USB console, which is where a hardware-CDC build puts its CLI."""

    def __init__(self, port):
        self.sock = socket.create_connection(("127.0.0.1", port), timeout=5)
        self.sock.settimeout(0.5)
        self.buf = ""

    def drain(self):
        try:
            self.buf += self.sock.recv(65536).decode("utf-8", "replace")
        except socket.timeout:
            pass
        return self.buf

    def send(self, command):
        # MeshCore's parser discards \n and completes a line only on \r.
        self.sock.sendall((command + "\r").encode())

    def ask(self, command, timeout=COMMAND_TIMEOUT_SECONDS):
        """Send a command and return its reply, or None if it never answers."""
        self.buf = ""
        self.send(command)
        deadline = time.time() + timeout
        while time.time() < deadline:
            self.drain()
            for line in self.buf.splitlines():
                if "->" in line:
                    return line.split("->", 1)[1].strip()
            time.sleep(0.2)
        return None

    def close(self):
        try:
            self.sock.close()
        except OSError:
            pass


def wait_for_cli(console, deadline):
    """Poll until the node answers, so the check measures liveness rather than elapsed time."""
    while time.time() < deadline:
        reply = console.ask("ver", timeout=5)
        if reply:
            return reply
        time.sleep(1)
    return None


def check_hotspot_ota(console, failures):
    """A setting round-trip: write it, read it back, and put it back as it was.

    ota.wan.pwr drives the WAN power-switch pin, and the get reads the pin rather than a
    cached value, so a matching read-back exercises the CLI, the setting and the GPIO.
    """
    original = console.ask("get ota.wan.pwr")
    if original is None:
        failures.append("hotspot-ota: 'get ota.wan.pwr' never answered")
        return
    for want in ("off", "on"):
        if console.ask(f"set ota.wan.pwr {want}") != "OK":
            failures.append(f"hotspot-ota: 'set ota.wan.pwr {want}' did not return OK")
            return
        got = console.ask("get ota.wan.pwr")
        if got is None or want not in got:
            failures.append(f"hotspot-ota: set {want} then read back {got!r}")
            return
    if original and "off" in original:
        console.ask("set ota.wan.pwr off")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--board-id", required=True)
    parser.add_argument("--variant", required=True)
    parser.add_argument("--boards-json", type=Path, required=True)
    parser.add_argument("--flasher-dir", type=Path, required=True)
    parser.add_argument("--qemu-bin", type=Path, required=True)
    parser.add_argument("--rom-dir", type=Path, required=True)
    parser.add_argument("--machine", required=True)
    # Only the S3 machine takes one; the C3 sets its own 400KB and wedges if given more.
    parser.add_argument("--mem", default="")
    parser.add_argument("--workdir", type=Path, required=True)
    args = parser.parse_args()

    boards = json.loads(args.boards_json.read_text())
    board = boards[args.board_id]
    variant = board["variants"][args.variant]

    args.workdir.mkdir(parents=True, exist_ok=True)
    flash_image = args.workdir / "flash_image.bin"
    sources = merge_flash_image(board, variant, args.flasher_dir, flash_image)
    mod_bits = read_mod_bits(args.flasher_dir, variant)

    uart_log = args.workdir / "uart0.log"
    cmd = [
        str(args.qemu_bin), "-display", "none", "-monitor", "none",
        "-machine", args.machine,
        "-L", str(args.rom_dir),
        "-drive", f"file={flash_image},if=mtd,format=raw",
        # Serial 0 carries the boot ROM and any panic; serial 2 is the SoC's USB console,
        # where a hardware-CDC build serves its CLI.
        "-serial", f"file:{uart_log}",
        "-serial", "null",
        "-serial", f"tcp:127.0.0.1:{CONSOLE_PORT},server=on,wait=off",
    ]
    if args.mem:
        cmd += ["-m", args.mem]
    cmd += BOARD_QEMU_ARGS.get(args.board_id, [])

    proc = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    failures = []
    console = None
    try:
        time.sleep(2)
        console = Console(CONSOLE_PORT)
        deadline = time.time() + BOOT_TIMEOUT_SECONDS
        version = wait_for_cli(console, deadline)

        if version is None:
            failures.append(
                f"no reply to 'ver' within {BOOT_TIMEOUT_SECONDS}s -- "
                "the firmware did not reach its command loop")
        else:
            print(f"ver -> {version}")
            expected = variant.get("version")
            if expected and expected not in version:
                failures.append(f"'ver' reported {version!r}, expected it to contain {expected!r}")

            if mod_bits & MOD_BIT_HOTSPOT_OTA:
                check_hotspot_ota(console, failures)
    except (OSError, socket.error) as exc:
        failures.append(f"console unreachable: {exc}")
    finally:
        if console:
            console.close()
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()

    boot_output = uart_log.read_text(errors="replace") if uart_log.exists() else ""
    for sig in CRASH_SIGNATURES:
        if sig in boot_output:
            failures.append(f"crash signature found: {sig!r}")

    # A boot writes to flash. Prove the run touched only its own copy.
    for path, before in sources.items():
        if hashlib.sha256(path.read_bytes()).hexdigest() != before:
            failures.append(f"source artifact modified by the test run: {path}")

    if failures:
        print(f"\nFAIL ({args.board_id}/{args.variant}):")
        for f in failures:
            print(f"  - {f}")
        sys.exit(1)

    exercised = ["ver"]
    if mod_bits & MOD_BIT_HOTSPOT_OTA:
        exercised.append("hotspot-ota setting round-trip")
    print(f"\nPASS ({args.board_id}/{args.variant}): answered {', '.join(exercised)}"
          f" (mod bits 0x{mod_bits:08x})")


if __name__ == "__main__":
    main()
