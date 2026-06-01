#!/usr/bin/env python3
"""Render server file exchange pcap decode + sequence diagram to PNG."""

import re
import subprocess
import sys
from pathlib import Path

ART = Path("/opt/cursor/artifacts")
OUT_PNG = ART / "server_file_exchange.png"
OUT_HTML = ART / "server_file_exchange.html"

FILE_OPS = {
    "FILE_OFFER", "FILE_META", "FILE_CHUNK", "FILE_DONE",
    "FILE_ACCEPT", "FILE_DECLINE", "FILE_REVOKE",
}


def parse_frames(frames_path: Path):
    """Extract (stream_label, opcode_name, payload_preview) per decoded frame."""
    if not frames_path.exists():
        return [], {}
    text = frames_path.read_text(errors="replace")
    stream = ""
    rows = []
    counts = {}
    for line in text.splitlines():
        m = re.match(r"--- ([0-9.:]+) -> ([0-9.:]+)", line)
        if m:
            stream = f"{m.group(1)} → {m.group(2)}"
            continue
        m = re.match(
            r"\s+off=0x[0-9a-f]+ .* op=0x[0-9a-f]+ \(([^)]+)\).* payload='([^']*)'",
            line,
        )
        if m:
            op = m.group(1)
            preview = m.group(2)
            rows.append((stream, op, preview))
            counts[op] = counts.get(op, 0) + 1
    return rows, counts


def pick_file_sequence(rows):
    """Return file-transfer frames in capture order (primary send + relay)."""
    seq = []
    for stream, op, preview in rows:
        if op in FILE_OPS:
            seq.append((stream, op, preview))
    return seq


def render_html(share_id: str, seq, counts, pcap_path: Path):
    file_counts = {k: v for k, v in counts.items() if k in FILE_OPS or k.startswith("FILE_")}
    other_counts = {k: v for k, v in counts.items() if k not in file_counts}

    seq_rows = ""
    for i, (stream, op, preview) in enumerate(seq, 1):
        hl = "meta" if op == "FILE_META" else "file"
        note = ""
        if op == "FILE_META":
            note = "session-only sidecar (not saved on receiver drive)"
        elif op == "FILE_OFFER":
            note = f"offer id visible in payload; share_id={share_id}"
        elif op == "FILE_CHUNK":
            note = "file bytes (joke.txt)"
        elif op == "FILE_DONE":
            note = "transfer complete; receiver may accept"
        seq_rows += f"""
        <tr class="{hl}">
          <td>{i}</td>
          <td><code>{op}</code></td>
          <td class="stream">{stream}</td>
          <td><code>{preview[:56]}{'…' if len(preview) > 56 else ''}</code></td>
          <td class="note">{note}</td>
        </tr>"""

    def count_lines(d):
        return "".join(f"<li><code>{k}</code> × {v}</li>" for k, v in sorted(d.items()))

    return f"""<!doctype html>
<html><head><meta charset="utf-8"><title>Server file exchange — pcap</title>
<style>
  body {{
    margin: 0; padding: 24px 28px 32px;
    background: #0d1117; color: #e6edf3;
    font-family: Menlo, Consolas, monospace; font-size: 12px;
  }}
  h1 {{ color: #79c0ff; font-size: 22px; margin: 0 0 6px 0; }}
  .sub {{ color: #8b949e; margin: 0 0 18px 0; line-height: 1.55; max-width: 1200px; }}
  .diagram {{
    display: flex; align-items: center; justify-content: center; gap: 18px;
    margin: 0 0 22px 0; padding: 18px; background: #161b22; border: 1px solid #30363d;
    border-radius: 10px; max-width: 1200px;
  }}
  .node {{
    padding: 14px 20px; border-radius: 8px; border: 2px solid #30363d;
    background: #010409; text-align: center; min-width: 120px;
  }}
  .node strong {{ display: block; color: #d2a8ff; font-size: 14px; }}
  .node span {{ color: #8b949e; font-size: 11px; }}
  .arrow {{
    color: #58a6ff; font-size: 13px; text-align: center; line-height: 1.45;
  }}
  .arrow em {{ color: #ffa657; font-style: normal; font-weight: 700; }}
  table {{
    width: 100%; max-width: 1200px; border-collapse: collapse;
    background: #161b22; border: 1px solid #30363d; border-radius: 8px; overflow: hidden;
  }}
  th, td {{ padding: 8px 10px; border-bottom: 1px solid #21262d; vertical-align: top; }}
  th {{ background: #21262d; color: #c9d1d9; text-align: left; }}
  tr.meta td {{ background: #1f2d1a; }}
  tr.file td {{ background: #161b22; }}
  tr.meta code {{ color: #7ee787; }}
  td.stream {{ color: #79c0ff; white-space: nowrap; }}
  td.note {{ color: #8b949e; font-size: 11px; }}
  .cols {{ display: grid; grid-template-columns: 1fr 1fr; gap: 16px; max-width: 1200px; margin-top: 18px; }}
  .box {{
    background: #161b22; border: 1px solid #30363d; border-radius: 8px; padding: 12px 14px;
  }}
  .box h2 {{ margin: 0 0 8px 0; font-size: 13px; color: #d2a8ff; }}
  ul {{ margin: 0; padding-left: 18px; line-height: 1.7; }}
  .footer {{ margin-top: 16px; color: #8b949e; max-width: 1200px; }}
</style></head><body>
<h1>Native server file exchange (pcap)</h1>
<p class="sub">Sarah (Alice) sends <strong>joke.txt</strong> to Bob over the P3-13 TCP session.
<code>FILE_META</code> travels in parallel with chunks for in-flight tracking; Bob's drive receives
<strong>joke.txt</strong> only after <code>accept --server-share</code> — no <code>&lt;share_id&gt;.meta</code> sidecar.</p>

<div class="diagram">
  <div class="node"><strong>Alice</strong><span>sender · member 2</span></div>
  <div class="arrow">FILE_OFFER<br><em>FILE_META</em><br>FILE_CHUNK…<br>FILE_DONE →</div>
  <div class="node"><strong>Host</strong><span>127.0.0.1 hub</span></div>
  <div class="arrow">relay →<br><em>FILE_META</em><br>FILE_CHUNK…<br>FILE_DONE</div>
  <div class="node"><strong>Bob</strong><span>receiver · member 3</span></div>
</div>

<table>
  <thead><tr><th>#</th><th>Opcode</th><th>TCP stream</th><th>Payload preview</th><th>Notes</th></tr></thead>
  <tbody>{seq_rows or '<tr><td colspan="5">(no file frames decoded — install scapy)</td></tr>'}
  </tbody>
</table>

<div class="cols">
  <div class="box"><h2>File opcodes in capture</h2><ul>{count_lines(file_counts) or '<li>(none)</li>'}</ul></div>
  <div class="box"><h2>Other session opcodes</h2><ul>{count_lines(other_counts) or '<li>(none)</li>'}</ul></div>
</div>

<p class="footer">pcap: <code>{pcap_path}</code> · share_id: <code>{share_id}</code></p>
</body></html>"""


def render_pillow(share_id: str, seq, counts):
    from PIL import Image, ImageDraw, ImageFont

    w, h = 1280, 1020
    img = Image.new("RGB", (w, h), "#0d1117")
    draw = ImageDraw.Draw(img)
    try:
        title = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 20)
        body = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf", 10)
        small = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 11)
        bold = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 11)
    except OSError:
        title = body = small = bold = ImageFont.load_default()

    draw.text((28, 18), "Server file exchange (pcap decode)", fill="#79c0ff", font=title)
    draw.text(
        (28, 48),
        f"joke.txt · share_id={share_id} · FILE_META session sidecar (not on receiver drive)",
        fill="#8b949e", font=small,
    )

    # Topology
    for label, x in [("Alice", 70), ("Host", 420), ("Bob", 770)]:
        draw.rounded_rectangle((x, 78, x + 130, 138), radius=8, outline="#30363d", width=2, fill="#010409")
        draw.text((x + 28, 98), label, fill="#d2a8ff", font=bold)
    draw.text((210, 88), "OFFER\nMETA\nCHUNK\nDONE", fill="#ffa657", font=body)
    draw.text((560, 100), "relay", fill="#58a6ff", font=body)
    draw.text((910, 92), "OFFER\nCHUNK\nDONE\nACCEPT", fill="#7ee787", font=body)

    y = 160
    draw.text((28, y), "Captured session frames (file transfer)", fill="#c9d1d9", font=bold)
    y += 24
    headers = ("#", "Opcode", "Stream", "Payload preview")
    col_x = [28, 48, 160, 430]
    for i, htxt in enumerate(headers):
        draw.text((col_x[i], y), htxt, fill="#8b949e", font=bold)
    y += 18
    draw.line((28, y, 1252, y), fill="#30363d")
    y += 6

    display = seq[:14] if seq else []
    for i, (stream, op, preview) in enumerate(display, 1):
        bg = "#1f2d1a" if op == "FILE_META" else "#161b22"
        draw.rectangle((28, y, 1252, y + 20), fill=bg)
        color = "#7ee787" if op == "FILE_META" else "#e6edf3"
        draw.text((col_x[0], y + 3), str(i), fill="#8b949e", font=body)
        draw.text((col_x[1], y + 3), op, fill=color, font=body)
        short_stream = stream.replace("127.0.0.1", "lo")[:28]
        draw.text((col_x[2], y + 3), short_stream, fill="#79c0ff", font=body)
        prev = preview[:52] + ("…" if len(preview) > 52 else "")
        draw.text((col_x[3], y + 3), prev, fill="#c9d1d9", font=body)
        y += 22

    y += 10
    draw.text((28, y), "Opcode counts (file ops)", fill="#c9d1d9", font=bold)
    y += 20
    file_counts = {k: v for k, v in counts.items() if k.startswith("FILE_")}
    cx = 28
    for op, n in sorted(file_counts.items()):
        draw.text((cx, y), f"{op} × {n}", fill="#ffa657" if op == "FILE_META" else "#79c0ff", font=body)
        cx += 180
        if cx > 900:
            cx = 28
            y += 18

    draw.text(
        (28, h - 36),
        "pcap: /opt/cursor/artifacts/server_file_exchange.pcap · html: server_file_exchange.html",
        fill="#8b949e", font=body,
    )
    img.save(OUT_PNG)


def main(argv):
    pcap = Path(argv[1]) if len(argv) > 1 else ART / "server_file_exchange.pcap"
    frames = Path(argv[2]) if len(argv) > 2 else ART / "server_file_exchange_frames.txt"
    share_id = argv[3] if len(argv) > 3 else "share-unknown"

    rows, counts = parse_frames(frames)
    seq = pick_file_sequence(rows)
    html = render_html(share_id, seq, counts, pcap)
    OUT_HTML.write_text(html)
    print(f"wrote {OUT_HTML}")

    try:
        subprocess.run(
            [
                "google-chrome", "--headless=new", "--disable-gpu", "--no-sandbox",
                "--disable-dev-shm-usage", "--user-data-dir=/tmp/chrome-file-exchange",
                f"--screenshot={OUT_PNG}", "--window-size=1280,1100", "--hide-scrollbars",
                f"file://{OUT_HTML}",
            ],
            check=True, timeout=20, capture_output=True,
        )
        print(f"wrote {OUT_PNG}")
    except (subprocess.CalledProcessError, subprocess.TimeoutExpired, FileNotFoundError):
        try:
            subprocess.check_call(
                [sys.executable, "-m", "pip", "install", "-q", "pillow"],
                stdout=subprocess.DEVNULL,
            )
            render_pillow(share_id, seq, counts)
            print(f"wrote {OUT_PNG} (pillow fallback)")
        except Exception as exc:
            print(f"png render failed: {exc}", file=sys.stderr)
            return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
