#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# SECFEAT=1: the separate security-features image (netlab compiled in); the default is the lab-free image main ships.
secfeat="${SECFEAT:-0}"
if [ "${secfeat}" = "1" ]; then
    build="${root}/build/freestanding-x86_64-secfeat"; imgname="flintstone-secfeat.img"; infoname="build-info-secfeat.json"
else
    build="${root}/build/freestanding-x86_64"; imgname="flintstone.img"; infoname="build-info.json"
fi
dist="${root}/dist"
mkdir -p "${build}" "${dist}"
for tool in nasm gcc ld objcopy python3 sha256sum git; do command -v "$tool" >/dev/null || { echo "missing tool: $tool" >&2; exit 1; }; done
fsdir="${root}/kernel/freestanding/x86_64"
nasm -f bin -I"${fsdir}/" "${fsdir}/boot.asm" -o "${build}/boot.bin"
nasm -f bin -I"${fsdir}/" "${fsdir}/stage2.asm" -o "${build}/stage2.bin"
layout() { awk -v n="$1" '$1=="%define" && $2==n{print $3}' "${fsdir}/bootinfo.inc"; }
stage2_sectors=$(layout FL_STAGE2_SECTORS)
load_addr=$(layout FL_KERNEL_LOAD_ADDR)
low_budget=$(layout FL_LOW_BUDGET)
bss_addr=$(layout FL_KERNEL_BSS_ADDR)
map_end=$(layout FL_IDENTITY_MAP_END)
(( stage2_sectors > 0 && $(stat -c%s "${build}/stage2.bin") == stage2_sectors * 512 )) || { echo "stage 2 is not ${stage2_sectors} sectors" >&2; exit 1; }
nasm -f elf64 -I"${fsdir}/" "${fsdir}/entry.asm" -o "${build}/entry.o"
cflags=(-c -std=c11 -m64 -march=x86-64 -mno-red-zone -mgeneral-regs-only -ffreestanding -fno-stack-protector -fno-pic -fno-asynchronous-unwind-tables -Wall -Wextra -Werror -DFL_FREESTANDING -DNDEBUG -I"${root}/kernel/drivers/freestanding_x86_64" -I"${root}/kernel/freestanding/x86_64")
for d in contracts/foundations contracts/runtime contracts/networking contracts/identity contracts/drivers contracts/storage contracts/observability contracts/operations contracts/virtualization contracts/hardening kernel/include kernel/core/sched kernel/core/time arch/x86_64/gas; do
    cflags+=(-I"${root}/${d}")
done
cflags+=(-I"${root}")
# KQ_FREESTANDING=0 skips the KeyQuorum library; cargo reads .cargo/config.toml from the working directory's parents (hence the cd).
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
[ "${kq_on}" = "1" ] && kq_srcs=("${root}/kernel/freestanding/x86_64/kq_platform.c" "${root}/kernel/freestanding/x86_64/kq_heap.c" "${root}/kernel/drivers/keyquorum/kq_driver.c")
# Not in the SECFEAT image: its bss has no room.
[ "${kq_on}" = "1" ] && [ "${secfeat}" != "1" ] && kq_srcs+=("${root}/kernel/freestanding/x86_64/kq_identity.c" "${root}/kernel/drivers/freestanding_x86_64/fwcfg.c")
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
    "${root}/kernel/freestanding/x86_64/bootmap.c" \
    "${root}/kernel/core/sched/priority_queue.c" \
    "${root}/kernel/core/sched/workqueue.c" \
    "${root}/kernel/core/sched/bg_jobs.c" \
    ${kq_srcs[@]+"${kq_srcs[@]}"} \
    "${root}/kernel/freestanding/x86_64/kernel.c"
do
    obj="${build}/$(basename "${src}" .c).o"
    extra=()
    # kq_platform.c defines memcpy and friends: nothing in it may turn into a call to itself.
    case "${src}" in
        */kq_platform.c) extra=(-fno-builtin -fno-tree-loop-distribute-patterns) ;;
        */bootmap.c) extra=(-Os) ;; # runs once per boot: small beats fast while the code lives below 0x9f000
    esac
    gcc "${cflags[@]}" ${extra[@]+"${extra[@]}"} "${src}" -o "${obj}"
    objects+=("${obj}")
done
gcc -c -m64 "${root}/arch/x86_64/gas/mem_asm.s" -o "${build}/mem_asm.o"
objects+=("${build}/mem_asm.o")
ld_extra=()
[ "${kq_on}" = "1" ] && ld_extra=(--gc-sections "${kqlib}")
ld -m elf_x86_64 -nostdlib -T "${root}/kernel/freestanding/x86_64/linker.ld" "${objects[@]}" ${ld_extra[@]+"${ld_extra[@]}"} -o "${build}/kernel.elf"
objcopy -O binary "${build}/kernel.elf" "${build}/kernel.bin"
sectors=$(( ($(stat -c%s "${build}/kernel.bin") + 511) / 512 ))
(( sectors > 0 && sectors <= 1024 )) || { echo "payload is outside the chunked BIOS load budget (max 1024 sectors / 512 KiB): ${sectors} sectors" >&2; exit 1; }
# The loaded image stays below FL_LOW_BUDGET (0x9f000, under the EBDA at 0x9fc00) and VGA at 0xa0000;
# the bss is linked at FL_KERNEL_BSS_ADDR above 1 MiB and is never read from disk.
image_end=$(nm "${build}/kernel.elf" | awk '$3=="__image_end"{print "0x"$1}')
bss_end=$(nm "${build}/kernel.elf" | awk '$3=="__bss_end"{print "0x"$1}')
payload_bytes=$(stat -c%s "${build}/kernel.bin")
nm "${build}/kernel.elf" | python3 -c '
import sys
payload, sectors = int(sys.argv[1]), int(sys.argv[2])
base_expected, budget = int(sys.argv[3], 0), int(sys.argv[4], 0)
bss_expected, map_end = int(sys.argv[5], 0), int(sys.argv[6], 0)
sym = {}
for line in sys.stdin:
    f = line.split()
    if len(f) == 3:
        sym[f[2]] = int(f[0], 16)
need = ("__kernel_start", "kernel_entry", "__image_end", "__bss_start", "__bss_end")
missing = [n for n in need if n not in sym]
if missing:
    sys.exit("image layout: missing symbols: " + ", ".join(missing))
start, entry, image_end, bss_start, bss_end = (sym[n] for n in need)
problems = []
if start != base_expected or entry != start:
    problems.append("entry %#x / image start %#x is not the load address %#x" % (entry, start, base_expected))
if start + payload != image_end:
    problems.append("the payload ends at %#x, not at the image end %#x" % (start + payload, image_end))
if start + sectors * 512 > budget:
    problems.append("the loader reads up to %#x, past the budget %#x" % (start + sectors * 512, budget))
if bss_start != bss_expected:
    problems.append("the bss starts at %#x, not FL_KERNEL_BSS_ADDR %#x" % (bss_start, bss_expected))
if not bss_start < bss_end <= map_end:
    problems.append("the bss [%#x, %#x) is empty or past the identity map (%#x)" % (bss_start, bss_end, map_end))
if problems:
    sys.exit("image layout: " + "; ".join(problems))
' "${payload_bytes}" "${sectors}" "${load_addr}" "${low_budget}" "${bss_addr}" "${map_end}"
python3 - "${build}/stage2.bin" "$sectors" "$bss_end" <<'PY'
import pathlib, struct, sys
p = pathlib.Path(sys.argv[1]); data = bytearray(p.read_bytes())
tag = b"FLS2PTCH"
off = data.find(tag)
assert off >= 0 and data.find(tag, off + 1) < 0, "stage 2 patch tag missing or not unique"
off += len(tag)
assert data[off:off + 8] == bytes(8), "stage 2 patch block already written"
data[off:off + 8] = struct.pack("<II", int(sys.argv[2]), int(sys.argv[3], 16))
p.write_bytes(data)
PY
image="${dist}/${imgname}"; cat "${build}/boot.bin" "${build}/stage2.bin" "${build}/kernel.bin" >"${image}"
truncate -s $(( (1 + stage2_sectors + sectors) * 512 )) "${image}"
commit="${GITHUB_SHA:-$(git -C "${root}" rev-parse HEAD)}"; short="${commit:0:7}"
sha="$(sha256sum "${image}" | awk '{print $1}')"; built="${BUILD_TIMESTAMP:-$(date -u +'%Y-%m-%dT%H:%M:%SZ')}"
python3 - "${dist}/${infoname}" "$commit" "$short" "$built" "$sha" "$imgname" "$secfeat" "$sectors" "$payload_bytes" "$image_end" "$bss_addr" "$bss_end" "$stage2_sectors" "$kq_on" <<'PY'
import json, sys
out, commit, short, built, sha, artifact, secfeat, sectors, payload_bytes, image_end, bss_start, bss_end, stage2_sectors, kq_on = sys.argv[1:]
kq = kq_on == "1" # the 40 MiB KeyQuorum arena is what 32 MiB of RAM cannot hold
info={"schemaVersion":2,"project":"Flintstone Kernel","repository":"BPForbes/Bailey-Forbes-Flinstone","commit":commit,"shortCommit":short,"builtAt":built,"architecture":"x86_64","cpuMode":"64-bit long mode, freestanding","artifact":artifact,"artifactFormat":"raw BIOS disk image","securityFeatures":secfeat=="1","sha256":sha,"browserEmulator":"QEMU Wasm x86_64 (b7c549b5e6f4)","browserCompatible":False,"v86Compatible":False,"bootableCandidate":True,"bootable":False,"bootloader":"BIOS MBR + stage 2 long-mode loader (E820, A20, boot info v1)","requiredBios":"SeaBIOS-compatible","qemuBootMode":"ide-drive","minimumRamBytes":64*1024*1024,"lowMemoryProbe":{"ramBytes":32*1024*1024,"marker":"FLINTSTONE_BOOT_FAIL memory"} if kq else None,"recommendedRamBytes":64*1024*1024,"bootSuccessMarker":"FLINTSTONE_KERNEL_BOOT_OK","bootSuccessMarkerImplemented":True,"validationOutcome":"qemu-unvalidated","runtimeMode":"browser-hosted","serverPath":"relay","serverRelayPort":8767,"stage2Sectors":int(stage2_sectors),"payloadSectors":int(sectors),"payloadBytes":int(payload_bytes),"imageEnd":image_end,"bssStart":bss_start,"bssEnd":bss_end,"conventionalMemoryBudget":"0x9f000","capabilities":{"longMode":True,"gdt":True,"idt":True,"serial":True,"vga":True,"pic":True,"pit":True,"ps2Probe":True,"keyboard":True,"biosBlockLoad":True,"identity":True,"hostedLabSessions":True,"filesystem":True,"network":True,"server":True,"securityLab":secfeat=="1"},"blockers":["QEMU has not independently observed the serial marker","Browser has not independently observed the serial marker for this image"]}
if info["lowMemoryProbe"] is None: del info["lowMemoryProbe"]
with open(out,'w',encoding='utf-8') as f: json.dump(info,f,indent=2); f.write('\n')
PY
echo "browser-kernel: wrote ${image} (${stage2_sectors} stage 2 + ${sectors} payload sectors / ${payload_bytes} bytes, image_end=${image_end} < ${low_budget}, bss=[${bss_addr}, ${bss_end}), SECFEAT=${secfeat})"