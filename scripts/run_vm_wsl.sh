#!/bin/sh
# Run Flinstone VM in SDL2 window (WSLg-friendly). Run from project root.
cd "$(dirname "$0")/.."

if [ ! -f ./BPForbes_Flinstone_Shell ]; then
    echo "Build first: ./scripts/build_wsl.sh"
    exit 1
fi

# Ensure we have VM+SDL build (quick check: binary exists and has vm)
./BPForbes_Flinstone_Shell -Virtualization -y -vm
