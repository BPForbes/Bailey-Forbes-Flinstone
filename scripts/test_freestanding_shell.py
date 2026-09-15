#!/usr/bin/env python3
"""Drive the freestanding serial shell: whoami, switchuser perspectives, sessions."""
import os
import select
import subprocess
import sys
import time
from pathlib import Path

root = Path(__file__).resolve().parents[1]
image = root / "dist" / "flintstone.img"
if not image.is_file():
    sys.exit("test-freestanding-shell: missing dist/flintstone.img")

proc = subprocess.Popen(
    [
        os.environ.get("FL_BROWSER_KERNEL_QEMU", "qemu-system-x86_64"),
        "-machine", "pc,accel=tcg", "-m", "64M", "-display", "none",
        "-monitor", "none", "-serial", "stdio", "-no-reboot",
        "-drive", f"file={image},format=raw,if=ide", "-boot", "c",
    ],
    stdin=subprocess.PIPE,
    stdout=subprocess.PIPE,
    stderr=subprocess.STDOUT,
    bufsize=0,
)
os.set_blocking(proc.stdout.fileno(), False)
buf = b""


def read_until(needle, timeout):
    global buf
    deadline = time.time() + timeout
    encoded = needle.encode()
    while time.time() < deadline:
        ready, _, _ = select.select([proc.stdout], [], [], 0.1)
        if ready:
            buf += proc.stdout.read() or b""
        if encoded in buf.replace(b"\r", b""):
            return
        if proc.poll() is not None:
            break
    raise SystemExit(f"test-freestanding-shell: missing {needle!r}\n{buf.decode('latin1', 'replace')}")


def send(text):
    proc.stdin.write(text.encode())
    proc.stdin.flush()


try:
    read_until("FLINTSTONE_KERNEL_BOOT_OK", 12)
    read_until("flinstone@flintstone>", 4)
    send("whoami\n")
    read_until("WHOAMI flinstone", 4)
    send("switchuser root\n")
    read_until("SWITCHUSER user=root", 4)
    send("whoami\n")
    read_until("WHOAMI root elevated", 4)
    send("history\n")
    read_until("1: whoami", 4)
    send("switchuser flinstone\n")
    read_until("SWITCHUSER user=flinstone", 4)
    send("whoami\n")
    read_until("WHOAMI flinstone", 4)
    send("history\n")
    read_until("1: whoami", 4)
    send("session new\n")
    read_until("SESSION 2 user=flinstone", 4)
    send("switchuser root\n")
    read_until("SWITCHUSER user=root", 4)
    send("session 1\n")
    read_until("SESSION 1 user=flinstone", 4)
    send("whoami\n")
    read_until("WHOAMI flinstone", 4)
    send("dir\n")
    read_until("readme.txt", 4)
    send("write hello.txt lab-fs\n")
    read_until("wrote hello.txt", 4)
    send("cat hello.txt\n")
    read_until("lab-fs", 4)
    send("server host\n")
    read_until("SERVER_RELAY host", 4)
    send("server msg hello-room\n")
    read_until("SERVER_RELAY msg hello-room", 4)
    print("test-freestanding-shell: PASS (whoami, switchuser, sessions, ramfs, server relay)")
finally:
    proc.kill()
    proc.wait()
