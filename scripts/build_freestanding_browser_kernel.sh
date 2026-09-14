#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build="${root}/build/freestanding-x86_64"; dist="${root}/dist"
mkdir -p "${build}" "${dist}"
for tool in nasm gcc ld objcopy python3 sha256sum git; do command -v "$tool" >/dev/null || { echo "missing tool: $tool" >&2; exit 1; }; done
nasm -f bin "${root}/kernel/freestanding/x86_64/boot.asm" -o "${build}/boot.bin"
nasm -f elf64 "${root}/kernel/freestanding/x86_64/entry.asm" -o "${build}/entry.o"
gcc -c -std=c11 -m64 -march=x86-64 -mno-red-zone -mgeneral-regs-only -ffreestanding -fno-stack-protector -fno-pic -Wall -Wextra -Werror "${root}/kernel/freestanding/x86_64/kernel.c" -o "${build}/kernel.o"
ld -m elf_x86_64 -nostdlib -T "${root}/kernel/freestanding/x86_64/linker.ld" "${build}/entry.o" "${build}/kernel.o" -o "${build}/kernel.elf"
objcopy -O binary "${build}/kernel.elf" "${build}/kernel.bin"
sectors=$(( ($(stat -c%s "${build}/kernel.bin") + 511) / 512 ))
(( sectors > 0 && sectors <= 127 )) || { echo "payload is outside one bounded BIOS transfer: ${sectors} sectors" >&2; exit 1; }
python3 - "${build}/boot.bin" "$sectors" <<'PY'
import pathlib, struct, sys
p = pathlib.Path(sys.argv[1]); data = bytearray(p.read_bytes())
needle = bytes((0x10, 0, 0, 0, 0, 0, 0, 0x10))
off = data.index(needle) + 2
data[off:off+2] = struct.pack('<H', int(sys.argv[2]))
p.write_bytes(data)
PY
image="${dist}/flintstone.img"; cat "${build}/boot.bin" "${build}/kernel.bin" >"${image}"
truncate -s $(( (sectors + 1) * 512 )) "${image}"
commit="${GITHUB_SHA:-$(git -C "${root}" rev-parse HEAD)}"; short="${commit:0:7}"
sha="$(sha256sum "${image}" | awk '{print $1}')"; built="${BUILD_TIMESTAMP:-$(date -u +'%Y-%m-%dT%H:%M:%SZ')}"
python3 - "${dist}/build-info.json" "$commit" "$short" "$built" "$sha" <<'PY'
import json, sys
out, commit, short, built, sha = sys.argv[1:]
info={"schemaVersion":2,"project":"Flintstone Kernel","repository":"BPForbes/Bailey-Forbes-Flinstone","commit":commit,"shortCommit":short,"builtAt":built,"architecture":"x86_64","cpuMode":"64-bit long mode, freestanding","artifact":"flintstone.img","artifactFormat":"raw BIOS disk image","sha256":sha,"browserEmulator":"QEMU Wasm x86_64 (b7c549b5e6f4)","browserCompatible":False,"v86Compatible":False,"bootableCandidate":True,"bootable":False,"bootloader":"BIOS MBR long-mode loader","requiredBios":"SeaBIOS-compatible","qemuBootMode":"ide-drive","minimumRamBytes":64*1024*1024,"recommendedRamBytes":64*1024*1024,"bootSuccessMarker":"FLINTSTONE_KERNEL_BOOT_OK","bootSuccessMarkerImplemented":True,"validationOutcome":"qemu-unvalidated","capabilities":{"longMode":True,"gdt":True,"idt":True,"serial":True,"vga":True,"pic":True,"pit":True,"ps2Probe":True,"biosBlockLoad":True,"identity":False,"filesystem":False,"network":False,"server":False,"hostedLabSessions":False},"blockers":["QEMU has not independently observed the serial marker","Browser has not independently observed the serial marker for this image"]}
with open(out,'w',encoding='utf-8') as f: json.dump(info,f,indent=2); f.write('\n')
PY
echo "browser-kernel: wrote ${image} (${sectors} payload sectors)"
