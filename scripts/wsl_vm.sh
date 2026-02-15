#!/bin/sh
# Single-script: build VM + SDL2 and run popup. WSLg-friendly.
# Detects WSL; installs deps if missing. Run from project root.
set -e
cd "$(dirname "$0")/.."

if [ -n "${WSL_DISTRO_NAME}" ] || [ -n "${WAYLAND_DISPLAY}" ] || uname -r 2>/dev/null | grep -q Microsoft; then
    :
fi

echo "[wsl_vm] Checking deps..."
if ! pkg-config --exists sdl2 2>/dev/null; then
    echo "[wsl_vm] SDL2 not found. Installing..."
    sudo apt-get update -qq
    sudo apt-get install -y libsdl2-dev build-essential
fi

echo "[wsl_vm] Building VM with SDL2..."
make vm-sdl

echo "[wsl_vm] Launching VM..."
./BPForbes_Flinstone_Shell -Virtualization -y -vm
