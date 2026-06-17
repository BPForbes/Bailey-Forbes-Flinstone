#!/usr/bin/env python3
"""End-to-end loopback relay test for tools/network_bridge.py."""

from __future__ import annotations

import socket
import subprocess
import sys
import threading
import time
from pathlib import Path


def pick_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.bind(("127.0.0.1", 0))
        return int(sock.getsockname()[1])


def run_echo_server(port: int, ready: threading.Event) -> None:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        sock.bind(("127.0.0.1", port))
        sock.listen(1)
        ready.set()
        conn, _ = sock.accept()
        with conn:
            data = conn.recv(1024)
            conn.sendall(b"echo:" + data)


def wait_for_bridge(proc: subprocess.Popen[str]) -> None:
    deadline = time.time() + 5.0
    while time.time() < deadline:
        line = proc.stdout.readline() if proc.stdout else ""
        if "bridge:" in line:
            return
        if proc.poll() is not None:
            break
    stderr = proc.stderr.read() if proc.stderr else ""
    raise RuntimeError(f"bridge did not start (rc={proc.poll()}): {stderr}")


def main() -> int:
    repo = Path(__file__).resolve().parents[1]
    bridge = repo / "tools" / "network_bridge.py"
    port = pick_port()
    ready = threading.Event()
    thread = threading.Thread(target=run_echo_server, args=(port, ready), daemon=True)
    thread.start()
    if not ready.wait(3.0):
        raise RuntimeError("echo server did not start")

    proc = subprocess.Popen(
        [sys.executable, str(bridge), "127.0.0.2", str(port), "127.0.0.1"],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    try:
        wait_for_bridge(proc)
        with socket.create_connection(("127.0.0.2", port), timeout=3.0) as client:
            client.sendall(b"lan-probe")
            data = client.recv(1024)
        if data != b"echo:lan-probe":
            raise RuntimeError(f"unexpected relay payload: {data!r}")
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=3.0)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait(timeout=3.0)

    thread.join(timeout=3.0)
    print("test_network_bridge: 127.0.0.2 -> 127.0.0.1 relay... ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
