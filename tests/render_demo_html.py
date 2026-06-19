#!/usr/bin/env python3
"""Render captured ANSI pane logs into one HTML file (then screenshot to
PNG with headless Chrome). Works with either the original 3-pane demo
(demo_clientA/clientB) or the multi-IP 4-pane demo (demo_multi_ip_A/B/C)."""

import sys
from ansi2html import Ansi2HTMLConverter
from pathlib import Path

ART = Path("/opt/cursor/artifacts")
multi = (ART / "demo_multi_ip_host.ansi").exists()
if multi:
    host = (ART / "demo_multi_ip_host.ansi").read_text()
    ca = (ART / "demo_multi_ip_A.ansi").read_text()
    cb = (ART / "demo_multi_ip_B.ansi").read_text()
    cc = (ART / "demo_multi_ip_C.ansi").read_text()
else:
    host = (ART / "demo_host_pane.ansi").read_text()
    ca = (ART / "demo_clientA_pane.ansi").read_text()
    cb = (ART / "demo_clientB_pane.ansi").read_text()
    cc = None

conv = Ansi2HTMLConverter(inline=True, dark_bg=True, scheme="solarized")

def block(title, text):
    body = conv.convert(text, full=False)
    return f"""
<section>
  <header>{title}</header>
  <pre>{body}</pre>
</section>
"""

title = ("P3-13 server foundations &mdash; multi-IP demo "
         "(host 10.99.0.1 + clients on 10.99.0.10 / .20 / .30)") if multi else \
        "P3-13 server foundations &mdash; live tmux demo (3 BPForbes_Flinstone_Shell processes)"
sections = [
    block("Host pane (server host 10.99.0.1:49913 &mdash; NOT loopback)", host) if multi else
    block("Host pane (server host 127.0.0.1:49913)", host),
    block("Client A pane (10.99.0.10 &rarr; member_id 2 &rarr; takes over as new host)", ca) if multi else
    block("Client A pane (server join &rarr; member_id 2 &rarr; nicked Jeff &rarr; leave)", ca),
    block("Client B pane (10.99.0.20 &rarr; nicked &quot;Bobby&quot; via Y/N prompt &rarr; private chat &rarr; auto-reconnect to new host)", cb) if multi else
    block("Client B pane (server join &rarr; member_id 3)", cb),
]
if multi and cc:
    sections.append(block("Client C pane (10.99.0.30 &rarr; explicit leave before host transfer)", cc))

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
<h1>{title}</h1>
{''.join(sections)}
</body></html>"""

out_name = "demo_multi_ip_server.html" if multi else "demo_server_foundations.html"
out = ART / out_name
out.write_text(html)
print(f"wrote {out}")
