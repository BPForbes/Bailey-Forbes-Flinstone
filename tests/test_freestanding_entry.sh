#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_TIMESTAMP=1970-01-01T00:00:00Z "${root}/scripts/build_freestanding_browser_kernel.sh" >/dev/null
python3 - "${root}" <<'PY'
import json, pathlib, sys
root=pathlib.Path(sys.argv[1])
boot=(root/'build/freestanding-x86_64/boot.bin').read_bytes()
kernel=(root/'build/freestanding-x86_64/kernel.bin').read_bytes()
image=(root/'dist/flintstone.img').read_bytes()
info=json.loads((root/'dist/build-info.json').read_text())
assert len(boot)==512 and boot[510:]==b'\x55\xaa'
assert boot[:2]==b'\xfa\xfc', 'boot entry must execute cli; cld'
assert kernel[:2]==b'\xfa\xfc', '64-bit entry must execute cli; cld'
assert image[:512]==boot and len(image)%512==0
# Stage 2 follows the MBR, ends with its magic, and the kernel payload follows stage 2.
stage2=(root/'build/freestanding-x86_64/stage2.bin').read_bytes()
assert len(stage2)%512==0 and len(stage2)//512==info['stage2Sectors'] and stage2[-4:]==b'FLS2'
assert image[512:512+len(stage2)]==stage2 and image[512+len(stage2):512+len(stage2)+len(kernel)]==kernel
assert len(image)==512+len(stage2)+info['payloadSectors']*512
assert info['schemaVersion']==2 and info['bootableCandidate'] is True
# A build is never allowed to self-certify an independent boot observation.
assert info['bootable'] is False and info['validationOutcome']=='qemu-unvalidated'
assert info['capabilities']['network'] is True
for name in ('longMode','gdt','idt','serial','vga','pic','pit','ps2Probe','keyboard','biosBlockLoad','identity','hostedLabSessions','filesystem','server'):
    assert info['capabilities'][name] is True, name
print('test_freestanding_entry: PASS')
PY
