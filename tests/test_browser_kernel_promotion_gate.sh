#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
fixture="$(mktemp -d)"
trap 'rm -rf "${fixture}"' EXIT

printf 'browser-kernel-gate-fixture\n' >"${fixture}/flintstone.img"
sha256="$(sha256sum "${fixture}/flintstone.img" | awk '{print $1}')"

write_manifest() {
    local v86_compatible="$1"
    python3 - "${fixture}/build-info.json" "${sha256}" "${v86_compatible}" <<'PY'
import json
import sys

output, sha256, v86_compatible = sys.argv[1:]
info = {
    "schemaVersion": 1,
    "project": "Flintstone Kernel",
    "repository": "BPForbes/Bailey-Forbes-Flinstone",
    "commit": "0" * 40,
    "shortCommit": "0000000",
    "builtAt": "2026-09-14T00:00:00Z",
    "architecture": "i386",
    "cpuMode": "32-bit protected mode",
    "artifact": "flintstone.img",
    "artifactFormat": "raw IDE disk image",
    "sha256": sha256,
    "browserEmulator": "v86",
    "v86Compatible": v86_compatible == "true",
    "bootable": True,
    "bootloader": "BIOS MBR",
    "requiredBios": "SeaBIOS-compatible",
    "qemuBootMode": "ide-drive",
    "minimumRamBytes": 16 * 1024 * 1024,
    "recommendedRamBytes": 64 * 1024 * 1024,
    "bootSuccessMarker": "FLINTSTONE_KERNEL_BOOT_OK",
    "bootSuccessMarkerImplemented": True,
    "validationOutcome": "browser-bootable",
    "blockers": [],
}
with open(output, "w", encoding="utf-8") as handle:
    json.dump(info, handle)
PY
}

cat >"${fixture}/qemu-marker" <<'SH'
#!/usr/bin/env bash
echo FLINTSTONE_KERNEL_BOOT_OK
SH
cat >"${fixture}/qemu-no-marker" <<'SH'
#!/usr/bin/env bash
echo boot-stalled
exit 1
SH
chmod +x "${fixture}/qemu-marker" "${fixture}/qemu-no-marker"

write_manifest true
GITHUB_OUTPUT="${fixture}/outputs" \
FL_BROWSER_KERNEL_DIST_DIR="${fixture}" \
FL_BROWSER_KERNEL_QEMU="${fixture}/qemu-marker" \
    "${repo_root}/scripts/test_browser_kernel_artifact.sh" >/dev/null
grep -q '^boot_smoke_passed=true$' "${fixture}/outputs"

: >"${fixture}/outputs"
if GITHUB_OUTPUT="${fixture}/outputs" \
   FL_BROWSER_KERNEL_DIST_DIR="${fixture}" \
   FL_BROWSER_KERNEL_QEMU="${fixture}/qemu-no-marker" \
       "${repo_root}/scripts/test_browser_kernel_artifact.sh" >/dev/null 2>&1; then
    echo "promotion gate accepted a boot without the serial marker" >&2
    exit 1
fi
if grep -q '^boot_smoke_passed=true$' "${fixture}/outputs"; then
    echo "promotion gate reported success without the serial marker" >&2
    exit 1
fi

write_manifest false
if FL_BROWSER_KERNEL_DIST_DIR="${fixture}" \
   FL_BROWSER_KERNEL_QEMU="${fixture}/qemu-marker" \
       "${repo_root}/scripts/test_browser_kernel_artifact.sh" >/dev/null 2>&1; then
    echo "promotion gate accepted a v86-incompatible candidate" >&2
    exit 1
fi

echo "test_browser_kernel_promotion_gate: PASS"
