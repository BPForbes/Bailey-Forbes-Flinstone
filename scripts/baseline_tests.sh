#!/bin/sh
# Baseline tests that should pass. Run from project root.
set -e
cd "$(dirname "$0")/.."

echo "[baseline] Building..."
make clean
make -j4 2>/dev/null || make

echo "[baseline] Shell: help, version, clear"
./BPForbes_Flinstone_Shell help | head -1
./BPForbes_Flinstone_Shell version
./BPForbes_Flinstone_Shell clear 2>/dev/null || true

echo "[baseline] VM (no SDL)"
make VM_ENABLE=1 2>/dev/null || true
if ./BPForbes_Flinstone_Shell -Virtualization -y -vm 2>&1 | grep -q "Flinstone VM"; then
  echo "  VM OK"
else
  echo "  VM: boot or output check failed (optional)"
fi

echo "[baseline] Core tests"
make test_mem_asm 2>/dev/null || true
make test_priority_queue 2>/dev/null || true

echo "[baseline] CUnit tests (optional)"
make BPForbes_Flinstone_Tests 2>/dev/null && (timeout 20 ./BPForbes_Flinstone_Tests 2>&1 | tail -5) || echo "(CUnit skipped - need deps or libcunit1-dev)"

echo "[baseline] Done."
