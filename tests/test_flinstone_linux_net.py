#!/usr/bin/env python3
"""End-to-end checks for tools/FlinstoneLinuxNet/FlinstoneLinuxNet."""

from __future__ import annotations

import os
import socket
import subprocess
import sys
import tempfile
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
            conn.sendall(b"linux:" + data)


def write_fake_nmcli(directory: Path) -> Path:
    path = directory / "nmcli"
    path.write_text(
        "#!/bin/sh\n"
        "case \"$*\" in\n"
        "  *\"-t -f DEVICE,TYPE device\"*) printf 'wlan0:wifi\\n' ;;\n"
        "  *\"device wifi list\"*) printf 'RouterLan:02\\\\:aa\\\\:bb\\\\:cc\\\\:dd\\\\:ee:82:36:5180:WPA3\\n' ;;\n"
        "  *\"device show wlan0\"*) printf 'GENERAL.STATE:100 (connected)\\nGENERAL.CONNECTION:RouterLan\\nIP4.ADDRESS[1]:192.168.1.235/24\\nIP4.GATEWAY:192.168.1.1\\n' ;;\n"
        "  *\"device wifi connect\"*) printf 'Device activated\\n' ;;\n"
        "  *\"device disconnect\"*) printf 'Device disconnected\\n' ;;\n"
        "  *) echo \"unexpected nmcli args: $*\" >&2; exit 1 ;;\n"
        "esac\n"
    )
    path.chmod(0o700)
    return path


def wait_for_bridge(proc: subprocess.Popen[str]) -> None:
    deadline = time.time() + 5.0
    while time.time() < deadline:
        line = proc.stdout.readline() if proc.stdout else ""
        if "result=ok" in line:
            return
        if proc.poll() is not None:
            break
    stderr = proc.stderr.read() if proc.stderr else ""
    raise RuntimeError(f"bridge did not start (rc={proc.poll()}): {stderr}")


def main() -> int:
    repo = Path(__file__).resolve().parents[1]
    helper = repo / "tools" / "FlinstoneLinuxNet" / "FlinstoneLinuxNet"
    if not helper.exists():
        raise RuntimeError(f"missing helper binary: {helper}")

    with tempfile.TemporaryDirectory(prefix="fl-linux-net-") as td:
        fake_nmcli = write_fake_nmcli(Path(td))
        env = os.environ.copy()
        env["FL_NET_WIFI_NMCLI"] = str(fake_nmcli)
        env["FL_NET_WIFI_IFACE"] = "wlan0"

        platform = subprocess.check_output([str(helper), "platform"], text=True, env=env)
        assert platform.strip() == "platform=linux"

        scan = subprocess.check_output([str(helper), "wifi-scan"], text=True, env=env)
        assert "ssid=RouterLan" in scan
        assert "bssid=02:aa:bb:cc:dd:ee" in scan
        assert "auth=wpa3" in scan

        status = subprocess.check_output([str(helper), "wifi-status"], text=True, env=env)
        assert "state=connected" in status
        assert "ipv4=192.168.1.235" in status
        assert "gateway=192.168.1.1" in status

    port = pick_port()
    ready = threading.Event()
    thread = threading.Thread(target=run_echo_server, args=(port, ready), daemon=True)
    thread.start()
    if not ready.wait(3.0):
        raise RuntimeError("echo server did not start")

    proc = subprocess.Popen(
        [str(helper), "server-bridge", "127.0.0.2", str(port), "127.0.0.1"],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    try:
        wait_for_bridge(proc)
        with socket.create_connection(("127.0.0.2", port), timeout=3.0) as client:
            client.sendall(b"probe")
            data = client.recv(1024)
        if data != b"linux:probe":
            raise RuntimeError(f"unexpected bridge payload: {data!r}")
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=3.0)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait(timeout=3.0)

    thread.join(timeout=3.0)
    print("test_flinstone_linux_net: wifi protocol + bridge... ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
