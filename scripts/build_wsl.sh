#!/bin/sh
# One-shot build for WSL: deps + VM with SDL2. Run from project root.
set -e
cd "$(dirname "$0")/.."

echo "[WSL build] Checking deps..."
if ! pkg-config --exists sdl2 2>/dev/null; then
    echo "[WSL build] SDL2 not found. Installing..."
    sudo apt-get update -qq
    sudo apt-get install -y libsdl2-dev build-essential
fi

echo "[WSL build] Building VM with SDL2..."
make vm-sdl

echo "[WSL build] Done. Run: ./scripts/run_vm_wsl.sh"
