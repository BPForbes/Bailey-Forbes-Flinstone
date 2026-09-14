#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
metadata="${repo_root}/dist/build-info.json"

for tool in file readelf python3 qemu-system-x86_64 sha256sum timeout; do
    command -v "${tool}" >/dev/null 2>&1 || {
        echo "test-browser-kernel: required tool not found: ${tool}" >&2
        exit 1
    }
done

[[ -s "${metadata}" ]] || {
    echo "test-browser-kernel: missing metadata: ${metadata}" >&2
    exit 1
}

if [[ -n "${GITHUB_OUTPUT:-}" ]]; then
    echo "boot_smoke_passed=false" >>"${GITHUB_OUTPUT}"
fi

mapfile -t manifest < <(python3 - "${metadata}" <<'PY'
import json
import re
import sys
from pathlib import PurePosixPath

with open(sys.argv[1], encoding="utf-8") as handle:
    info = json.load(handle)

required_strings = (
    "project", "repository", "commit", "shortCommit", "builtAt", "architecture",
    "cpuMode", "artifact", "artifactFormat", "sha256", "browserEmulator",
    "bootSuccessMarker", "validationOutcome",
)
required_bools = ("v86Compatible", "bootable", "bootSuccessMarkerImplemented")
nullable_strings = ("bootloader", "requiredBios", "qemuBootMode")
missing = [
    key for key in (
        "schemaVersion", *required_strings, *required_bools, *nullable_strings,
        "minimumRamBytes", "recommendedRamBytes", "blockers",
    )
    if key not in info
]
assert not missing, f"missing required metadata fields: {', '.join(missing)}"
assert type(info["schemaVersion"]) is int and info["schemaVersion"] == 1
for key in required_strings:
    assert isinstance(info[key], str) and info[key], f"{key} must be a non-empty string"
for key in required_bools:
    assert type(info[key]) is bool, f"{key} must be a JSON boolean"
for key in nullable_strings:
    assert info[key] is None or isinstance(info[key], str), f"{key} must be a string or null"
assert info["minimumRamBytes"] is None or (
    type(info["minimumRamBytes"]) is int and info["minimumRamBytes"] >= 0
)
assert type(info["recommendedRamBytes"]) is int and info["recommendedRamBytes"] > 0
assert isinstance(info["blockers"], list) and all(
    isinstance(item, str) and item for item in info["blockers"]
)
artifact = PurePosixPath(info["artifact"])
assert artifact.name == info["artifact"] and info["artifact"] not in (".", "..")
assert re.fullmatch(r"[0-9a-f]{64}", info["sha256"])
assert info["qemuBootMode"] in (None, "kernel", "ide-drive")
assert info["validationOutcome"] in ("browser-bootable", "architecture-blocked")
assert info["bootSuccessMarker"] == "FLINTSTONE_KERNEL_BOOT_OK"
if info["bootable"]:
    assert info["qemuBootMode"] is not None
    assert info["bootSuccessMarkerImplemented"] is True
    assert info["validationOutcome"] == "browser-bootable"
if info["validationOutcome"] == "architecture-blocked":
    assert info["blockers"], "blocked candidates must explain at least one blocker"

assert info["repository"] == "BPForbes/Bailey-Forbes-Flinstone"
print(info["artifact"])
print(str(info["bootable"]).lower())
print(str(info["v86Compatible"]).lower())
print(str(info["bootSuccessMarkerImplemented"]).lower())
print(info["qemuBootMode"] or "none")
print(info["sha256"])
PY
)

[[ ${#manifest[@]} -eq 6 ]] || {
    echo "test-browser-kernel: metadata validation did not return the contract fields" >&2
    exit 1
}

artifact="${repo_root}/dist/${manifest[0]}"
bootable="${manifest[1]}"
v86_compatible="${manifest[2]}"
marker_implemented="${manifest[3]}"
qemu_boot_mode="${manifest[4]}"
expected_sha256="${manifest[5]}"

[[ -s "${artifact}" ]] || {
    echo "test-browser-kernel: missing artifact: ${artifact}" >&2
    exit 1
}
actual_sha256="$(sha256sum "${artifact}" | awk '{print $1}')"
[[ "${actual_sha256}" == "${expected_sha256}" ]] || {
    echo "test-browser-kernel: artifact SHA-256 does not match build-info.json" >&2
    exit 1
}

qemu_log="$(mktemp)"
trap 'rm -f "${qemu_log}"' EXIT
qemu_args=(
    -machine pc,accel=tcg
    -m 64M
    -display none
    -monitor none
    -serial stdio
    -no-reboot
)

if [[ "${bootable}" == "true" ]]; then
    [[ "${v86_compatible}" == "true" && "${marker_implemented}" == "true" ]] || {
        echo "test-browser-kernel: bootable candidate is not eligible for v86 promotion" >&2
        exit 1
    }
    case "${qemu_boot_mode}" in
        kernel)
            qemu_args+=(-kernel "${artifact}")
            ;;
        ide-drive)
            qemu_args+=(-drive "file=${artifact},format=raw,if=ide" -boot c)
            ;;
        *)
            echo "test-browser-kernel: bootable candidate has no supported QEMU boot mode" >&2
            exit 1
            ;;
    esac
else
    file "${artifact}" | grep -q 'ELF 64-bit.*x86-64'
    readelf -l "${artifact}" | grep -q 'Requesting program interpreter:'
    qemu_args+=(-kernel "${artifact}")
fi

set +e
timeout 10 qemu-system-x86_64 "${qemu_args[@]}" >"${qemu_log}" 2>&1
qemu_status=$?
set -e

if [[ "${bootable}" == "true" ]]; then
    if ! grep -Fq 'FLINTSTONE_KERNEL_BOOT_OK' "${qemu_log}"; then
        cat "${qemu_log}" >&2
        echo "test-browser-kernel: QEMU did not observe the required serial boot marker" >&2
        exit 1
    fi
    if [[ -n "${GITHUB_OUTPUT:-}" ]]; then
        echo "boot_smoke_passed=true" >>"${GITHUB_OUTPUT}"
    fi
    echo "test-browser-kernel: PASS"
    echo "  QEMU boot probe: observed FLINTSTONE_KERNEL_BOOT_OK"
    echo "  promotion signals: bootable=true, v86Compatible=true, marker observed=true"
    exit 0
fi

if [[ ${qemu_status} -eq 0 ]] || grep -Fq 'FLINTSTONE_KERNEL_BOOT_OK' "${qemu_log}"; then
    cat "${qemu_log}" >&2
    echo "test-browser-kernel: blocked candidate unexpectedly booted" >&2
    exit 1
fi

echo "test-browser-kernel: PASS"
echo "  ELF audit: x86-64, dynamically linked, no firmware boot protocol"
if [[ ${qemu_status} -eq 124 ]]; then
    echo "  QEMU boot probe: no boot marker before bounded timeout"
else
    echo "  QEMU boot probe: loader rejected candidate (exit ${qemu_status})"
fi
echo "  v86 audit: incompatible (no x86-64 long mode)"
echo "  outcome: documented architecture blocker; no public lab artifact may be promoted"
echo "  QEMU: $(tr '\n' ' ' <"${qemu_log}")"
