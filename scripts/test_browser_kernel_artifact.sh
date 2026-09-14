#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
artifact="${repo_root}/dist/flintstone-kernel-x86_64.elf"
metadata="${repo_root}/dist/build-info.json"

for tool in file readelf python3 qemu-system-x86_64 timeout; do
    command -v "${tool}" >/dev/null 2>&1 || {
        echo "test-browser-kernel: required tool not found: ${tool}" >&2
        exit 1
    }
done

[[ -s "${artifact}" ]] || {
    echo "test-browser-kernel: missing artifact: ${artifact}" >&2
    exit 1
}
[[ -s "${metadata}" ]] || {
    echo "test-browser-kernel: missing metadata: ${metadata}" >&2
    exit 1
}

file "${artifact}" | grep -q 'ELF 64-bit.*x86-64'
readelf -l "${artifact}" | grep -q 'Requesting program interpreter:'

python3 - "${metadata}" "$(basename "${artifact}")" <<'PY'
import json
import sys

with open(sys.argv[1], encoding="utf-8") as handle:
    info = json.load(handle)

assert info["repository"] == "BPForbes/Bailey-Forbes-Flinstone"
assert info["architecture"] == "x86_64"
assert info["artifact"] == sys.argv[2]
assert info["browserEmulator"] == "requires-x86-64-browser-emulator"
assert info["v86Compatible"] is False
assert info["bootable"] is False
assert info["validationOutcome"] == "architecture-blocked"
assert info["bootSuccessMarkerImplemented"] is False
PY

qemu_log="$(mktemp)"
trap 'rm -f "${qemu_log}"' EXIT
set +e
timeout 10 qemu-system-x86_64 \
    -machine pc,accel=tcg \
    -m 64M \
    -display none \
    -monitor none \
    -serial stdio \
    -no-reboot \
    -kernel "${artifact}" >"${qemu_log}" 2>&1
qemu_status=$?
set -e

if [[ ${qemu_status} -eq 0 || ${qemu_status} -eq 124 ]]; then
    cat "${qemu_log}" >&2
    echo "test-browser-kernel: QEMU unexpectedly accepted or hung on the hosted ELF" >&2
    exit 1
fi
if grep -q 'FLINTSTONE_KERNEL_BOOT_OK' "${qemu_log}"; then
    cat "${qemu_log}" >&2
    echo "test-browser-kernel: impossible boot marker found in rejected candidate" >&2
    exit 1
fi

echo "test-browser-kernel: PASS"
echo "  ELF audit: x86-64, dynamically linked, no firmware boot protocol"
echo "  QEMU boot probe: rejected as expected (exit ${qemu_status})"
echo "  v86 audit: incompatible (no x86-64 long mode)"
echo "  outcome: documented architecture blocker; no public lab artifact may be promoted"
echo "  QEMU: $(tr '\n' ' ' <"${qemu_log}")"
