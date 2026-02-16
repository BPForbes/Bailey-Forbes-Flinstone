#!/bin/bash
# Build-time gate: fail if stub sentinels exist in driver code.
# Prevents stub drivers from being used when real implementations are required.
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

FAIL=0

# Pattern 1: #error "STUB_DRIVER" or #error "STUB" - stub files must not compile
if grep -rn '#error.*STUB' kernel/ arch/ drivers/ VM/ 2>/dev/null | grep -v '.md:'; then
    echo "ERROR: Stub sentinel found - driver file must not compile (remove or implement)" >&2
    FAIL=1
fi

# Pattern 2: Unimplemented driver markers - scan all driver roots (README + actual layout)
# Coverage: kernel/drivers/, arch drivers/HAL, VM devices, root drivers/ if present
for f in drivers/*.c kernel/drivers/*.c kernel/drivers/block/*.c kernel/arch/*/drivers/*.c kernel/arch/*/hal/*.c VM/devices/*.c; do
    [ -f "$f" ] || continue
    if grep -q 'PLACEHOLDER_STUB\|#error.*STUB_DRIVER' "$f" 2>/dev/null; then
        echo "ERROR: $f contains forbidden stub marker" >&2
        FAIL=1
    fi
done

# Exclude: caps.h defines FL_CAP_STUB, docs, comments with "stub" as explanation
# The above grep tries to exclude those; refine if needed.
if [ $FAIL -eq 1 ]; then
    exit 1
fi
echo "check_no_stubs: OK (no forbidden stub markers)"
exit 0
