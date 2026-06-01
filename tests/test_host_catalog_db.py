#!/usr/bin/env python3
"""
Integration test: host SQLite catalog (files + messages).

Drives a 3-pane tmux session (host + Alice + Bob), sends broadcast/direct
messages and private/public file offers through the session host, then validates
server_shared/host_catalog.db and blob files with sqlite3 + hashlib.

Writes a PNG report to /opt/cursor/artifacts/host_catalog_db_test.png
"""

from __future__ import annotations

import hashlib
import os
import re
import sqlite3
import subprocess
import sys
import tempfile
import time
from dataclasses import dataclass, field
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
SHELL = REPO_ROOT / "BPForbes_Flinstone_Shell"
ARTIFACTS = Path("/opt/cursor/artifacts")
TMUX = ["tmux", "-f", "/exec-daemon/tmux.portal.conf"]

# contract_p3_channel_sidecar.h / contract_p5_server_catalog.h
PAYLOAD_FILE = 1
PAYLOAD_MSG = 2
STATUS_COMPLETE = 1


@dataclass
class Check:
    name: str
    ok: bool
    detail: str = ""


@dataclass
class Report:
    checks: list[Check] = field(default_factory=list)

    def add(self, name: str, ok: bool, detail: str = "") -> None:
        self.checks.append(Check(name, ok, detail))

    @property
    def passed(self) -> bool:
        return all(c.ok for c in self.checks)


def run(cmd: list[str], *, cwd: Path | None = None, env: dict | None = None, timeout: float | None = None) -> subprocess.CompletedProcess:
    return subprocess.run(
        cmd,
        cwd=cwd or REPO_ROOT,
        env=env,
        text=True,
        capture_output=True,
        check=False,
        timeout=timeout,
    )


def tmux(*args: str) -> subprocess.CompletedProcess:
    return run([*TMUX, *args])


def ensure_shell_built() -> None:
    if SHELL.is_file():
        return
    proc = run(["make", "BPForbes_Flinstone_Shell"])
    if proc.returncode != 0:
        raise RuntimeError(f"make failed:\n{proc.stdout}\n{proc.stderr}")


def run_session_scenario(work: Path, port: int, report: Report) -> str | None:
    """Return accepted share_id when found."""
    session = "host-catalog-py-test"
    quote = "Catalog integration quote for host DB validation."
    public_line = "Public bulletin stored in host catalog."
    large_payload = ("L" * 2100) + ("arge-file-marker-" * 130)  # well over 4096 bytes

    for rel in ("server_shared",):
        p = REPO_ROOT / rel
        if p.exists():
            import shutil
            shutil.rmtree(p)

    (work / "demo_share.txt").write_text(quote + "\n", encoding="utf-8")
    (work / "public_note.txt").write_text(public_line + "\n", encoding="utf-8")
    (work / "large.bin").write_bytes(large_payload.encode("ascii"))

    env = os.environ.copy()
    env["FL_USERS_LAB_DEFAULTS"] = "1"

    tmux("has-session", "-t", f"={session}", "2>/dev/null")  # noqa: shell redirect ignored
    tmux("kill-session", "-t", session)

    if tmux("new-session", "-d", "-s", session, "-c", str(REPO_ROOT), "-x", "240", "-y", "64", "--", "bash", "-l").returncode != 0:
        raise RuntimeError("tmux new-session failed")
    time.sleep(0.4)
    tmux("split-window", "-h", "-t", f"{session}:0", "-c", str(REPO_ROOT))
    tmux("split-window", "-v", "-t", f"{session}:0.0", "-c", str(REPO_ROOT))
    time.sleep(0.3)
    tmux("select-layout", "-t", f"{session}:0", "tiled")

    def send(pane: str, keys: str, delay: float = 0.7) -> None:
        tmux("send-keys", "-t", f"{session}:0.{pane}", keys, "C-m")
        time.sleep(delay)

    for pane in ("0", "1", "2"):
        send(pane, f"clear; export FL_USERS_LAB_DEFAULTS=1; {SHELL} whoami", 2.5)

    send("0", f"server host 127.0.0.1:{port}", 1.2)
    send("1", f"server join 127.0.0.1:{port}", 0.8)
    send("1", "n", 0.8)
    send("2", f"server join 127.0.0.1:{port}", 0.8)
    send("2", "n", 0.8)

    send("0", "server nick -id 2 -name Alice", 0.5)
    send("0", "server nick -id 3 -name Bob", 0.5)

    # Messages (host catalog on relay)
    send("1", "server msg -all Hello everyone from Alice", 0.8)
    send("1", "server msg -user Bob Private ping before file offer", 0.8)
    send("2", "server msg -user Alice Got your ping", 0.8)

    # Private file Alice -> Bob
    send("1", f"server file -user Bob {work}/demo_share.txt -vw", 5.0)
    send("2", "server file inbox", 1.0)

    cap = tmux("capture-pane", "-t", f"{session}:0.2", "-S", "-120", "-p")
    inbox = cap.stdout or ""
    m = re.search(r"share-\d+-\d+", inbox)
    share_id = m.group(0) if m else None
    report.add("file offer visible in Bob inbox", share_id is not None, share_id or "missing share id")

    if share_id:
        send("2", f"server file accept {share_id} --server-share", 3.0)

    # Public file from host
    send("0", f"server file -all {work}/public_note.txt -vw", 2.0)

    # Large private file (validates >4 KiB catalog path)
    send("1", f"server file -user Bob {work}/large.bin -vw", 10.0)

    send("0", "server kill", 0.5)
    for pane in ("0", "1", "2"):
        tmux("send-keys", "-t", f"{session}:0.{pane}", "exit", "C-m")
    time.sleep(0.4)
    tmux("kill-session", "-t", session)

    db_path = REPO_ROOT / "server_shared" / "host_catalog.db"
    report.add("host_catalog.db created", db_path.is_file(), str(db_path))
    return share_id


def sha256_hex(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def validate_catalog_db(report: Report) -> list[tuple]:
    db_path = REPO_ROOT / "server_shared" / "host_catalog.db"
    if not db_path.is_file():
        report.add("open catalog database", False, "missing host_catalog.db")
        return []

    conn = sqlite3.connect(db_path)
    conn.row_factory = sqlite3.Row
    rows = conn.execute(
        """
        SELECT transfer_id, content_hash, payload_kind, sender_member_id,
               receiver_member_id, is_public, status, payload_name,
               storage_relpath, total_bytes, created_at
        FROM catalog_entry
        ORDER BY created_at ASC
        """
    ).fetchall()
    report.add("catalog_entry rows present", len(rows) > 0, f"{len(rows)} row(s)")

    msg_rows = [r for r in rows if r["payload_kind"] == PAYLOAD_MSG and r["status"] == STATUS_COMPLETE]
    file_rows = [r for r in rows if r["payload_kind"] == PAYLOAD_FILE and r["status"] == STATUS_COMPLETE]

    broadcast = [r for r in msg_rows if r["payload_name"] == "broadcast"]
    direct = [r for r in msg_rows if r["payload_name"] == "direct"]
    report.add(
        "broadcast message in catalog",
        len(broadcast) >= 1,
        f"count={len(broadcast)}",
    )
    report.add(
        "direct message in catalog",
        len(direct) >= 1,
        f"count={len(direct)}",
    )
    report.add(
        "complete file row(s) in catalog",
        len(file_rows) >= 1,
        f"count={len(file_rows)}",
    )

    large_files = [r for r in file_rows if int(r["total_bytes"]) > 4096]
    report.add(
        "large file (>4096 B) stored complete",
        len(large_files) >= 1,
        f"max_bytes={max((int(r['total_bytes']) for r in file_rows), default=0)}",
    )

    for row in msg_rows + file_rows:
        relpath = row["storage_relpath"]
        chash = row["content_hash"]
        total = int(row["total_bytes"])
        if not relpath or str(relpath).startswith("pending:"):
            report.add(f"storage path for {row['transfer_id']}", False, "empty/pending")
            continue
        blob = REPO_ROOT / "server_shared" / relpath
        if not blob.is_file():
            report.add(f"blob exists {relpath}", False, "missing on disk")
            continue
        data = blob.read_bytes()
        ok_hash = sha256_hex(data) == chash
        ok_len = len(data) == total
        report.add(
            f"hash match {row['transfer_id'][:24]}",
            ok_hash and ok_len,
            f"bytes={len(data)} expected={total}",
        )

    # ACL: private file should grant Bob (3) via entry metadata or catalog_acl
    private_files = [r for r in file_rows if int(r["is_public"]) == 0 and int(r["receiver_member_id"]) == 3]
    if private_files:
        row = private_files[0]
        tid = row["transfer_id"]
        chash = row["content_hash"]
        sender = int(row["sender_member_id"])
        receiver = int(row["receiver_member_id"])
        meta_ok = sender == 2 and receiver == 3
        report.add(
            "private file sender/receiver metadata",
            meta_ok,
            f"sender={sender} receiver={receiver}",
        )
        acl_cols = {r[1] for r in conn.execute("PRAGMA table_info(catalog_acl)").fetchall()}
        if "transfer_id" in acl_cols:
            acl = conn.execute(
                "SELECT member_id FROM catalog_acl WHERE transfer_id=? ORDER BY member_id",
                (tid,),
            ).fetchall()
            key = tid
        else:
            acl = conn.execute(
                "SELECT member_id FROM catalog_acl WHERE content_hash=? ORDER BY member_id",
                (chash,),
            ).fetchall()
            key = chash[:16] + "…"
        members = [int(a["member_id"]) for a in acl]
        report.add(
            "private file ACL lists sender+receiver",
            2 in members and 3 in members,
            f"key={key} members={members}",
        )

    conn.close()
    return rows


def render_png(report: Report, rows: list) -> Path:
    from PIL import Image, ImageDraw, ImageFont

    ARTIFACTS.mkdir(parents=True, exist_ok=True)
    png_path = ARTIFACTS / "host_catalog_db_test.png"

    passed = sum(1 for c in report.checks if c.ok)
    total = len(report.checks)
    status_text = "PASS" if report.passed else "FAIL"
    status_rgb = (63, 185, 80) if report.passed else (248, 81, 73)

    lines = [
        "Host SQLite catalog — Python integration test",
        f"{status_text} ({passed}/{total} checks)",
        "",
        "Scenario: tmux host + Alice + Bob",
        "  · broadcast + direct messages",
        "  · private, public, and large file offers",
        "  · validates host_catalog.db + blobs/",
        "",
        "Assertions:",
    ]
    for c in report.checks:
        mark = "OK" if c.ok else "FAIL"
        extra = f" — {c.detail}" if c.detail else ""
        lines.append(f"  [{mark}] {c.name}{extra}")

    lines.extend(["", "catalog_entry snapshot:"])
    for r in rows[:10]:
        kind = "FILE" if r["payload_kind"] == PAYLOAD_FILE else "MSG"
        lines.append(
            f"  {kind:4} {str(r['transfer_id'])[:26]:26} "
            f"{str(r['payload_name'])[:16]:16} {int(r['total_bytes']):6}B "
            f"{'pub' if r['is_public'] else 'priv'}"
        )

    font = ImageFont.load_default()
    line_h = 18
    pad = 24
    width = 1180
    height = pad * 2 + line_h * len(lines) + 20
    img = Image.new("RGB", (width, height), (13, 17, 23))
    draw = ImageDraw.Draw(img)

    y = pad
    draw.text((pad, y), lines[0], fill=(121, 192, 255), font=font)
    y += line_h + 4
    draw.text((pad, y), lines[1], fill=status_rgb, font=font)
    y += line_h + 8

    for line in lines[2:]:
        if line.startswith("  [OK]"):
            color = (63, 185, 80)
        elif line.startswith("  [FAIL]"):
            color = (248, 81, 73)
        elif line.startswith("  FILE") or line.startswith("  MSG"):
            color = (201, 209, 217)
        elif line.endswith(":") and not line.startswith("  "):
            color = (210, 168, 255)
        elif line.startswith("  ·"):
            color = (139, 148, 158)
        else:
            color = (230, 237, 243)
        draw.text((pad, y), line, fill=color, font=font)
        y += line_h

    img.save(png_path)
    return png_path


def main() -> int:
    report = Report()
    work = Path(tempfile.mkdtemp(prefix="fl_host_catalog_", dir=REPO_ROOT))
    port = 50123 + (os.getpid() % 1000)

    try:
        ensure_shell_built()
        run([str(SHELL), "-y", "whoami"], env={**os.environ, "FL_USERS_LAB_DEFAULTS": "1"})

        share_id = run_session_scenario(work, port, report)
        rows = validate_catalog_db(report)
        if share_id:
            report.add("private file accept step ran", True, share_id)

        png = render_png(report, rows)
        print(f"Report: {png}")
        for c in report.checks:
            mark = "OK" if c.ok else "FAIL"
            extra = f" — {c.detail}" if c.detail else ""
            print(f"  [{mark}] {c.name}{extra}")

        return 0 if report.passed else 1
    finally:
        import shutil
        shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
