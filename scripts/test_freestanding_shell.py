#!/usr/bin/env python3
"""Drive the freestanding serial shell: identity, ramfs, lab disk/net, every hosted verb."""
import os
import pty
import re
import json
import select
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path

root = Path(__file__).resolve().parents[1]
# netlab needs the SECFEAT image (dist/flintstone-secfeat.img); the default is the plain image for manual runs.
image = Path(os.environ.get("FL_BROWSER_KERNEL_IMAGE", str(root / "dist" / "flintstone.img")))


def version_line():
    """VERSION_LINE the kernel was built with (userland/shell/version_def.h)."""
    for line in (root / "userland" / "shell" / "version_def.h").read_text(encoding="utf-8").splitlines():
        if line.startswith("#define VERSION_LINE "):
            return line.split('"')[1]
    raise SystemExit("version_def.h has no VERSION_LINE")
if not image.is_file():
    sys.exit(f"test-freestanding-shell: missing {image}")

# Fresh entropy per boot via fw_cfg (QEMU has no RDRAND); never a fixed file, since a reused one repeats keys.
entropy_dir = tempfile.mkdtemp(prefix="fl-entropy-")
entropy_file = os.path.join(entropy_dir, "entropy.bin")
qmp_path = os.path.join(entropy_dir, "qmp.sock")
with open(entropy_file, "wb") as f:
    f.write(os.urandom(4096))

proc = subprocess.Popen(
    [
        os.environ.get("FL_BROWSER_KERNEL_QEMU", "qemu-system-x86_64"),
        "-machine", "pc,accel=tcg", "-m", "64M", "-display", "none",
        "-fw_cfg", f"name=opt/flintstone/entropy,file={entropy_file}",
        "-monitor", "none", "-serial", "stdio", "-no-reboot",
        "-qmp", f"unix:{qmp_path},server=on,wait=off",
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


def vga_cells():
    """The 80x25 VGA text buffer, read through QMP pmemsave, as (char, attribute) byte pairs."""
    dump = os.path.join(entropy_dir, "vga.bin")
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as qmp:
        qmp.settimeout(5)
        qmp.connect(qmp_path)
        reader = qmp.makefile("r", encoding="utf-8")
        reader.readline()  # greeting

        def call(execute, arguments=None):
            msg = {"execute": execute}
            if arguments:
                msg["arguments"] = arguments
            qmp.sendall((json.dumps(msg) + "\n").encode())
            while True:
                reply = json.loads(reader.readline())
                if "return" in reply or "error" in reply:
                    if "error" in reply:
                        raise SystemExit(f"test-freestanding-shell: QMP {execute}: {reply['error']}")
                    return reply["return"]

        call("qmp_capabilities")
        call("pmemsave", {"val": 0xB8000, "size": 4000, "filename": dump})
    return Path(dump).read_bytes()


def vga_text():
    """The 80x25 VGA text screen as one string per row."""
    cells = vga_cells()
    chars = bytes(cells[i] for i in range(0, len(cells), 2)).decode("latin1")
    return "\n".join(chars[r * 80:(r + 1) * 80] for r in range(25))


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


# Each step is run in both shells and must give the same observation; accepted differences are listed below.
PARITY_SESSION = [
    ("su Flinstone", "flinstone"), ("whoami", None),
    ("su Root", "root"), ("whoami", None),
    ("useradd Bob", "bobpw"),
    ("su Bob", "bobpw"), ("whoami", None),
    ("useradd Eve", "evepw"),        # Bob is not elevated: refused
    ("su Root", "wrong"),            # wrong password: refused, still Bob
    ("whoami", None),
    ("su Nobody", "wrong"),          # no such account: refused (the hosted shell does not even prompt)
    ("su Root", "root"), ("whoami", None),
    ("su Flinstone", "flinstone"), ("whoami", None),
]
# Accepted lab/hosted differences: no step queues work yet (the FL-25 journal is memory-only on the lab); list any that does.
KQ_PARITY_EXCEPTIONS = {"queued-work journal durability (FL-25): memory only on the lab image"}

_FAIL_WORDS = re.compile(r"fail|denied|not exist|incorrect|could not|cannot|invalid|unknown|refus", re.I)


def observe(line, text):
    """Reduce one command's output to what both shells must agree on: always (verb, result, elevated),
    where result is the account name for whoami and "ok" or "refused" for everything else."""
    body = [l.strip() for l in text.replace("\r", "").split("\n") if l.strip()]
    body = [l for l in body if not l.startswith(("SESSION ", "Password:", "New password:"))
            and not l.endswith(("flintstone>", "shell>")) and line not in l]
    joined = " ".join(body)
    if line.startswith("whoami"):
        m = re.search(r"(?:WHOAMI\s+)?([A-Za-z][A-Za-z0-9_]*)(.*)", joined)
        return ("whoami", m.group(1), "elevated" in joined) if m else ("whoami", "?", False)
    if _FAIL_WORDS.search(joined):
        return (line.split()[0], "refused", False)
    return (line.split()[0], "ok", False)


def lab_session_step(line, secret):
    global buf
    start = len(buf)
    send(line + "\n")
    answered = False
    deadline = time.time() + 8
    while time.time() < deadline:
        ready, _, _ = select.select([proc.stdout], [], [], 0.1)
        if ready:
            buf += proc.stdout.read() or b""
        chunk = buf[start:].replace(b"\r", b"").decode("latin1", "replace")
        if secret is not None and not answered and chunk.rstrip().endswith("Password:") :
            send(secret + "\n")
            answered = True
            continue
        if chunk.endswith("flintstone> ") and line in chunk:
            return chunk
    raise SystemExit(f"test-freestanding-shell: parity step {line!r} did not finish\n{buf[start:].decode('latin1', 'replace')}")


def hosted_session(steps):
    binary = root / "BPForbes_Flinstone_Shell"
    if not binary.is_file():
        # Built here, not in browser-lab-build.yml: a second command after its untrusted checkout is a cache-poisoning risk.
        build = subprocess.run(["make"], cwd=root, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=False)
        if build.returncode != 0 or not binary.is_file():
            tail = build.stdout.decode("latin1", "replace")[-2000:]
            raise SystemExit(f"test-freestanding-shell: could not build {binary} (make exit {build.returncode})\n{tail}")
    out = []
    with tempfile.TemporaryDirectory() as work:
        (Path(work) / "userland" / "shell").mkdir(parents=True)  # where the hosted account store is written
        env = dict(os.environ, FL_USERS_LAB_DEFAULTS="1")
        pid, fd = pty.fork()
        if pid == 0:
            os.chdir(work)
            os.execve(str(binary), [str(binary)], env)

        def read_until_prompt(secret, timeout=None):
            timeout = timeout if timeout is not None else (60 if secret is not None else 10)
            text, answered, deadline = "", False, time.time() + timeout
            while time.time() < deadline:
                ready, _, _ = select.select([fd], [], [], 0.2)
                if ready:
                    try:
                        data = os.read(fd, 4096)
                    except OSError:
                        break
                    text += data.decode("latin1")
                clean = re.sub(r"\x1b\[[0-9;]*[A-Za-z]", "", text).replace("\r", "")
                if secret is not None and not answered and re.search(r"[Pp]assword: ?$", clean.rstrip("\n")):
                    os.write(fd, (secret + "\n").encode())
                    answered = True
                    continue
                if clean.rstrip(" ").endswith("shell>") and clean.count("shell>") >= 1 and \
                        (ready is None or not ready):
                    return clean
            raise SystemExit("test-freestanding-shell: hosted shell did not return to its prompt\n" + text)

        try:
            read_until_prompt(None, 15)
            for line, secret in steps:
                os.write(fd, (line + "\n").encode())
                text = read_until_prompt(secret)
                text = text[text.rfind(line) + len(line):] if line in text else text
                out.append(observe(line, text))
            os.write(fd, b"exit\n")
        finally:
            try:
                os.kill(pid, 9)
                os.waitpid(pid, 0)
            except (ProcessLookupError, ChildProcessError):
                pass  # the hosted shell already exited (its "exit" ran): nothing left to kill or reap
            except OSError as err:
                print(f"test-freestanding-shell: could not stop the hosted shell: {err}", file=sys.stderr)
    return out


def run_parity_session():
    lab = [observe(line, lab_session_step(line, secret)) for line, secret in PARITY_SESSION]
    hosted = hosted_session(PARITY_SESSION)
    if os.environ.get("FL_PARITY_VERBOSE"):
        print("parity lab   :", lab)
        print("parity hosted:", hosted)
    if lab != hosted:
        diff = "\n".join(f"  {step[0]!r:20} lab={a} hosted={b}"
                         for step, a, b in zip(PARITY_SESSION, lab, hosted) if a != b)
        raise SystemExit("test-freestanding-shell: account session differs between the lab image and the "
                         f"hosted shell (accepted: {sorted(KQ_PARITY_EXCEPTIONS)})\n{diff}\nlab={lab}\nhosted={hosted}")


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


def sign(path, password, expect, timeout=20):
    start = len(buf)
    send("server file sign " + path + "\n")
    wait_from("Password:", start)
    send(password + "\n")
    wait_from(expect, start, timeout)


def kq_history():
    """FL-02 stage 2 (the default image only): a ramfs file's history, each revision signed by KeyQuorum
    with the account's sealed key, judged by KeyQuorum under the keys the image holds."""
    read_until("CAP keyquorum-history=ok", 4)  # the boot self-test: a signed history in the in-memory store
    read_until("Flinstone@flintstone>", 4)
    cwd = cmd("pwd").replace("\r", "").split("\n")[1].strip()
    note = (cwd.rstrip("/") + "/kqnote.txt") if cwd.startswith("/") else "kqnote.txt"
    cmd("write kqnote.txt first draft", expect="wrote")
    cmd("server file log kqnote.txt", expect="no history for kqnote.txt")
    cmd("server file verify kqnote.txt", expect="no history for kqnote.txt")
    sign("kqnote.txt", "not-the-password", "authentication failed")
    for draft in ("first draft", "second draft"):
        cmd("write kqnote.txt " + draft, expect="wrote")
        sign("kqnote.txt", "flinstone", "signed by Flinstone")
    cmd("server file verify kqnote.txt", expect="signed by Flinstone")
    history = cmd("server file log kqnote.txt", expect="Flinstone")
    if history.count("by Flinstone") < 2:
        raise SystemExit("test-freestanding-shell: server file log shows fewer than two signed revisions\n" + history)
    start = len(buf)
    send("switchuser root\n")
    wait_from("Password:", start)
    send("root\n")
    wait_from("SWITCHUSER user=Root", start)
    cmd("server file verify " + note, expect="signed by Flinstone")  # Root reads it under Flinstone's key
    sign(note, "root", "the newest revision is another account's")  # and may not sign over it
    # passwd gives Flinstone a new key: its earlier revisions no longer verify as Flinstone's.
    start = len(buf)
    send("passwd flinstone\n")
    wait_from("Password:", start)
    send("flinstone\n")
    wait_from("ok", start, timeout=20)
    cmd("server file verify " + note, expect="the newest revision is not trusted")


try:
    if os.environ.get("FL_KQ_HISTORY_ONLY") == "1":
        read_until("FLINTSTONE_KERNEL_BOOT_OK", 12)
        kq_history()
        print("test-freestanding-shell: PASS (KeyQuorum-signed history in the default image)")
        sys.exit(0)
    read_until("FLINTSTONE_KERNEL_BOOT_OK", 12)
    read_until("network=lab", 4)
    read_until("CAP keyquorum=linked", 4)  # the KeyQuorum library is linked and its ABI accepted (FL-02)
    read_until("Flinstone@flintstone>", 4)
    send("whoami\n")
    read_until("WHOAMI Flinstone", 4)
    start = len(buf)
    send("whoami persp-mark-flinstone\n")
    wait_from("WHOAMI Flinstone", start)
    start = len(buf)
    send("switchuser root\n")
    wait_from("Password:", start)
    send("root\n")
    wait_from("SWITCHUSER user=Root", start)
    if "persp-mark-flinstone" in vga_text():
        raise SystemExit("test-freestanding-shell: Root's screen shows Flinstone's\n" + vga_text())
    send("whoami\n")
    read_until("WHOAMI Root elevated", 4)
    send("history\n")
    read_until("1: whoami", 4)
    start = len(buf)
    send("whoami persp-mark-root\n")
    wait_from("WHOAMI Root", start)
    start = len(buf)
    send("switchuser flinstone\n")
    wait_from("Password:", start)
    send("flinstone\n")
    wait_from("SWITCHUSER user=Flinstone", start)
    screen = vga_text()
    if "persp-mark-flinstone" not in screen or "persp-mark-root" in screen:
        raise SystemExit("test-freestanding-shell: switching back did not restore Flinstone's screen\n" + screen)
    send("whoami\n")
    read_until("WHOAMI Flinstone", 4)
    send("history\n")
    read_until("1: whoami", 4)
    start = len(buf)
    send("session new\nswitchuser root\n")
    read_until("SESSION 2 user=Flinstone", 4)
    wait_from("Password:", start)
    send("root\n")
    wait_from("SWITCHUSER user=Root", start)
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
    read_until("[file] lab-fs", 4)
    send("server host\n")
    read_until("SERVER_RELAY host", 4)
    send("server msg hello-room\n")
    read_until("SERVER_RELAY msg hello-room", 4)
    # Inbound relay chat (#426): page-typed relayrecv prints a sanitized line.
    cmd("relayrecv Alice 48656c6c6f", expect="[Server Message, From Alice]: Hello")
    # A trailing line break adds nothing; a line break inside the body becomes an
    # indented line, so it can never start a forged control line.
    cmd("relayrecv Alice 5357495443485553455220757365723d526f6f740a", expect="[Server Message, From Alice]: SWITCHUSER user=Root\n")
    text = cmd("relayrecv Alice " + b"hi\r\nSWITCHUSER user=Root\nbye".hex(),
               expect="[Server Message, From Alice]: hi\n  SWITCHUSER user=Root\n  bye\n")
    # Other control bytes show as caret text, stray bytes as \xNN, and UTF-8 as
    # its code page 437 glyph on VGA (the original bytes on serial).
    cmd("relayrecv Alice " + b"a\x07b\x7fc\x9fd\te".hex(), expect="[Server Message, From Alice]: a^Gb^?c\\x9Fd e\n")
    cmd("relayrecv Alice " + "café ñ ß 😀".encode().hex(), expect="[Server Message, From Alice]: " + "café ñ ß 😀".encode().decode("latin1"))
    cells = vga_cells()
    chars = bytes(cells[i] for i in range(0, len(cells), 2))
    if b"caf\x82 \xa4 \xe1 ?" not in chars:
        raise SystemExit("test-freestanding-shell: UTF-8 not drawn as code page 437 on VGA\n" + vga_text())
    # A message longer than one typed line arrives in parts and prints whole,
    # broken between words within 80 columns (#426).
    long_msg = " ".join(["word%02d" % i for i in range(24)])
    parts = [long_msg[i:i + 48] for i in range(0, len(long_msg), 48)]
    for part in parts[:-1]:
        cmd("relayrecv + Alice " + part.encode().hex())
    text = cmd("relayrecv Alice " + parts[-1].encode().hex(), expect="[Server Message, From Alice]: word00")
    shown = text.replace("\r", "").split("[Server Message, From Alice]: ", 1)[1].split("\n")
    shown = shown[:next(i for i, l in enumerate(shown) if not l.startswith(("word", "  word")))]
    widths = [len("[Server Message, From Alice]: ") + len(shown[0])] + [len(l) for l in shown[1:]]
    if " ".join(l.strip() for l in shown) != long_msg or len(shown) < 2 or max(widths) > 80:
        raise SystemExit("test-freestanding-shell: long relay message not whole or not wrapped\n" + text)
    # A part from another sender drops the unfinished message.
    cmd("relayrecv + Bob " + b"unfinished".hex())
    text = cmd("relayrecv Alice " + b"hi".hex(), expect="[Server Message, From Alice]: hi")
    if "unfinished" in text:
        raise SystemExit("test-freestanding-shell: a stale relay part leaked into another message\n" + text)
    # Colour-only SGR codes reach the VGA screen as colour; the line ends with a reset.
    cmd("relayrecv Alice " + b"\x1b[31mRED\x1b[0m ok".hex(), expect="[Server Message, From Alice]: \x1b[31mRED\x1b[0m ok\x1b[0m")
    cells = vga_cells()
    chars = bytes(cells[i] for i in range(0, len(cells), 2)).decode("latin1")
    at = chars.rfind("[Server Message, From Alice]: RED ok")
    if at < 0:
        raise SystemExit("test-freestanding-shell: coloured relay line missing on VGA\n" + vga_text())
    red = at + len("[Server Message, From Alice]: ")
    if cells[red * 2 + 1] != 0x04 or cells[(red + 4) * 2 + 1] != 0x03 or cells[at * 2 + 1] != 0x03 or cells[(at + 22) * 2 + 1] != 0x03:
        raise SystemExit("test-freestanding-shell: relay SGR colour not shown on VGA")
    # A sender's reset brings back the recipient's cyan, not plain grey.
    cmd("relayrecv Alice " + b"\x1b[31mRED\x1b[0m after".hex(), expect="after")
    cells = vga_cells()
    chars = bytes(cells[i] for i in range(0, len(cells), 2)).decode("latin1")
    at = chars.rfind("RED after")
    if at < 0 or cells[at * 2 + 1] != 0x04 or cells[(at + 4) * 2 + 1] != 0x03:
        raise SystemExit("test-freestanding-shell: text after a sender reset is not cyan on VGA")
    # Any other escape sequence (cursor moves, screen clears) stays visible as caret text.
    cmd("relayrecv Alice " + b"\x1b[2Jboom".hex(), expect="[Server Message, From Alice]: ^[[2Jboom")
    # Host announcements (#428 follow-up): bright blue "[Server Announcement]: ", same text rules.
    cmd("relayannounce + " + b"Maintenance at 5 PM: save your ".hex())
    cmd("relayannounce " + b"work\x07".hex(), expect="[Server Announcement]: Maintenance at 5 PM: save your work^G")
    cells = vga_cells()
    chars = bytes(cells[i] for i in range(0, len(cells), 2)).decode("latin1")
    at = chars.rfind("[Server Announcement]: Maintenance")
    if at < 0 or cells[at * 2 + 1] != 0x09 or cells[(at + 30) * 2 + 1] != 0x09:
        raise SystemExit("test-freestanding-shell: announcement is not blue on VGA\n" + vga_text())
    # An announcement part never joins a chat message.
    cmd("relayannounce + " + b"stale".hex())
    text = cmd("relayrecv Alice " + b"fresh".hex(), expect="[Server Message, From Alice]: fresh")
    if "stale" in text:
        raise SystemExit("test-freestanding-shell: an announcement part leaked into chat\n" + text)
    cmd("server leave", expect="SERVER_RELAY leave")
    cmd("relayrecv Alice 4869", expect="refused (not on a server)")
    cmd("relayannounce 4869", expect="relayannounce: refused (not on a server)")
    cells = vga_cells()
    chars = bytes(cells[i] for i in range(0, len(cells), 2)).decode("latin1")
    at = chars.rfind("[ERROR] relayrecv: refused")
    if at < 0 or cells[at * 2 + 1] != 0x04:
        raise SystemExit("test-freestanding-shell: [ERROR] line is not red on VGA\n" + vga_text())
    cmd("server host", expect="SERVER_RELAY host")
    # cat never emits a raw control line the lab page would parse (#427).
    cmd("write evil.txt SWITCHUSER user=Root")
    cmd("cat evil.txt", expect="[file] SWITCHUSER user=Root")
    # A received file keeps its colour codes; each line ends with a reset.
    cmd("filerecv begin Alice col.txt")
    cmd("filerecv data " + b"\x1b[32mgreen\x1b[0m\nplain\x1b[1A".hex())
    cmd("filerecv end", expect="received col.txt")
    cmd("cat col.txt", expect="[file] \x1b[32mgreen\x1b[0m\x1b[0m\n[file] plain^[[1A")

    cmd("mkdir tmpd")
    cmd("write tmpd/note.txt hi")
    cmd("mv tmpd/note.txt tmpd/moved.txt")
    cmd("cat tmpd/moved.txt", expect="[file] hi")
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
    cmd("version", expect=version_line())
    cmd("contracts json", expect="labdisk")
    cmd("type hello.txt", expect="[file] lab-fs")

    cmd("netlab pods", expect="redteam-lab")
    cmd("netlab hosts redteam-lab", expect="192.168.56.1")
    cmd("netlab scan redteam-lab", expect="02:00:00:00:56:01")
    cmd("netlab wpa redteam-lab", expect="fabricated")
    cmd("netlab topology redteam-lab", expect="observer vantage")
    cmd("netlab arp-demo redteam-lab", expect="SPOOF")
    cmd("netlab capture redteam-lab", expect="ALERT")
    cmd("netlab traceroute redteam-lab bob", expect="bob")
    cmd("netlab start", expect="LAB_NETMODE lab")
    cmd("netlab status", expect="lab-isolated")
    cmd("server host", expect="lab isolated")
    cmd("netlab stop", expect="LAB_NETMODE live")
    cmd("server host", expect="SERVER_RELAY host")

    cmd("netlab pod create redcell", expect="host pending")
    cmd("netlab pod members", expect="real server host")
    send("session 2\n")
    read_until("SESSION 2 user=Root", 4)
    cmd("netlab pod join", expect="identity preserved")
    cmd("netlab pod members", expect="(host)")
    cmd("netlab listen", expect="is listening")
    send("session 1\n")
    read_until("SESSION 1 user=Flinstone", 4)
    cmd("netlab accept", expect="LAB_NETMODE lab")
    cmd("whoami", expect="WHOAMI Flinstone")
    cmd("server host", expect="lab isolated")
    send("session 2\n")
    read_until("SESSION 2 user=Root", 4)
    cmd("netlab whoami", expect="Flinstone")
    cmd("netlab hostname", expect="Flinstone-sandbox")
    cmd("netlab write test.txt hello-from-sandbox", expect="wrote test.txt")
    cmd("netlab cat test.txt", expect="hello-from-sandbox")
    send("session 1\n")
    read_until("SESSION 1 user=Flinstone", 4)
    cmd("netlab end", expect="LAB_NETMODE live")
    cmd("server host", expect="SERVER_RELAY host")
    send("session 2\n")
    read_until("SESSION 2 user=Root", 4)
    cmd("netlab pod leave", expect="host pending")

    send("session 1\n")
    read_until("SESSION 1 user=Flinstone", 4)
    cmd("server kill", expect="pod dissolved")
    cmd("netlab pod members", expect="no pod")
    cmd("server host", expect="SERVER_RELAY host")
    cmd("netlab pod create redcell2", expect="host pending")
    send("session 2\n")
    read_until("SESSION 2 user=Root", 4)
    cmd("netlab pod join", expect="identity preserved")
    cmd("netlab pod host", expect="Root")
    send("session 1\n")
    read_until("SESSION 1 user=Flinstone", 4)
    cmd("server leave", expect="new pod host")
    cmd("netlab pod host", expect="Flinstone")
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
    run_parity_session()
    print("test-freestanding-shell: PASS (hosted verb parity, account session parity, ramfs, labdisk, labnet, server relay)")
finally:
    proc.kill()
    proc.wait()
