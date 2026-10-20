#!/usr/bin/env python3
"""Drive the freestanding serial shell: identity, ramfs, lab disk/net, every hosted verb."""
import os
import select
import subprocess
import sys
import time
from pathlib import Path

root = Path(__file__).resolve().parents[1]
# The netlab suite below needs the security-features (SECFEAT) image; the
# Makefile builds dist/flintstone-secfeat.img and points us at it. Default to
# the plain image for a manual run.
image = Path(os.environ.get("FL_BROWSER_KERNEL_IMAGE", str(root / "dist" / "flintstone.img")))
if not image.is_file():
    sys.exit(f"test-freestanding-shell: missing {image}")

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
    "switchuser", "login", "useradd", "users", "ls", "pwd", "rm", "netlab",
]


try:
    read_until("FLINTSTONE_KERNEL_BOOT_OK", 12)
    read_until("network=lab", 4)
    read_until("Flinstone@flintstone>", 4)
    send("whoami\n")
    read_until("WHOAMI Flinstone", 4)
    send("switchuser root\n")
    read_until("SWITCHUSER user=Root", 4)
    send("whoami\n")
    read_until("WHOAMI Root elevated", 4)
    send("history\n")
    read_until("1: whoami", 4)
    send("switchuser flinstone\n")
    read_until("SWITCHUSER user=Flinstone", 4)
    send("whoami\n")
    read_until("WHOAMI Flinstone", 4)
    send("history\n")
    read_until("1: whoami", 4)
    # Lab "New session" sends both lines in one burst, matching the iframe UI.
    send("session new\nswitchuser root\n")
    read_until("SESSION 2 user=Flinstone", 4)
    read_until("SWITCHUSER user=Root", 4)
    if b"no such session" in buf.replace(b"\r", b""):
        raise SystemExit(
            "test-freestanding-shell: leftover session new poisoned switchuser\n"
            + buf.decode("latin1", "replace")
        )
    send("session 1\n")
    read_until("SESSION 1 user=Flinstone", 4)
    send("whoami\n")
    read_until("WHOAMI Flinstone", 4)
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

    # netlab teaching lab (simulation) + lab/live network switch.
    cmd("netlab pods", expect="redteam-lab")
    cmd("netlab hosts redteam-lab", expect="192.168.56.1")
    cmd("netlab scan redteam-lab", expect="02:00:00:00:56:01")
    cmd("netlab wpa redteam-lab", expect="fabricated")
    cmd("netlab topology redteam-lab", expect="observer vantage")
    cmd("netlab arp-demo redteam-lab", expect="SPOOF")
    cmd("netlab capture redteam-lab", expect="ALERT")
    cmd("netlab traceroute redteam-lab bob", expect="bob")
    # Starting the demo drops the real network: the server relay refuses while
    # isolated, then resumes once the demo stops.
    cmd("netlab start", expect="LAB_NETMODE lab")
    cmd("netlab status", expect="lab-isolated")
    cmd("server host", expect="lab isolated")
    cmd("netlab stop", expect="LAB_NETMODE live")
    cmd("server host", expect="SERVER_RELAY host")

    # Pods: real-account membership, host-collision resolution, and a
    # listener/accept reverse shell bound to a real second session (not a
    # free-typed name). Session 1 (flinstone) just ran `server host` above,
    # so it is netlab's locally-tracked real server host: creating a pod
    # from here must NOT hand that same identity the pod host role too.
    cmd("netlab pod create redcell", expect="host pending")
    cmd("netlab pod members", expect="real server host")
    # Session 2 already exists (created earlier, logged in as root) and is
    # not the real server host, so joining it must resolve the pending host.
    send("session 2\n")
    read_until("SESSION 2 user=Root", 4)
    cmd("netlab pod join", expect="identity preserved")
    cmd("netlab pod members", expect="(host)")
    # root calls `listen` -> root is the attacker. flinstone will `accept` ->
    # flinstone is the victim side (listener = attacker, acceptor = victim).
    cmd("netlab listen", expect="is listening")
    send("session 1\n")
    read_until("SESSION 1 user=Flinstone", 4)
    cmd("netlab accept", expect="LAB_NETMODE lab")
    # flinstone (the victim) keeps using its own real session normally while
    # bound -- the binding never touches it.
    cmd("whoami", expect="WHOAMI Flinstone")
    cmd("server host", expect="lab isolated")
    # root is the bound attacker: only root's non-management `netlab <cmd>`
    # calls are redirected into the sandbox, and they execute for real
    # against isolated state, not canned text.
    send("session 2\n")
    read_until("SESSION 2 user=Root", 4)
    cmd("netlab whoami", expect="Flinstone")
    cmd("netlab hostname", expect="Flinstone-sandbox")
    cmd("netlab write test.txt hello-from-sandbox", expect="wrote test.txt")
    cmd("netlab cat test.txt", expect="hello-from-sandbox")
    # The accepting victim (session 1) can end the binding unilaterally.
    send("session 1\n")
    read_until("SESSION 1 user=Flinstone", 4)
    cmd("netlab end", expect="LAB_NETMODE live")
    cmd("server host", expect="SERVER_RELAY host")
    # Now that the binding is cleanly closed, root leaving the pod (its only
    # other member) must re-open "host pending": the sole remaining member
    # (flinstone) is still netlab's locally-tracked real server host.
    send("session 2\n")
    read_until("SESSION 2 user=Root", 4)
    cmd("netlab pod leave", expect="host pending")

    # Server lifecycle coupling. Session 1 (flinstone) is still the real server
    # host and the sole member of pod redcell (host pending).
    send("session 1\n")
    read_until("SESSION 1 user=Flinstone", 4)
    # `server kill` by the server host tears the server down, so the pod that
    # lived under it is killed too.
    cmd("server kill", expect="pod dissolved")
    cmd("netlab pod members", expect="no pod")
    # Rebuild a two-member pod to show `server leave` re-electing the pod host:
    # flinstone hosts the server again (so it is blocked from the pod host),
    # root joins and takes the pod host.
    cmd("server host", expect="SERVER_RELAY host")
    cmd("netlab pod create redcell2", expect="host pending")
    send("session 2\n")
    read_until("SESSION 2 user=Root", 4)
    cmd("netlab pod join", expect="identity preserved")
    cmd("netlab pod host", expect="Root")
    # flinstone (server host AND pod join-order #0) leaves the server: the relay
    # picks a new server host by its own order, and netlab independently
    # re-elects the pod host by pod join order, so flinstone reclaims it.
    send("session 1\n")
    read_until("SESSION 1 user=Flinstone", 4)
    cmd("server leave", expect="new pod host")
    cmd("netlab pod host", expect="Flinstone")
    # Gap closure: becoming the server host also re-elects the pod host on the
    # spot. flinstone (session 1) is the pod host; running `server host` makes
    # session 1 the server host (a separate relay point), so the pod host moves
    # to session 2 (root) immediately -- not only at accept time.
    cmd("server host", expect="new pod host: Root")
    cmd("netlab pod host", expect="Root")

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
    cmd("whoami", expect="WHOAMI Root elevated")
    print("test-freestanding-shell: PASS (hosted verb parity, ramfs, labdisk, labnet, server relay)")
finally:
    proc.kill()
    proc.wait()
