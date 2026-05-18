# QEMU lab bring-up (Phase 8 / P8-3)

This document satisfies the **Phase 8** table acceptance for **documented QEMU recipes** used
with the in-repo shell and VM work. Caps and enums live in **`contracts/virtualization/contract_p8_qemu_lab.h`**
(**`FL_CONTRACT_P8_QEMU_MACHINE_NAME_MAX_CHARS`**, **`FL_CONTRACT_P8_QEMU_FIXTURE_PATH_MAX_CHARS`**,
**`fl_contract_p8_qemu_accel_class_t`**).

## Machine profiles (`-M`)

Typical **x86_64** lab machines:

- **`pc-q35-8.2`** (or another pinned **`pc-q35-*`**) — PCIe + modern-ish chipset; good default when
  exercising virtio under a Q35 root complex.
- **`microvm`** — minimal footprint when you only need virtio-mmio devices and no legacy PC junk.

**AArch64** labs often use **`virt`** with a generated DTB. Keep the **`-M` token** under the
contract cap (63 characters including terminator budget in headers; stay descriptive but short).

## Acceleration (`-accel`)

- **`tcg`** — portable; pair with **`-icount`** when you need more deterministic instruction timing
  for replay or flake reduction (see **P8-1** in **`docs/ROADMAP.md`**).
- **`kvm`** on Linux hosts with `/dev/kvm` — uses host TSC discipline; **do not** assume **`-icount`**
  semantics apply.
- **macOS HVF** — treat as **`FL_CONTRACT_P8_QEMU_ACCEL_HVF_DEFERRED`** at the contract layer until a
  maintainer promotes explicit CI coverage.

## Deterministic timing (TCG / P8-1)

Example pattern (adjust machine and paths):

```text
qemu-system-x86_64 -M microvm -accel tcg \
  -icount shift=auto,sleep=off \
  -m 512 -display none -serial stdio
```

**`-icount`** is optional and **TCG-centric**; document which jobs use it in CI or local scripts.

## Virtio + disk (`-drive`) / net (`-netdev`)

Block file (respect host path length; large paths belong in a wrapper script, not argv tables):

```text
-drive file=build/vm_disk.img,if=none,format=raw,id=d0 -device virtio-blk-device,drive=d0
```

User networking (quick smoke):

```text
-netdev user,id=n0 -device virtio-net-device,netdev=n0
```

## Inter-VM / TAP bridge (P8-2)

Two guests on one software bridge (illustrative; interface names vary by host):

```text
# Once per host: create bridge and TAPs (iproute2 example; run as root)
ip link add br0 type bridge
ip tuntap add dev tap0 mode tap && ip link set tap0 master br0
ip tuntap add dev tap1 mode tap && ip link set tap1 master br0
ip link set br0 up && ip link set tap0 up && ip link set tap1 up

# Guest A
qemu-system-x86_64 ... -netdev tap,id=n0,ifname=tap0,script=no,downscript=no \
  -device virtio-net-pci,netdev=n0

# Guest B
qemu-system-x86_64 ... -netdev tap,id=n0,ifname=tap1,script=no,downscript=no \
  -device virtio-net-pci,netdev=n0
```

TAP data path and **`netdev`** policy remain **P3**; this file only documents **how QEMU is wired**
for inter-guest L2 labs.

## DTB / flash fixtures

When using **`-dtb`** or pflash images, record **path**, **max size**, and (for reproducibility) a
**hash** in lab scripts or CI notes. Paths must respect **`FL_CONTRACT_P8_QEMU_FIXTURE_PATH_MAX_CHARS`**.

## See also

- **`docs/ROADMAP.md`** — Phase **8** rows **P8-1**–**P8-3**
- **`contracts/virtualization/README.txt`** — umbrella and shard index
- **`AGENTS.md`** — `make vm` / `make vm-sdl` for the in-process emulator (separate from QEMU)
