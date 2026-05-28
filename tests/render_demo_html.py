#!/usr/bin/env python3
"""Render the three captured ANSI pane logs side by side into one HTML file
plus a PNG via Pillow text rendering. The HTML is what we screenshot."""

import sys
from ansi2html import Ansi2HTMLConverter
from pathlib import Path

ART = Path("/opt/cursor/artifacts")
host = (ART / "demo_host_pane.ansi").read_text()
ca = (ART / "demo_clientA_pane.ansi").read_text()
cb = (ART / "demo_clientB_pane.ansi").read_text()

conv = Ansi2HTMLConverter(inline=True, dark_bg=True, scheme="solarized")

def block(title, text):
    body = conv.convert(text, full=False)
    return f"""
<section>
  <header>{title}</header>
  <pre>{body}</pre>
</section>
"""

html = f"""<!doctype html>
<html><head><meta charset="utf-8"><title>P3-13 server foundations demo</title>
<style>
  body {{
    background: #0d1117; color: #e6edf3;
    font-family: 'Menlo','Consolas',monospace;
    margin: 24px; line-height: 1.35;
  }}
  h1 {{ color: #79c0ff; margin: 0 0 16px 0; font-size: 18px; }}
  section {{
    background: #161b22; border: 1px solid #30363d; border-radius: 6px;
    margin-bottom: 16px;
  }}
  section header {{
    background: #21262d; padding: 8px 12px; font-weight: bold;
    color: #d2a8ff; border-bottom: 1px solid #30363d; font-size: 13px;
  }}
  pre {{ margin: 0; padding: 12px; font-size: 12px; white-space: pre-wrap;
        overflow-x: auto; }}
</style></head><body>
<h1>P3-13 server foundations &mdash; live tmux demo (3 BPForbes_Flinstone_Shell processes)</h1>
{block("Host pane (server host 127.0.0.1:49913)", host)}
{block("Client A pane (server join &rarr; member_id 2 &rarr; nicked Jeff &rarr; leave)", ca)}
{block("Client B pane (server join &rarr; member_id 3)", cb)}
</body></html>"""

out = ART / "demo_server_foundations.html"
out.write_text(html)
print(f"wrote {out}")
