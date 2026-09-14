#!/usr/bin/env python3
"""Install the pinned browser PC runtime; verify every downloaded byte."""
import argparse
import hashlib
import json
from pathlib import Path
import urllib.request

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="verify local assets without downloading")
    args = parser.parse_args()
    lab = Path(__file__).resolve().parents[1] / "tools/browser-lab"
    lock = json.loads((lab / "runtime-lock.json").read_text())
    destination = lab / "vendor/qemu"
    destination.mkdir(parents=True, exist_ok=True)
    for name, expected in lock["files"].items():
        path = destination / name
        if path.exists() and hashlib.sha256(path.read_bytes()).hexdigest() == expected:
            print(f"verified {name}")
            continue
        if args.check:
            raise SystemExit(f"Missing or corrupt runtime asset: {name}")
        with urllib.request.urlopen(lock["baseUrl"] + name, timeout=120) as response:
            data = response.read()
        if hashlib.sha256(data).hexdigest() != expected:
            raise SystemExit(f"SHA-256 mismatch: {name}")
        temporary = path.with_suffix(path.suffix + ".download")
        temporary.write_bytes(data)
        temporary.replace(path)
        print(f"installed {name}")

if __name__ == "__main__":
    main()
