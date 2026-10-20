#!/usr/bin/env python3
"""Host-gcc smoke test for the Emscripten/WASM lab shell."""
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HOST = ROOT / "dist" / "flintstone_wasm_host"


def main():
    if not HOST.is_file():
        raise SystemExit(f"missing {HOST}; run ./scripts/build_wasm_lab.sh")
    commands = [
        "whoami",
        "switchuser root",
        "whoami",
        "useradd demo",
        "demo",
        "switchuser demo",
        "whoami",
        "server msg hi",
        "dir",
        # Sending a file: the guest hands it to the page as one SERVER_FILE line.
        "write note.txt hi bob",
        "server file -user bob note.txt",
        # Receiving one: it waits in the page until the user accepts; the page then types it
        # in hex parts, and an existing name is never replaced.
        "server file accept",
        "server file decline",
        "filerecv none",
        "filerecv begin alice got.txt",
        "filerecv data 68656c6c6f",
        "filerecv data 2c20626f62",
        "filerecv end",
        "cat got.txt",
        "filerecv begin alice got.txt",
        "filerecv data 6869",
        "filerecv end",
        "filerecv begin alice ../evil",
    ]
    result = subprocess.run(
        [str(HOST), *commands],
        check=True,
        capture_output=True,
        text=True,
    )
    out = result.stdout
    checks = [
        "FLINTSTONE_KERNEL_BOOT_OK",
        "shell>",
        "WHOAMI Flinstone",
        "SWITCHUSER user=Root",
        "WHOAMI Root",
        "ok",
        "SWITCHUSER user=demo",
        "WHOAMI demo",
        "SERVER_RELAY msg hi",
        "readme.txt",
        "SERVER_FILE bob note.txt 686920626f62",
        "server file: sending note.txt to bob (6 bytes)",
        "SERVER_FILE_ACCEPT",
        "SERVER_FILE_DECLINE",
        "server file: files waiting for you were declined",
        "server file: no files waiting",
        "received got.txt from alice (10 bytes)",
        "hello, bob",
        "filerecv: got.txt from alice not saved: a file with that name exists",
        "filerecv: refused (bad sender or file name)",
    ]
    missing = [item for item in checks if item not in out]
    if missing:
        sys.stderr.write(out)
        raise SystemExit("wasm host shell missing: " + ", ".join(missing))
    print("test_wasm_shell: PASS")


if __name__ == "__main__":
    main()
