#!/usr/bin/env python3
"""FL-02 persistence: KeyQuorum histories and sealed account keys survive a reboot on the data disk.

Boots the default image under QEMU with a writable data disk (secondary master, `index=2`) and checks:
  1. a signed history and the Flinstone key written in one boot are back after a reboot (log and verify);
  2. a damaged record ends the log there: the history after it is not restored, and the boot still works;
  3. with no data disk the image still boots and runs its history self-test (nothing persists).
The data disk is always a copy in a temporary directory; the built image is only read.
"""
import os
import select
import shutil
import subprocess
import tempfile
import time
from pathlib import Path

root = Path(__file__).resolve().parents[1]
image = Path(os.environ.get("FL_BROWSER_KERNEL_IMAGE", str(root / "dist" / "flintstone.img")))
qemu = os.environ.get("FL_BROWSER_KERNEL_QEMU", "qemu-system-x86_64")
work = Path(tempfile.mkdtemp(prefix="fl-persist-"))
entropy = work / "entropy.bin"
entropy.write_bytes(os.urandom(4096))
DISK_SECTORS = 8192  # 4 MiB


class Boot:
    def __init__(self, data_disk):
        args = [qemu, "-machine", "pc,accel=tcg", "-m", "64M", "-display", "none", "-monitor", "none",
                "-serial", "stdio", "-no-reboot", "-fw_cfg", f"name=opt/flintstone/entropy,file={entropy}",
                "-drive", f"file={image},format=raw,if=ide"]
        if data_disk is not None:
            args += ["-drive", f"file={data_disk},format=raw,if=ide,index=2"]
        self.proc = subprocess.Popen(args, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                     stderr=subprocess.STDOUT, bufsize=0)
        os.set_blocking(self.proc.stdout.fileno(), False)
        self.buf = b""

    def _pump(self, timeout):
        ready, _, _ = select.select([self.proc.stdout], [], [], timeout)
        if ready:
            self.buf += self.proc.stdout.read() or b""

    def wait_for(self, needle, start=0, timeout=20):
        deadline = time.time() + timeout
        raw = needle.encode()
        while time.time() < deadline:
            self._pump(0.1)
            if raw in self.buf[start:].replace(b"\r", b""):
                return self.buf[start:].replace(b"\r", b"").decode("latin1", "replace")
            if self.proc.poll() is not None:
                break
        raise SystemExit(f"test-freestanding-kq-persist: missing {needle!r}\n"
                         + self.buf[start:].decode("latin1", "replace")[-1500:])

    def cmd(self, line, expect, timeout=10):
        start = len(self.buf)
        self.proc.stdin.write((line + "\n").encode())
        self.proc.stdin.flush()
        text = self.wait_for(expect, start, timeout)
        self.wait_for("flintstone> ", start, timeout)
        return text

    def boot_ready(self):
        self.wait_for("FLINTSTONE_KERNEL_BOOT_OK", timeout=30)
        self.wait_for("Flinstone@flintstone>", timeout=30)

    def stop(self):
        self.proc.kill()
        self.proc.wait()


def blank_disk(name):
    path = work / name
    with open(path, "wb") as f:
        f.truncate(DISK_SECTORS * 512)
    return path


def check(cond, what, text=""):
    if not cond:
        raise SystemExit(f"test-freestanding-kq-persist: {what}\n{text}")


disk = blank_disk("data.img")

# 1. write and sign, then reboot and find the history and the key again
first = Boot(disk)
try:
    first.wait_for("CAP keyquorum-disk=present", timeout=30)
    first.boot_ready()
    first.cmd("write kqnote.txt persisted draft", "wrote")
    start = len(first.buf)
    first.proc.stdin.write(b"server file sign kqnote.txt\n")
    first.wait_for("Password:", start)
    first.proc.stdin.write(b"flinstone\n")
    text = first.wait_for("kept on the data disk", start, timeout=30)
    check("not kept" not in text, "the signature was not kept on the data disk", text)
finally:
    first.stop()

second = Boot(disk)
try:
    second.wait_for("CAP keyquorum-disk=present", timeout=30)
    second.boot_ready()
    text = second.cmd("server file log kqnote.txt", "by Flinstone")
    check(text.count("by Flinstone") >= 1, "the history did not come back after a reboot", text)
    second.cmd("server file verify kqnote.txt", "signed by Flinstone")
finally:
    second.stop()

# 2. a damaged first record ends the log: nothing after it is restored, and the image still boots
damaged = work / "damaged.img"
shutil.copyfile(disk, damaged)
with open(damaged, "r+b") as f:
    f.seek(512 + 5)  # inside the first record's data sector
    byte = f.read(1)
    f.seek(512 + 5)
    f.write(bytes([byte[0] ^ 0xFF]))
third = Boot(damaged)
try:
    third.wait_for("CAP keyquorum-disk=present", timeout=30)
    third.boot_ready()
    text = third.cmd("server file log kqnote.txt", "no history for kqnote.txt")
    check("by Flinstone" not in text, "a damaged record was restored", text)
    third.wait_for("CAP keyquorum-history=ok", timeout=30)
finally:
    third.stop()

# 3. no data disk: the image still boots and its self-test passes
bare = Boot(None)
try:
    bare.wait_for("CAP keyquorum-disk=absent", timeout=30)
    bare.boot_ready()
    bare.wait_for("CAP keyquorum-history=ok", timeout=30)
finally:
    bare.stop()

shutil.rmtree(work, ignore_errors=True)
print("test-freestanding-kq-persist: PASS (histories and sealed keys survive a reboot; a damaged record ends the log; no disk boots)")
