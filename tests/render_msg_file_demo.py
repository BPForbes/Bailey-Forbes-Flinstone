#!/usr/bin/env python3
"""Render demo_msg_file_* ANSI captures to HTML and PNG."""

import subprocess
import sys
from pathlib import Path

ART = Path("/opt/cursor/artifacts")

try:
    from ansi2html import Ansi2HTMLConverter
except ImportError:
    subprocess.check_call([sys.executable, "-m", "pip", "install", "-q", "ansi2html"])
    from ansi2html import Ansi2HTMLConverter

conv = Ansi2HTMLConverter(inline=True, dark_bg=True, scheme="solarized")

panes = [
    ("Host (server host + public file offer)", "demo_msg_file_host.ansi"),
    ("Alice / member 2 (broadcast msg + private file send)", "demo_msg_file_alice.ansi"),
    ("Bob / member 3 (private msg reply + inbox accept)", "demo_msg_file_bob.ansi"),
]

sections = []
for title, fname in panes:
    path = ART / fname
    if not path.exists():
        print(f"missing {path}", file=sys.stderr)
        sys.exit(1)
    body = conv.convert(path.read_text(errors="replace"), full=False)
    sections.append(
        f'<section><header>{title}</header><pre>{body}</pre></section>'
    )

html = f"""<!doctype html>
<html><head><meta charset="utf-8"><title>Server msg + file share demo</title>
<style>
  body {{
    background: #0d1117; color: #e6edf3;
    font-family: Menlo, Consolas, monospace; margin: 20px;
  }}
  h1 {{ color: #79c0ff; font-size: 20px; margin-bottom: 12px; }}
  p.sub {{ color: #8b949e; font-size: 13px; margin: 0 0 16px 0; }}
  section {{
    background: #161b22; border: 1px solid #30363d; border-radius: 6px;
    margin-bottom: 14px;
  }}
  section header {{
    background: #21262d; padding: 8px 12px; font-weight: bold;
    color: #d2a8ff; border-bottom: 1px solid #30363d; font-size: 13px;
  }}
  pre {{ margin: 0; padding: 10px; font-size: 11px; white-space: pre-wrap; }}
</style></head><body>
<h1>Server messaging + native file share (tmux, 3 shells)</h1>
<p class="sub">127.0.0.1 hub · server msg broadcast/private · server file -user / -all · inbox accept --server-share · sender/receiver <code>cat</code> the shared quote</p>
{''.join(sections)}
</body></html>"""

html_path = ART / "demo_msg_file_server.html"
html_path.write_text(html)
print(f"wrote {html_path}")

png_path = ART / "demo_msg_file_server.png"
subprocess.run(
    [
        "google-chrome",
        "--headless=new",
        "--disable-gpu",
        "--no-sandbox",
        "--user-data-dir=/tmp/chrome-msg-file-demo",
        f"--screenshot={png_path}",
        "--window-size=1280,2600",
        f"file://{html_path}",
    ],
    check=True,
    timeout=30,
)
print(f"wrote {png_path}")
