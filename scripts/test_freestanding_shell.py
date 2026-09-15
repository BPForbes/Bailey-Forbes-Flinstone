#!/usr/bin/env python3
"""Drive the freestanding serial shell: identity, ramfs, lab disk/net, every hosted verb."""
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


def wait_from(needle, start, timeout=4):
    global buf
    deadline = time.time() + timeout
    encoded = needle.encode()
    while time.time() < deadline:
        ready, _, _ = select.select([proc.stdout], [], [], 0.1)
        if ready:
            buf += proc.stdout.read() or b""
        if encoded in buf[start:].replace(b"\r", b""):
            return
        if proc.poll() is not None:
            break
    raise SystemExit(
        f"test-freestanding-shell: missing {needle!r}\n{buf[start:].decode('latin1', 'replace')}"
    )


def cmd(line, expect=None, timeout=6):
    global buf
    start = len(buf)
    send(line + "\n")
    deadline = time.time() + timeout
    prompt = b"flintstone> "
    while time.time() < deadline:
        ready, _, _ = select.select([proc.stdout], [], [], 0.1)
        if ready:
            buf += proc.stdout.read() or b""
        chunk = buf[start:].replace(b"\r", b"")
        echoed = line.encode()
        if echoed not in chunk:
            continue
        if prompt in chunk and chunk.endswith(prompt) and chunk.count(b"\n") >= 1:
            text = chunk.decode("latin1", "replace")
            if "unknown command" in text:
                raise SystemExit(f"test-freestanding-shell: unknown command for {line!r}\n{text}")
            if expect is not None and expect not in text:
                raise SystemExit(f"test-freestanding-shell: missing {expect!r} after {line!r}\n{text}")
            return text
        if proc.poll() is not None:
            break
    raise SystemExit(
        f"test-freestanding-shell: timeout after {line!r}\n{buf[start:].decode('latin1', 'replace')}"
    )


HOSTED_VERBS = [
    "addcluster", "arp", "audit", "cat", "check", "cd", "contracts", "createdisk",
    "delcluster", "diskdel", "diskfiles", "diskget", "diskmkdir", "diskput", "dir",
    "du", "format", "ifconfig", "import", "initdisk", "listclusters", "listdirs",
    "logout", "loc", "mkdir", "mv", "netsh", "wifi", "netstat", "nslookup", "resolve",
    "printdisk", "redirect", "rerun", "rmdir", "rmtree", "route", "search", "server",
    "setdisk", "sudo -k", "userdel", "ping", "ping6", "whoami", "type", "udplisten",
    "udpsend", "update", "version", "where", "write", "writecluster",
    "help", "history", "his", "cc", "exit", "bios", "clear", "make", "session",
    "switchuser", "login", "useradd", "users", "ls", "pwd", "rm",
]


try:
    read_until("FLINTSTONE_KERNEL_BOOT_OK", 12)
    read_until("network=lab", 4)
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
    # Lab "New session" sends both lines in one burst, matching the iframe UI.
    send("session new\nswitchuser root\n")
    read_until("SESSION 2 user=flinstone", 4)
    read_until("SWITCHUSER user=root", 4)
    if b"no such session" in buf.replace(b"\r", b""):
        raise SystemExit(
            "test-freestanding-shell: leftover session new poisoned switchuser\n"
            + buf.decode("latin1", "replace")
        )
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

    cmd("mkdir tmpd")
    cmd("write tmpd/note.txt hi")
    cmd("mv tmpd/note.txt tmpd/moved.txt")
    cmd("cat tmpd/moved.txt", expect="hi")
    cmd("search moved", expect="moved.txt")
    cmd("du", expect="ramfs")
    cmd("rmdir tmpd")
    cmd("rmtree tmpd")
    cmd("ping 127.0.0.1", expect="PING")
    cmd("ping bailey-forbes.com", expect="10.0.0.80")
    cmd("ping 8.8.8.8", expect="PING 8.8.8.8")
    cmd("ping example.com", expect="SERVER_RELAY dns example.com")
    cmd("dnsack example.com 93.184.216.34", expect="PING example.com 93.184.216.34")
    cmd("nslookup example.com", expect="93.184.216.34")
    cmd("ping missing.invalid", expect="SERVER_RELAY dns missing.invalid")
    cmd("dnsack missing.invalid fail", expect="unknown host")
    cmd("ping6 localhost", expect="PING6")
    cmd("ifconfig", expect="lo UP")
    cmd("nslookup localhost", expect="127.0.0.1")
    cmd("check requirements localhost 80", expect="check OK")
    cmd("wifi scan", expect="FlintstoneLab")
    cmd("createdisk labvol 4 8", expect="created disk")
    cmd("writecluster 0 -t payload", expect="cluster written")
    cmd("listclusters", expect="payload")
    cmd("version", expect="4.5.2")
    cmd("contracts json", expect="labdisk")
    cmd("type hello.txt", expect="lab-fs")

    for verb in HOSTED_VERBS:
        cmd(verb)

    start = len(buf)
    send("passwd\n")
    wait_from("Password:", start)
    start = len(buf)
    send("flinstone\n")
    wait_from("ok", start)
    start = len(buf)
    send("sudo\n")
    wait_from("Password:", start)
    start = len(buf)
    send("flinstone\n")
    wait_from("ok", start)
    cmd("whoami", expect="elevated")
    cmd("sudo -k")
    start = len(buf)
    send("su\n")
    wait_from("Password:", start)
    start = len(buf)
    send("root\n")
    wait_from("ok", start)
    cmd("whoami", expect="WHOAMI root elevated")
    print("test-freestanding-shell: PASS (hosted verb parity, ramfs, labdisk, labnet, server relay)")
finally:
    proc.kill()
    proc.wait()
