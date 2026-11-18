# Flinstone color system

How server text is colored in Flinstone, which bytes carry each color, and how the VGA text console (the embedded VM, the freestanding image and the browser lab) draws them. Written against the `cursor/relay-chat-and-cat-sanitize-c68c` branch, where the VGA console parses ANSI colour codes.

## 1. Which color is used for what

All of these are in `userland/shell/fl_colors.[ch]`, used by the hosted shell.

| Color | Helper | Printed prefix | Used for |
|---|---|---|---|
| Blue | `fl_color_announce` | `[Server Announcement]: ` | joins, leaves, nickname changes, host announcements |
| Cyan | `fl_color_msg_from_all` | `[Server Message, From <name>]: ` | broadcast chat |
| Cyan | `fl_color_msg_from_user` | `[Server Message, <name> -> You]: ` | private message |
| Cyan | `fl_color_server_dm` | `[SERVER]: ` | direct notice (nickname collision) |
| Green | `fl_color_success` | `[Server] ` | good news (reconnected, peers can join) |
| Yellow | `fl_color_warn` | `[Server] ` | notices (session closed, elevated permissions) |
| Yellow | `fl_color_warn_local` | `[WARNING] ` | local-only warning |
| Yellow | `fl_color_prompt_yellow` | none | the nickname prompts (no newline) |
| Red | `fl_color_error` | `[ERROR] ` | errors |
| Magenta, white | none | none | defined (`KMAG`, `KWHT`) but unused |

`fl_color_msg_to_user` and `fl_color_msg_to_all` (cyan) exist but have no callers. A tagged line is one color for the whole line, then a reset. Color is written only when the stream is a terminal (`isatty`); `fl_color_set_disabled(1)` turns it off (the server tests do this). Errors and `[WARNING]` go to stderr; everything else to stdout. Each line prints between a prelude and a postlude hook (set by `shell_io_init`) that redraw the prompt.

## 2. What the hosted shell sends: ANSI escape bytes

Each color is `ESC [ <n> m`: ESC is `0x1B`, `[` is `0x5B`, `m` is `0x6D`, and the digits are ASCII. In C: `"\x1B[31m"`.

| Color | Macro | ASCII text | Hex bytes | Binary bytes |
|---|---|---|---|---|
| Reset | `KNRM` | `ESC[0m` | `1B 5B 30 6D` | `00011011 01011011 00110000 01101101` |
| Red | `KRED` | `ESC[31m` | `1B 5B 33 31 6D` | `00011011 01011011 00110011 00110001 01101101` |
| Green | `KGRN` | `ESC[32m` | `1B 5B 33 32 6D` | `00011011 01011011 00110011 00110010 01101101` |
| Yellow | `KYEL` | `ESC[33m` | `1B 5B 33 33 6D` | `00011011 01011011 00110011 00110011 01101101` |
| Blue | `KBLU` | `ESC[34m` | `1B 5B 33 34 6D` | `00011011 01011011 00110011 00110100 01101101` |
| Magenta | `KMAG` | `ESC[35m` | `1B 5B 33 35 6D` | `00011011 01011011 00110011 00110101 01101101` |
| Cyan | `KCYN` | `ESC[36m` | `1B 5B 33 36 6D` | `00011011 01011011 00110011 00110110 01101101` |
| White | `KWHT` | `ESC[37m` | `1B 5B 33 37 6D` | `00011011 01011011 00110011 00110111 01101101` |

The color parameter is 30 plus the ANSI color index (red 31 = `0x1F` = `00011111`, green 32, yellow 33, blue 34, magenta 35, cyan 36, white 37). `ESC[1;31m` is bold red; `ESC[91m` is bright red (`1B 5B 39 31 6D`).

## 3. What VGA text memory stores: attribute bytes

VGA text mode has no escape codes. Each cell is 16 bits, `(attribute << 8) | character`, stored little-endian at `0xB8000 + 2 * (row * 80 + col)`, so the character byte comes first. The attribute is `(background << 4) | foreground`, 4 bits each. The screen is 80x25. Bit 7 of the attribute blinks on real VGA, so only bits 4 to 6 carry the background here.

The palette is the one in `VM/devices/vm_sdl.c` (`s_vga_colors`), index 0 to 15:

| Index | Name | RGB (hex) | Index | Name | RGB (hex) |
|---|---|---|---|---|---|
| 0 | black | `000000` | 8 | dark gray | `555555` |
| 1 | blue | `0000AA` | 9 | light blue | `5555FF` |
| 2 | green | `00AA00` | 10 | light green | `55FF55` |
| 3 | cyan | `00AAAA` | 11 | light cyan | `55FFFF` |
| 4 | red | `AA0000` | 12 | light red | `FF5555` |
| 5 | magenta | `AA00AA` | 13 | light magenta | `FF55FF` |
| 6 | brown | `AA5500` | 14 | yellow | `FFFF55` |
| 7 | light gray | `AAAAAA` | 15 | white | `FFFFFF` |

ANSI and VGA number their colors differently (the red and blue bits are swapped). The parser uses this table, ANSI index to VGA index: `{0, 4, 2, 6, 1, 5, 3, 7}`. Equivalent formula with `n = SGR - 30`: `vga = ((n & 1) << 2) | (n & 2) | ((n & 4) >> 2)`.

The attribute each color of section 2 produces on a black background, with and without bold (`ESC[1m` sets the intensity bit `0x08`; `ESC[9Xm` sets it too):

| Color | `ESC[3Xm` attribute | binary | RGB | `ESC[1;3Xm` or `ESC[9Xm` | binary | RGB |
|---|---|---|---|---|---|---|
| Reset (`ESC[0m`) | `0x07` | `0000 0111` | `AAAAAA` | n/a | n/a | n/a |
| Red | `0x04` | `0000 0100` | `AA0000` | `0x0C` | `0000 1100` | `FF5555` |
| Green | `0x02` | `0000 0010` | `00AA00` | `0x0A` | `0000 1010` | `55FF55` |
| Yellow | `0x06` | `0000 0110` | `AA5500` | `0x0E` | `0000 1110` | `FFFF55` |
| Blue | `0x01` | `0000 0001` | `0000AA` | `0x09` | `0000 1001` | `5555FF` |
| Magenta | `0x05` | `0000 0101` | `AA00AA` | `0x0D` | `0000 1101` | `FF55FF` |
| Cyan | `0x03` | `0000 0011` | `00AAAA` | `0x0B` | `0000 1011` | `55FFFF` |
| White | `0x07` | `0000 0111` | `AAAAAA` | `0x0F` | `0000 1111` | `FFFFFF` |

The hosted helpers send the plain codes (no bold), so on VGA blue is dark blue (`0x01`) and yellow is brown (`0x06`). A red `F` is the word `0x0446`: character `0x46` in the low byte, attribute `0x04` in the high byte.

## 4. How the VGA console draws ANSI (this branch)

`kernel/drivers/freestanding_x86_64/vga_sgr.h` is a small parser shared by the bare-metal VGA driver and the WebAssembly shell's VGA buffer. `fl_fs_vga_putc` feeds every byte to `fl_vga_sgr_feed` first: a byte that is part of an escape sequence is not drawn, and everything else is drawn with the current attribute (default `0x07`, `FL_VGA_SGR_DEFAULT_ATTR`).

| SGR code | Effect on the attribute |
|---|---|
| `0` | foreground `7`, background `0` (attribute `0x07`) |
| `1` | foreground `\|= 0x08` (bright) |
| `22` | foreground `&= 0x07` (normal intensity) |
| `30` to `37` | foreground = bright bit kept, plus the mapped index |
| `39` | default foreground (`7`), bright bit kept |
| `40` to `47` | background = the mapped index |
| `49` | background `0` |
| `90` to `97` | foreground = `0x08` plus the mapped index |
| `100` to `107` | background = the mapped index (bit 7 would blink, so bright backgrounds show as the normal color) |
| `38`, `48` | stops reading the sequence (extended colors are not supported) |
| any other `ESC [ ... <final>` | swallowed, not drawn |

A sequence holds at most 8 numbers (each stops growing at 1000); one with more, or a malformed one, is ignored. The untrusted-text filter below is stricter: at most 3 digits per number. Backspace erases with the current attribute.

Untrusted text (relay chat, host announcements, and `cat` / `type` output) goes through `emit_text_unit` and `safe_sgr_len` in `kernel/freestanding/x86_64/commands.c`: a sequence made only of the colour codes above is kept, every other escape sequence and control byte shows as caret text (`^[[2J`, `^G`), valid UTF-8 shows as its code page 437 glyph on VGA, and a byte that is not valid UTF-8 shows as `\xNN`. A message or file line that used colour ends with `ESC[0m` (`SGR_RESET`), so a sender cannot recolour the receiver's prompt, and a sender's code that resets the foreground (0, 39) brings back the receiver's chat cyan or announcement blue. Relayed host announcements print as `[Server Announcement]: ` in bright blue (SGR 94, attribute 0x09) on the VGA screen, since VGA blue (0x01) is hard to read on black. Details are in `tools/browser-lab/README.md`.

## 5. Files

| Part | Files | What it does |
|---|---|---|
| Hosted color system | `userland/shell/fl_colors.h`, `userland/shell/fl_colors.c` | ANSI macros and every `fl_color_*` helper |
| Server callers | `userland/command/cmd_server.c` (`client_event_print`, nickname prompts), `kernel/core/net/net_server.c` (`emit_local_announce`), `userland/shell/cmd_journal.c` | pick the helper for each event |
| Display driver | `kernel/drivers/display_driver.c` | hosted: `printf` and ANSI cursor codes. Bare-metal: writes VGA memory at `0xB8000` with `impl->color`, default `0x07` |
| SGR parser | `kernel/drivers/freestanding_x86_64/vga_sgr.h` | the ANSI to VGA attribute conversion (section 4) |
| Freestanding VGA | `kernel/drivers/freestanding_x86_64/vga.c`, `vga.h` | VGA text at `0xB8000`; `fl_fs_vga_putc` feeds the parser |
| WebAssembly VGA buffer | `tools/browser-lab/wasm/platform_wasm.c` | the same parser over its own text buffer |
| Untrusted-text filter | `kernel/freestanding/x86_64/commands.c` | keeps colour-only sequences, shows other codes as caret text and UTF-8 as code page 437 |
| Embedded x86 VM | `VM/devices/vm_sdl.c` | the 16-color palette; draws foreground `attr & 0x0F` on background `attr >> 4` |
| VM memory and fonts | `VM/devices/vm_mem.h` (`GUEST_VGA_BASE 0xb8000`), `VM/devices/vm_font.[ch]`, `VM/devices/vm_display.c` | guest VGA text location, glyphs, text refresh (no color handling in `vm_display.c`) |
| Tests | `tests/test_fs_vga_wrap.c`, `scripts/test_freestanding_shell.py` | VGA wrapping and the SGR cases |

## 6. Migrating server text to bare metal

- **The console side is done.** If the bytes `ESC[34m ... ESC[0m` reach `fl_fs_vga_putc`, the VGA console draws them in color. The same bytes still work on a serial terminal.
- **The image colours its own server text, on the VGA screen only.** `commands.c` sends the SGR bytes through `fl_fs_vga_putc` (`vga_sgr`, `emit_in`, `emit_tagged`), never through serial or the `>` redirect buffer, so the lab page's line parser (`SERVER_RELAY`, `SERVER_FILE`, `SWITCHUSER`, `LAB_NETMODE`) and saved files see plain text. Machine lines are never coloured. Formats and colours follow the hosted helpers: chat is `[Server Message, From <sender>]: <text>` in cyan (`fl_color_msg_from_all`, `ESC[36m`, VGA `0x03`); `[Server] <text>` green for success (`fl_color_success`, `0x02`) and yellow for notices (`fl_color_warn`, `0x06`); `[ERROR] <text>` red (`fl_color_error`, `0x04`); the `netlab:` tag is cyan (image only). Every coloured span ends with `ESC[0m`. `fl_colors.c` stays hosted-only (it uses `isatty` and `stdio`).
- **Pick the readable variants.** The hosted helpers send plain `ESC[34m` and `ESC[33m`, which are dark blue and brown on VGA. For bare metal, `ESC[1;34m` (`0x09`) and `ESC[1;33m` (`0x0E`) read better on black.
- **No `isatty`.** `fl_color_is_enabled_for` needs a bare-metal answer; the image would decide color on or off from its own state.
