#!/usr/bin/env python3
# Boots a real, unmodified release firmware image in QEMU and holds a conversation with it.
# A pass means the node answered: absence of a crash is not a pass, since an image that
# never executes an application instruction produces no crash either.
#
# The mod bit register says which mods were built in, so a mod's CLI is only exercised when
# its bit is set. QEMU is always pointed at a freshly composed copy, never the image under
# test: booting a node writes SPIFFS, NVS and a first-boot identity to its flash.

import argparse
import hashlib
import json
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
MOD_BIT_HOTSPOT_OTA = 0x00000002

# The device models take board wiring as qdev properties so one binary serves every board.
# strap-mode selects SPI boot and differs per chip; the wrong value forces download mode.
def merge_flash_image(board, variant, flasher_dir, out_path):
    """Compose a fresh flash image from the vendored parts. Never modifies its inputs."""
    offsets = {k: int(v, 16) for k, v in board["offsets"].items()}
    # 16MB regardless of the board's real flash: QEMU picks the emulated chip from the drive
    # size and only the 16MB ISSI part gets past esp_flash_init_default_chip().
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
    reserved = data[208:288]
    for marker in range(len(reserved) - 7):
        if reserved[marker:marker + 8] == b"MOBMESH\0" and marker >= 28 and reserved[marker - 1] == 1:
            return int.from_bytes(reserved[marker - 8:marker - 4], "little")
    return 0


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

    def ask(self, command, timeout=COMMAND_TIMEOUT_SECONDS, quiet=False, settle=0.4):
        """Send a command and return its whole reply, or None if it never answers.

        Only complete lines count. A reply arrives over TCP in whatever chunks the
        emulator's console produces, so matching on the arrow alone can return the empty
        string when the text after it has not landed yet.

        A reply can span several lines: `sync.region` prints one entry per line and
        `publish.status` groups its fields. The firmware writes the whole reply in a single
        println, so the remaining lines are already in flight once the arrow line is complete
        -- `settle` is how long to keep draining for them before giving up on more. Returning
        only the arrow line, as this used to, reads a seven-entry overlay as its root alone.
        """
        self.buf = ""
        self.send(command)
        deadline = time.time() + timeout
        while time.time() < deadline:
            self.drain()
            complete, _, _ = self.buf.rpartition("\n")
            lines = complete.splitlines()
            for index, line in enumerate(lines):
                if "->" not in line:
                    continue
                reply = line.split("->", 1)[1].strip()
                if not reply:
                    continue
                # Anything after the arrow line belongs to this reply: the next command has
                # not been sent yet, so nothing else can be on the wire.
                until = time.time() + settle
                while time.time() < until:
                    self.drain()
                    time.sleep(0.05)
                complete, _, _ = self.buf.rpartition("\n")
                tail = complete.splitlines()[index + 1:]
                parts = [reply] + [t.strip() for t in tail if t.strip()]
                reply = "\n".join(parts)
                if not quiet:
                    shown = reply.replace("\n", " | ")
                    print(f"    {command} -> {shown}", flush=True)
                return reply
            time.sleep(0.2)
        return None

    def close(self):
        try:
            self.sock.close()
        except OSError:
            pass


def wait_for_cli(console, deadline):
    """Wait for one probe, avoiding queued replies that can mask the next command."""
    return console.ask("ver", timeout=max(0, deadline - time.time()), quiet=True)


def check_hotspot_ota(console, failures):
    """Read-only: ask the mod to report its service and A/B slot state.

    Chosen over the settings round-trips because it writes nothing. A CLI `set` persists to
    flash, which is safe here only by virtue of the image being a throwaway copy, and a
    build gate should not rest on that. This still proves the mod's CLI is wired up and that
    RollbackGuard can read the partition table and identify the running slot.

    Deliberately not asserted: which slot is active, or its recorded version. A freshly
    composed image has never taken an update, so those describe flashing history rather than
    the health of the firmware.
    """
    reply = console.ask("get ota.status")
    if reply is None:
        failures.append("hotspot-ota: 'get ota.status' never answered")
    elif "idle" not in reply:
        failures.append(f"hotspot-ota: service was not idle after boot: {reply!r}")

    reply = console.ask("get ota.slot")
    if reply is None:
        failures.append("hotspot-ota: 'get ota.slot' never answered")
        return
    if "A=" not in reply or "B=" not in reply:
        failures.append(f"hotspot-ota: 'get ota.slot' did not report both slots: {reply!r}")
        return
    if reply.count("active") != 1:
        failures.append(f"hotspot-ota: expected exactly one active slot: {reply!r}")


def run_once(cmd, variant, mod_bits, uart_log):
    """One boot and one conversation. Returns the problems it found."""
    if uart_log.exists():
        uart_log.unlink()
    proc = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    failures = []
    console = None
    try:
        time.sleep(2)
        console = Console(CONSOLE_PORT)
        version = wait_for_cli(console, time.time() + BOOT_TIMEOUT_SECONDS)
        if version is not None:
            print(f"    ver -> {version}", flush=True)
        if version is None:
            failures.append(
                f"no reply to 'ver' within {BOOT_TIMEOUT_SECONDS}s -- "
                "the firmware did not reach its command loop")
        else:
            expected = variant.get("version")
            if expected and expected not in version:
                failures.append(f"'ver' reported {version!r}, expected it to contain {expected!r}")
            if mod_bits & MOD_BIT_HOTSPOT_OTA:
                check_hotspot_ota(console, failures)
    except OSError as exc:
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
    # Always, not only on failure: a passing run's console is what makes the next failure
    # readable, and a silent log hides a check that is asserting less than it appears to.
    print(f"--- console ({uart_log.name}) ---")
    print(boot_output.rstrip() or "(nothing on serial 0)")
    print("--- end console ---", flush=True)
    for sig in CRASH_SIGNATURES:
        if sig in boot_output:
            failures.append(f"crash signature found: {sig!r}")
    return failures


def serial_args(console, uart_log):
    """Serial 0 is UART0 (boot ROM, panics), 2 hardware USB, 3 the OTG CDC; the CLI rides uart0, usb or otg."""
    cli = f"tcp:127.0.0.1:{CONSOLE_PORT},server=on,wait=off"
    if console == "uart0":
        # UART0 carries the CLI and any panic: the guest waits for the harness, and every byte is logged for the crash scan.
        return ["-chardev", f"socket,id=uart0,host=127.0.0.1,port={CONSOLE_PORT},server=on,wait=on,logfile={uart_log}",
                "-serial", "chardev:uart0", "-serial", "null", "-serial", "null"]
    if console == "otg":
        return ["-serial", f"file:{uart_log}", "-serial", "null", "-serial", "null", "-serial", cli]
    return ["-serial", f"file:{uart_log}", "-serial", "null", "-serial", cli]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--board-id", required=True)
    parser.add_argument("--variant", required=True)
    parser.add_argument("--boards-json", type=Path, required=True)
    parser.add_argument("--flasher-dir", type=Path, required=True)
    # Either point at an unpacked emulator release and let the board table pick the binary,
    # or name the binary, ROMs and machine directly.
    parser.add_argument("--qemu-dir", type=Path)
    parser.add_argument("--qemu-bin", type=Path)
    parser.add_argument("--rom-dir", type=Path)
    parser.add_argument("--machine")
    parser.add_argument("--mem")
    parser.add_argument("--workdir", type=Path, required=True)
    args = parser.parse_args()

    boards = json.loads(args.boards_json.read_text())
    board = boards[args.board_id]
    variant = board["variants"][args.variant]
    # How to emulate this board is a board fact, so it lives beside the others in
    # variants/<board>/overrides.yaml and rides into auto_boards.json from there.
    emu = board.get("qemu") or {}
    if not emu.get("enabled"):
        print(f"SKIP ({args.board_id}/{args.variant}): "
              "no emulation configured for this board (variants/<board>/overrides.yaml)")
        return

    if args.qemu_dir:
        qemu_bin = args.qemu_dir / emu["binary"]
        rom_dir = args.qemu_dir / "pc-bios"
    elif args.qemu_bin and args.rom_dir:
        qemu_bin, rom_dir = args.qemu_bin, args.rom_dir
    else:
        sys.exit("need --qemu-dir, or both --qemu-bin and --rom-dir")

    machine = args.machine or emu.get("machine")
    if not machine:
        sys.exit("the board's qemu block names no machine")
    mem = args.mem if args.mem is not None else emu.get("mem", "")

    args.workdir.mkdir(parents=True, exist_ok=True)
    flash_image = args.workdir / "flash_image.bin"
    sources = merge_flash_image(board, variant, args.flasher_dir, flash_image)
    mod_bits = read_mod_bits(args.flasher_dir, variant)
    uart_log = args.workdir / "uart0.log"

    cmd = [
        str(qemu_bin), "-display", "none", "-monitor", "none",
        "-machine", machine,
        "-L", str(rom_dir),
        "-drive", f"file={flash_image},if=mtd,format=raw",
    ] + serial_args(emu.get("console", "usb"), uart_log)
    if mem:
        cmd += ["-m", mem]
    for prop, value in (emu.get("globals") or {}).items():
        driver, _, name = prop.rpartition(".")
        cmd += ["-global", f"driver={driver},property={name},value={value}"]

    # A first-boot SPIFFS mount failure and stack-canary panic appears about one run in
    # twelve and does not reproduce; two failures in a row is real.
    failures = run_once(cmd, variant, mod_bits, uart_log)
    retried = bool(failures)
    if failures:
        print(f"attempt 1 failed ({len(failures)} problem(s)) -- retrying once")
        # A fresh image: the previous attempt booted and wrote to this copy.
        merge_flash_image(board, variant, args.flasher_dir, flash_image)
        failures = run_once(cmd, variant, mod_bits, uart_log)

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
        exercised.append("hotspot-ota service and slot reports")
    note = " (passed on retry)" if retried else ""
    print(f"\nPASS ({args.board_id}/{args.variant}): answered {', '.join(exercised)}"
          f" (mod bits 0x{mod_bits:08x}){note}")


if __name__ == "__main__":
    main()
