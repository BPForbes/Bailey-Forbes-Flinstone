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
        "WHOAMI flinstone",
        "SWITCHUSER user=root",
        "WHOAMI root",
        "ok",
        "SWITCHUSER user=demo",
        "WHOAMI demo",
        "SERVER_RELAY msg hi",
        "readme.txt",
    ]
    missing = [item for item in checks if item not in out]
    if missing:
        sys.stderr.write(out)
        raise SystemExit("wasm host shell missing: " + ", ".join(missing))
    print("test_wasm_shell: PASS")


if __name__ == "__main__":
    main()
