#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# SECFEAT=1 builds the security-features variant (the netlab teaching lab
# compiled in) to a separate image; default is the lab-free image that the
# Flinstone page embeds and that main ships.
secfeat="${SECFEAT:-0}"
if [ "${secfeat}" = "1" ]; then
    build="${root}/build/freestanding-x86_64-secfeat"; imgname="flintstone-secfeat.img"; infoname="build-info-secfeat.json"
else
    build="${root}/build/freestanding-x86_64"; imgname="flintstone.img"; infoname="build-info.json"
fi
dist="${root}/dist"
mkdir -p "${build}" "${dist}"
for tool in nasm gcc ld objcopy python3 sha256sum git; do command -v "$tool" >/dev/null || { echo "missing tool: $tool" >&2; exit 1; }; done
nasm -f bin "${root}/kernel/freestanding/x86_64/boot.asm" -o "${build}/boot.bin"
nasm -f elf64 "${root}/kernel/freestanding/x86_64/entry.asm" -o "${build}/entry.o"
cflags=(-c -std=c11 -m64 -march=x86-64 -mno-red-zone -mgeneral-regs-only -ffreestanding -fno-stack-protector -fno-pic -fno-asynchronous-unwind-tables -Wall -Wextra -Werror -I"${root}/kernel/drivers/freestanding_x86_64" -I"${root}/kernel/freestanding/x86_64")
[ "${secfeat}" = "1" ] && cflags+=(-DFL_SECFEAT)
objects=("${build}/entry.o")
for src in \
    "${root}/kernel/drivers/freestanding_x86_64/serial.c" \
    "${root}/kernel/drivers/freestanding_x86_64/vga.c" \
    "${root}/kernel/drivers/freestanding_x86_64/pic.c" \
    "${root}/kernel/drivers/freestanding_x86_64/pit.c" \
    "${root}/kernel/drivers/freestanding_x86_64/keyboard.c" \
    "${root}/kernel/freestanding/x86_64/identity.c" \
    "${root}/kernel/freestanding/x86_64/ramfs.c" \
    "${root}/kernel/freestanding/x86_64/labdisk.c" \
    "${root}/kernel/freestanding/x86_64/labnet.c" \
    "${root}/kernel/freestanding/x86_64/commands.c" \
    "${root}/kernel/freestanding/x86_64/shell.c" \
    "${root}/kernel/freestanding/x86_64/kernel.c"
do
    obj="${build}/$(basename "${src}" .c).o"
    gcc "${cflags[@]}" "${src}" -o "${obj}"
    objects+=("${obj}")
done
ld -m elf_x86_64 -nostdlib -T "${root}/kernel/freestanding/x86_64/linker.ld" "${objects[@]}" -o "${build}/kernel.elf"
objcopy -O binary "${build}/kernel.elf" "${build}/kernel.bin"
# Chunked BIOS load (see boot.asm): the MBR reads the payload in <=127-sector
# pieces, advancing the destination segment each time, so it is no longer a
# single-transfer loader. The ceiling here is not that mechanism's limit; it
# keeps the loaded payload's end address (0x10000 + sectors*512) safely below
# 0xa0000, the start of VGA memory and the universal top of real-mode
# conventional memory, with 128 sectors of margin to spare (1024 sectors =
# 512 KiB payload ends at 0x90000; the boundary itself is at 1152 sectors).
sectors=$(( ($(stat -c%s "${build}/kernel.bin") + 511) / 512 ))
(( sectors > 0 && sectors <= 1024 )) || { echo "payload is outside the chunked BIOS load budget (max 1024 sectors / 512 KiB): ${sectors} sectors" >&2; exit 1; }
python3 - "${build}/boot.bin" "$sectors" <<'PY'
import pathlib, struct, sys
p = pathlib.Path(sys.argv[1]); data = bytearray(p.read_bytes())
needle = bytes((0xef, 0xbe, 0, 0, 0x0d, 0xf0))
off = data.index(needle) + 2
data[off:off+2] = struct.pack('<H', int(sys.argv[2]))
p.write_bytes(data)
PY
image="${dist}/${imgname}"; cat "${build}/boot.bin" "${build}/kernel.bin" >"${image}"
truncate -s $(( (sectors + 1) * 512 )) "${image}"
commit="${GITHUB_SHA:-$(git -C "${root}" rev-parse HEAD)}"; short="${commit:0:7}"
sha="$(sha256sum "${image}" | awk '{print $1}')"; built="${BUILD_TIMESTAMP:-$(date -u +'%Y-%m-%dT%H:%M:%SZ')}"
python3 - "${dist}/${infoname}" "$commit" "$short" "$built" "$sha" "$imgname" "$secfeat" <<'PY'
import json, sys
out, commit, short, built, sha, artifact, secfeat = sys.argv[1:]
info={"schemaVersion":2,"project":"Flintstone Kernel","repository":"BPForbes/Bailey-Forbes-Flinstone","commit":commit,"shortCommit":short,"builtAt":built,"architecture":"x86_64","cpuMode":"64-bit long mode, freestanding","artifact":artifact,"artifactFormat":"raw BIOS disk image","securityFeatures":secfeat=="1","sha256":sha,"browserEmulator":"QEMU Wasm x86_64 (b7c549b5e6f4)","browserCompatible":False,"v86Compatible":False,"bootableCandidate":True,"bootable":False,"bootloader":"BIOS MBR long-mode loader","requiredBios":"SeaBIOS-compatible","qemuBootMode":"ide-drive","minimumRamBytes":64*1024*1024,"recommendedRamBytes":64*1024*1024,"bootSuccessMarker":"FLINTSTONE_KERNEL_BOOT_OK","bootSuccessMarkerImplemented":True,"validationOutcome":"qemu-unvalidated","runtimeMode":"browser-hosted","serverPath":"relay","serverRelayPort":8767,"capabilities":{"longMode":True,"gdt":True,"idt":True,"serial":True,"vga":True,"pic":True,"pit":True,"ps2Probe":True,"keyboard":True,"biosBlockLoad":True,"identity":True,"hostedLabSessions":True,"filesystem":True,"network":True,"server":True,"securityLab":secfeat=="1"},"blockers":["QEMU has not independently observed the serial marker","Browser has not independently observed the serial marker for this image"]}
with open(out,'w',encoding='utf-8') as f: json.dump(info,f,indent=2); f.write('\n')
PY
echo "browser-kernel: wrote ${image} (${sectors} payload sectors, SECFEAT=${secfeat})"
