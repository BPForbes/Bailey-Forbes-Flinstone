#!/usr/bin/env python3
"""Run a KeyQuorum CLI command on a pseudo-terminal and answer each passphrase prompt.

The keyquorum and keyquorum-device commands read passphrases from the terminal only (rpassword
opens /dev/tty), so a test fixture cannot pipe them in. Usage:

    kq_pty_answer.py PASSPHRASE_FILE PROGRAM [ARGS...]

Every prompt the program prints that ends in ':' is answered with the first line of
PASSPHRASE_FILE. The passphrase is never on a command line or in the environment, and never
echoed: the terminal has echo off while it is typed, and this script does not print the
program's output back unless the program fails. Exits with the program's status.
"""
import os
import pty
import sys


def main() -> int:
    if len(sys.argv) < 3:
        sys.stderr.write("usage: kq_pty_answer.py PASSPHRASE_FILE PROGRAM [ARGS...]\n")
        return 2
    with open(sys.argv[1], "rb") as f:
        secret = f.readline().rstrip(b"\r\n")
    pid, fd = pty.fork()
    if pid == 0:
        try:
            os.execv(sys.argv[2], sys.argv[2:])
        finally:
            os._exit(127)
    out = b""
    pending = b""
    while True:
        try:
            chunk = os.read(fd, 4096)
        except OSError:
            break
        if not chunk:
            break
        out += chunk
        pending += chunk
        if pending.rstrip().endswith(b":"):
            os.write(fd, secret + b"\n")
            pending = b""
    _, status = os.waitpid(pid, 0)
    code = os.waitstatus_to_exitcode(status)
    # Only the program's own lines (public keys and device ids); the typed answer is not echoed.
    sys.stdout.buffer.write(out.replace(b"\r\n", b"\n"))
    return code


if __name__ == "__main__":
    sys.exit(main())
