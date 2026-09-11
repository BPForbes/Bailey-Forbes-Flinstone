#!/usr/bin/env python3
"""ESP AT firmware stand-in on a PTY for #328 UART scan/join validation."""
from __future__ import annotations

import os
import pty
import select
import sys
import time


def main() -> int:
    master, slave = pty.openpty()
    slave_name = os.ttyname(slave)
    print(f"ESP_AT_PTY={slave_name}", flush=True)
    os.close(slave)
    buf = b""
    deadline = time.time() + float(os.environ.get("ESP_AT_PTY_TIMEOUT", "30"))
    while time.time() < deadline:
        r, _, _ = select.select([master], [], [], 0.5)
        if master not in r:
            continue
        chunk = os.read(master, 256)
        if not chunk:
            break
        buf += chunk
        while b"\n" in buf:
            line, buf = buf.split(b"\n", 1)
            cmd = line.replace(b"\r", b"").decode("ascii", "replace")
            if cmd == "AT":
                os.write(master, b"\r\nOK\r\n")
            elif cmd == "AT+CWMODE=1":
                os.write(master, b"\r\nOK\r\n")
            elif cmd == "AT+CWLAP":
                os.write(
                    master,
                    b'+CWLAP:(3,"flinstone_ci",-40,"02:11:22:33:44:55",6)\r\n'
                    b"\r\nOK\r\n",
                )
            elif cmd.startswith("AT+CWJAP="):
                os.write(master, b"WIFI CONNECTED\r\nWIFI GOT IP\r\n\r\nOK\r\n")
            elif cmd == "AT+CWQAP":
                os.write(master, b"\r\nOK\r\n")
            elif cmd:
                os.write(master, b"\r\nERROR\r\n")
    os.close(master)
    return 0


if __name__ == "__main__":
    sys.exit(main())
