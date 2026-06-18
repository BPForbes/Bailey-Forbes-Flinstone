#!/usr/bin/env python3
import os
import pty
import select
import signal
import sys
import time


def read_available(fd, deadline, output):
    while time.time() < deadline:
        timeout = max(0.0, deadline - time.time())
        ready, _, _ = select.select([fd], [], [], timeout)
        if not ready:
            break
        try:
            chunk = os.read(fd, 4096)
        except OSError:
            break
        if not chunk:
            break
        output.extend(chunk)


def wait_for(fd, output, marker, timeout):
    deadline = time.time() + timeout
    while time.time() < deadline:
        if marker in output.decode("utf-8", errors="replace"):
            return True
        read_available(fd, min(time.time() + 0.05, deadline), output)
    return marker in output.decode("utf-8", errors="replace")


def main():
    binary = "./BPForbes_Flinstone_Shell"
    pid, fd = pty.fork()
    if pid == 0:
        os.execl(binary, binary, "whoami")

    output = bytearray()
    try:
        if not wait_for(fd, output, "shell> ", 5.0):
            raise AssertionError("interactive prompt did not appear")

        os.write(fd, b"abc")
        read_available(fd, time.time() + 0.2, output)

        before_ctrl_c = len(output)
        os.write(fd, b"\x03")
        deadline = time.time() + 2.0
        while time.time() < deadline:
            post_ctrl = output[before_ctrl_c:].decode("utf-8", errors="replace")
            if "^C" in post_ctrl and "shell> " in post_ctrl:
                break
            read_available(fd, min(time.time() + 0.05, deadline), output)
        else:
            raise AssertionError("Ctrl+C did not clear the line and redraw shell> prompt")

        os.write(fd, b"exit\n")
        read_available(fd, time.time() + 2.0, output)
    finally:
        try:
            done_pid, _ = os.waitpid(pid, os.WNOHANG)
            if done_pid == 0:
                os.kill(pid, signal.SIGTERM)
                os.waitpid(pid, 0)
        except ChildProcessError:
            pass
        os.close(fd)

    text = output.decode("utf-8", errors="replace")
    if "execvp: No such file or directory" in text:
        raise AssertionError("Ctrl+C left the old input buffer to execute as a command")
    print("Ctrl+C clears the interactive input line and redraws shell>.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
