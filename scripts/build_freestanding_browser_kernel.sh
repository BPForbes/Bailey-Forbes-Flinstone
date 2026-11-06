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
cflags=(-c -std=c11 -m64 -march=x86-64 -mno-red-zone -mgeneral-regs-only -ffreestanding -fno-stack-protector -fno-pic -fno-asynchronous-unwind-tables -Wall -Wextra -Werror -DFL_FREESTANDING -DNDEBUG -I"${root}/kernel/drivers/freestanding_x86_64" -I"${root}/kernel/freestanding/x86_64")
# The shared workqueue (kernel/core/sched) is compiled in unchanged; -DFL_FREESTANDING selects its
# unlocked, static-pool configuration. Same include roots as the hosted build for its contracts.
for d in contracts/foundations contracts/runtime contracts/networking contracts/identity contracts/drivers contracts/storage contracts/observability contracts/operations contracts/virtualization contracts/hardening kernel/include kernel/core/sched kernel/core/time arch/x86_64/gas; do
    cflags+=(-I"${root}/${d}")
done
cflags+=(-I"${root}")
# KeyQuorum (FL-02): the Rust library for x86_64-unknown-none, built from the submodule with the portable
# crypto backends its .cargo/config.toml selects (cargo reads that file from the working directory's
# parents, hence the cd). KQ_FREESTANDING=0 builds the image without it, as before.
kq_on="${KQ_FREESTANDING:-1}"
kq_objects=()
if [ "${kq_on}" = "1" ]; then
    kq="${KQ_DIR:-${root}/third_party/keyquorum/ffi/identity}"
    [ -f "${kq}/Cargo.toml" ] || { echo "missing KeyQuorum submodule (git submodule update --init third_party/keyquorum), or set KQ_FREESTANDING=0" >&2; exit 1; }
    command -v cargo >/dev/null || { echo "cargo is needed to link KeyQuorum into the image (or set KQ_FREESTANDING=0)" >&2; exit 1; }
    (cd "${kq}" && cargo build --locked --release --no-default-features --target x86_64-unknown-none >&2)
    kqlib="${kq}/target/x86_64-unknown-none/release/libkeyquorum_identity.a"
    cflags+=(-I"${kq}/include" -I"${root}/kernel/core/mm" -DFL_WITH_KEYQUORUM)
fi
[ "${secfeat}" = "1" ] && cflags+=(-DFL_SECFEAT)
objects=("${build}/entry.o")
kq_srcs=()
[ "${kq_on}" = "1" ] && kq_srcs=("${root}/kernel/freestanding/x86_64/kq_platform.c" "${root}/kernel/drivers/keyquorum/kq_driver.c")
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
    "${root}/kernel/freestanding/x86_64/bgtick.c" \
    "${root}/kernel/core/sched/priority_queue.c" \
    "${root}/kernel/core/sched/workqueue.c" \
    "${root}/kernel/core/sched/bg_jobs.c" \
    ${kq_srcs[@]+"${kq_srcs[@]}"} \
    "${root}/kernel/freestanding/x86_64/kernel.c"
do
    obj="${build}/$(basename "${src}" .c).o"
    extra=()
    # kq_platform.c defines memcpy and friends: nothing in it may turn into a call to itself.
    case "${src}" in */kq_platform.c) extra=(-fno-builtin -fno-tree-loop-distribute-patterns) ;; esac
    gcc "${cflags[@]}" ${extra[@]+"${extra[@]}"} "${src}" -o "${obj}"
    objects+=("${obj}")
done
# asm_mem_zero / asm_mem_copy for the queue: the same GAS source the hosted build links (no libc, no SSE).
gcc -c -m64 "${root}/arch/x86_64/gas/mem_asm.s" -o "${build}/mem_asm.o"
objects+=("${build}/mem_asm.o")
ld_extra=()
[ "${kq_on}" = "1" ] && ld_extra=(--gc-sections "${kqlib}")
ld -m elf_x86_64 -nostdlib -T "${root}/kernel/freestanding/x86_64/linker.ld" "${objects[@]}" ${ld_extra[@]+"${ld_extra[@]}"} -o "${build}/kernel.elf"
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
# The payload is followed by bss (the stack, the queues, the KeyQuorum library's statics) in the same
# conventional memory: its end must stay below 0x9f000 (the EBDA starts at 0x9fc00, VGA at 0xa0000).
bss_end=$(nm "${build}/kernel.elf" | awk '$3=="__bss_end"{print "0x"$1}')
(( bss_end > 0 && bss_end < 0x9f000 )) || { echo "image plus bss ends at ${bss_end}: past the conventional-memory budget (0x9f000)" >&2; exit 1; }
# FL-22 acceptance evidence: payload size and bss end (already gated above) are recorded for CI logs.
payload_bytes=$(stat -c%s "${build}/kernel.bin")
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
python3 - "${dist}/${infoname}" "$commit" "$short" "$built" "$sha" "$imgname" "$secfeat" "$sectors" "$payload_bytes" "$bss_end" <<'PY'
import json, sys
out, commit, short, built, sha, artifact, secfeat, sectors, payload_bytes, bss_end = sys.argv[1:]
info={"schemaVersion":2,"project":"Flintstone Kernel","repository":"BPForbes/Bailey-Forbes-Flinstone","commit":commit,"shortCommit":short,"builtAt":built,"architecture":"x86_64","cpuMode":"64-bit long mode, freestanding","artifact":artifact,"artifactFormat":"raw BIOS disk image","securityFeatures":secfeat=="1","sha256":sha,"browserEmulator":"QEMU Wasm x86_64 (b7c549b5e6f4)","browserCompatible":False,"v86Compatible":False,"bootableCandidate":True,"bootable":False,"bootloader":"BIOS MBR long-mode loader","requiredBios":"SeaBIOS-compatible","qemuBootMode":"ide-drive","minimumRamBytes":64*1024*1024,"recommendedRamBytes":64*1024*1024,"bootSuccessMarker":"FLINTSTONE_KERNEL_BOOT_OK","bootSuccessMarkerImplemented":True,"validationOutcome":"qemu-unvalidated","runtimeMode":"browser-hosted","serverPath":"relay","serverRelayPort":8767,"payloadSectors":int(sectors),"payloadBytes":int(payload_bytes),"bssEnd":bss_end,"conventionalMemoryBudget":"0x9f000","capabilities":{"longMode":True,"gdt":True,"idt":True,"serial":True,"vga":True,"pic":True,"pit":True,"ps2Probe":True,"keyboard":True,"biosBlockLoad":True,"identity":True,"hostedLabSessions":True,"filesystem":True,"network":True,"server":True,"securityLab":secfeat=="1"},"blockers":["QEMU has not independently observed the serial marker","Browser has not independently observed the serial marker for this image"]}
with open(out,'w',encoding='utf-8') as f: json.dump(info,f,indent=2); f.write('\n')
PY
echo "browser-kernel: wrote ${image} (${sectors} payload sectors / ${payload_bytes} bytes, bss_end=${bss_end} < 0x9f000, SECFEAT=${secfeat})"