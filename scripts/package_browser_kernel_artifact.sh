#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source_elf="${repo_root}/BPForbes_Flinstone_Shell"
dist_dir="${repo_root}/dist"
artifact_name="flintstone-kernel-x86_64.elf"
artifact_path="${dist_dir}/${artifact_name}"

for tool in git readelf sha256sum python3; do
    command -v "${tool}" >/dev/null 2>&1 || {
        echo "browser-kernel: required tool not found: ${tool}" >&2
        exit 1
    }
done

[[ -f "${source_elf}" ]] || {
    echo "browser-kernel: ${source_elf} is missing; run make baremetal first" >&2
    exit 1
}

elf_header="$(readelf -h "${source_elf}")"
elf_programs="$(readelf -l "${source_elf}")"
grep -q 'Class:[[:space:]]*ELF64' <<<"${elf_header}" || {
    echo "browser-kernel: expected the current x86 build to be ELF64" >&2
    exit 1
}
grep -q 'Machine:[[:space:]]*Advanced Micro Devices X86-64' <<<"${elf_header}" || {
    echo "browser-kernel: expected an AMD x86-64 ELF machine" >&2
    exit 1
}
grep -q 'Requesting program interpreter:' <<<"${elf_programs}" || {
    echo "browser-kernel: audit expectation changed: output is no longer dynamically linked" >&2
    echo "Re-audit the boot protocol before marking it browser bootable." >&2
    exit 1
}

rm -rf "${dist_dir}"
mkdir -p "${dist_dir}"
cp "${source_elf}" "${artifact_path}"

commit="${GITHUB_SHA:-$(git -C "${repo_root}" rev-parse HEAD)}"
short_commit="${commit:0:7}"
built_at="${BUILD_TIMESTAMP:-$(date -u +'%Y-%m-%dT%H:%M:%SZ')}"
sha256="$(sha256sum "${artifact_path}" | awk '{print $1}')"

python3 - "${dist_dir}/build-info.json" "${artifact_name}" "${commit}" \
    "${short_commit}" "${built_at}" "${sha256}" <<'PY'
import json
import sys

output, artifact, commit, short_commit, built_at, sha256 = sys.argv[1:]
metadata = {
    "project": "Flintstone Kernel",
    "repository": "BPForbes/Bailey-Forbes-Flinstone",
    "commit": commit,
    "shortCommit": short_commit,
    "builtAt": built_at,
    "architecture": "x86_64",
    "cpuMode": "64-bit long mode code in a hosted Linux process",
    "artifact": artifact,
    "artifactFormat": "ELF64 dynamically linked Linux executable",
    "sha256": sha256,
    "browserEmulator": "requires-x86-64-browser-emulator",
    "v86Compatible": False,
    "bootable": False,
    "bootloader": None,
    "requiredBios": None,
    "minimumRamBytes": None,
    "recommendedRamBytes": 64 * 1024 * 1024,
    "bootSuccessMarker": "FLINTSTONE_KERNEL_BOOT_OK",
    "bootSuccessMarkerImplemented": False,
    "validationOutcome": "architecture-blocked",
    "blockers": [
        "v86 does not implement x86-64 long mode",
        "the current target is a Linux-hosted ELF with a PT_INTERP dependency",
        "the repository has no firmware entry point, freestanding linker script, or bootloader handoff",
        "the in-process VM boots a synthetic 16/32-bit demonstration guest, not this ELF",
    ],
}
with open(output, "w", encoding="utf-8") as handle:
    json.dump(metadata, handle, indent=2)
    handle.write("\n")
PY

echo "browser-kernel: wrote ${artifact_path}"
echo "browser-kernel: wrote ${dist_dir}/build-info.json"
echo "browser-kernel: outcome B — artifact is audited but not bootable by v86"
