#!/usr/bin/env python3
"""Render sender/receiver CLI flow through the shell FS kernel, ending in two cat commands."""

import subprocess
import sys
from pathlib import Path

ART = Path("/opt/cursor/artifacts")
OUT = ART / "demo_file_share_content_match.png"
HTML = ART / "demo_file_share_content_match.html"

QUOTE = "The road goes ever on and on, down from the door where it began."
SENDER_PATH = "/workspace/.tmp_msg_file_demo/demo_share.txt"
RECEIVER_PATH = "server_shared/demo_share.txt"
SHARE_ID = "share-1780267182-1"
PORT = "49917"

SENDER_LINES = [
    ("prompt", f"shell> server join 127.0.0.1:{PORT}"),
    ("server", "[Server] joined as 'flinstone {2}' (member_id 2)"),
    ("prompt", f"shell> cat {SENDER_PATH}"),
    ("cat_out", QUOTE),
    ("prompt", f"shell> server file -user Bob {SENDER_PATH} -vw"),
    ("server", "[Server] file offer sent"),
]

RECEIVER_LINES = [
    ("prompt", f"shell> server join 127.0.0.1:{PORT}"),
    ("server", "[Server] joined as 'flinstone {3}' (member_id 3)"),
    ("prompt", "shell> server file inbox"),
    ("server", f"{SHARE_ID}  demo_share.txt  from Alice"),
    ("prompt", f"shell> server file accept {SHARE_ID} --server-share"),
    ("server", f"[Server] accepted {SHARE_ID} (server_shared)"),
    ("prompt", f"shell> cat {RECEIVER_PATH}"),
    ("cat_out", QUOTE),
]


def render_html_lines(lines):
    parts = []
    for kind, text in lines:
        cls = {
            "prompt": "line prompt",
            "server": "line server",
            "cat_out": "line cat-out",
        }[kind]
        parts.append(f'<div class="{cls}">{text}</div>')
    return "\n".join(parts)


html = f"""<!doctype html>
<html><head><meta charset="utf-8"><title>File share CLI flow</title>
<style>
  * {{ box-sizing: border-box; }}
  body {{
    margin: 0; padding: 28px;
    background: #0d1117; color: #e6edf3;
    font-family: Menlo, Consolas, "Courier New", monospace;
  }}
  h1 {{ margin: 0 0 6px 0; font-size: 21px; color: #79c0ff; }}
  .subtitle {{ margin: 0 0 18px 0; color: #8b949e; font-size: 13px; line-height: 1.5; max-width: 1180px; }}
  .badge {{
    display: inline-block; margin-bottom: 16px; padding: 5px 12px;
    background: #238636; color: #fff; border-radius: 999px; font-size: 12px; font-weight: 700;
  }}
  .grid {{
    display: grid; grid-template-columns: 1fr 64px 1fr; gap: 0; max-width: 1180px;
  }}
  .term {{
    background: #010409; border: 1px solid #30363d; border-radius: 8px; overflow: hidden;
  }}
  .term-h {{
    padding: 10px 14px; background: #161b22; border-bottom: 1px solid #30363d;
    font-size: 12px; font-weight: 700; color: #d2a8ff;
  }}
  .term-h span {{ color: #ffa657; font-weight: 400; }}
  .term-b {{ padding: 14px 14px 16px 14px; font-size: 11.5px; line-height: 1.55; }}
  .line.prompt {{ color: #7ee787; margin-top: 2px; }}
  .line.server {{ color: #79c0ff; padding-left: 0; }}
  .line.cat-out {{
    margin: 6px 0 8px 0; padding: 10px 12px; color: #f0f6fc;
    background: #0d1117; border-left: 3px solid #58a6ff; border-radius: 4px;
    font-size: 13px; line-height: 1.5;
  }}
  .vfs-note {{
    margin-top: 10px; padding-top: 8px; border-top: 1px dashed #30363d;
    color: #8b949e; font-size: 10.5px;
  }}
  .equals {{
    display: flex; align-items: center; justify-content: center;
    font-size: 36px; font-weight: 700; color: #3fb950;
  }}
  .footer {{
    margin-top: 16px; max-width: 1180px; padding: 12px 14px;
    background: #161b22; border: 1px solid #30363d; border-radius: 8px;
    font-size: 11px; color: #8b949e; line-height: 1.55;
  }}
  .footer strong {{ color: #c9d1d9; }}
</style></head><body>
<h1>Server file share — CLI through FS kernel</h1>
<p class="subtitle">Each user runs <code>server</code> verbs in <strong>BPForbes_Flinstone_Shell</strong> while joined to the hub.
The final step on both sides is <code>cat</code>, which reads through the VFS layer (<code>fs_jail</code> / host FS).</p>
<div class="badge">TWO cat COMMANDS · SAME CONTENT</div>
<div class="grid">
  <section class="term">
    <div class="term-h">Sender <span>Alice · member 2</span></div>
    <div class="term-b">
      {render_html_lines(SENDER_LINES)}
      <div class="vfs-note">cat → VFS read of sender-local path before FILE_OFFER / FILE_CHUNK wire transfer</div>
    </div>
  </section>
  <div class="equals">=</div>
  <section class="term">
    <div class="term-h">Receiver <span>Bob · member 3</span></div>
    <div class="term-b">
      {render_html_lines(RECEIVER_LINES)}
      <div class="vfs-note">accept --server-share → server_shared_fs write, then cat → VFS read of saved copy</div>
    </div>
  </section>
</div>
<div class="footer">
  <strong>Flow:</strong> join → server file offer/chunk/done over TCP session → inbox/accept →
  <strong>cat</strong> on each side. Share id <code>{SHARE_ID}</code> labels the offer; filename stays <code>demo_share.txt</code>.
</div>
</body></html>"""

HTML.write_text(html)
print(f"wrote {HTML}")


def draw_terminal_panel(draw, x, y, w, h, title, lines, fonts, note):
    mono, small, quote = fonts
    draw.rounded_rectangle((x, y, x + w, y + h), radius=8, outline="#30363d", width=2, fill="#010409")
    draw.rounded_rectangle((x, y, x + w, y + 34), radius=8, fill="#161b22")
    draw.rectangle((x, y + 20, x + w, y + 34), fill="#161b22")
    draw.text((x + 12, y + 10), title, fill="#d2a8ff", font=small)

    cy = y + 44
    for kind, text in lines:
        if kind == "prompt":
            draw.text((x + 12, cy), text, fill="#7ee787", font=mono)
            cy += 18
        elif kind == "server":
            draw.text((x + 12, cy), text, fill="#79c0ff", font=mono)
            cy += 18
        elif kind == "cat_out":
            wrapped = wrap(draw, text, quote, w - 48)
            box_h = 16 + len(wrapped) * 22
            draw.rounded_rectangle((x + 12, cy, x + w - 12, cy + box_h), radius=4, fill="#0d1117", outline="#58a6ff", width=2)
            ty = cy + 10
            for line in wrapped:
                draw.text((x + 20, ty), line, fill="#f0f6fc", font=quote)
                ty += 22
            cy += box_h + 8
    draw.line((x + 12, cy, x + w - 12, cy), fill="#30363d", width=1)
    cy += 8
    for line in wrap(draw, note, mono, w - 36):
        draw.text((x + 12, cy), line, fill="#8b949e", font=mono)
        cy += 16


def wrap(draw, text, font, width):
    words = text.split()
    lines, line = [], ""
    for word in words:
        trial = f"{line} {word}".strip()
        if draw.textlength(trial, font=font) <= width:
            line = trial
        else:
            if line:
                lines.append(line)
            line = word
    if line:
        lines.append(line)
    return lines or [text]


def render_pillow():
    from PIL import Image, ImageDraw, ImageFont

    w, h = 1280, 920
    img = Image.new("RGB", (w, h), "#0d1117")
    draw = ImageDraw.Draw(img)
    try:
        title_font = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 22)
        sub_font = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 13)
        small_font = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 12)
        mono = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf", 11)
        quote = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 13)
    except OSError:
        title_font = sub_font = small_font = mono = quote = ImageFont.load_default()

    draw.text((28, 24), "Server file share — CLI through FS kernel", fill="#79c0ff", font=title_font)
    sub = "server verbs while joined · final step on each side: cat (VFS read)"
    draw.text((28, 54), sub, fill="#8b949e", font=sub_font)
    draw.rounded_rectangle((28, 82, 248, 112), radius=14, fill="#238636")
    draw.text((44, 88), "TWO cat COMMANDS · SAME CONTENT", fill="#ffffff", font=small_font)

    fonts = (mono, small_font, quote)
    draw_terminal_panel(
        draw, 28, 130, 580, 720,
        "Sender (Alice · member 2)", SENDER_LINES, fonts,
        "cat → VFS read of sender-local path before wire transfer",
    )
    draw_terminal_panel(
        draw, 672, 130, 580, 720,
        "Receiver (Bob · member 3)", RECEIVER_LINES, fonts,
        "accept --server-share → server_shared_fs, then cat → VFS read",
    )
    draw.text((628, 470), "=", fill="#3fb950", font=title_font)

    foot = (
        f"join → server file offer/chunk/done → inbox/accept → cat. "
        f"Share id {SHARE_ID} labels the offer; filename demo_share.txt on both sides."
    )
    draw.rounded_rectangle((28, 870, 1252, 900), radius=6, fill="#161b22", outline="#30363d")
    draw.text((40, 878), foot, fill="#8b949e", font=mono)
    img.save(OUT)


try:
    subprocess.run(
        [
            "google-chrome", "--headless=new", "--disable-gpu", "--no-sandbox",
            "--disable-dev-shm-usage", "--user-data-dir=/tmp/chrome-file-match-demo",
            f"--screenshot={OUT}", "--window-size=1280,920", "--hide-scrollbars",
            f"file://{HTML}",
        ],
        check=True, timeout=15, capture_output=True,
    )
    print(f"wrote {OUT}")
except (subprocess.CalledProcessError, subprocess.TimeoutExpired) as exc:
    try:
        subprocess.check_call(
            [sys.executable, "-m", "pip", "install", "-q", "pillow"],
            stdout=subprocess.DEVNULL,
        )
        render_pillow()
        print(f"wrote {OUT} (pillow fallback)")
    except Exception as pill_exc:
        print(f"chrome failed: {exc}", file=sys.stderr)
        print(f"pillow failed: {pill_exc}", file=sys.stderr)
        sys.exit(1)
