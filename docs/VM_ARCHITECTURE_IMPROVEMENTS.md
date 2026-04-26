# VM 5-Layer Driver System + 11-Point Architecture Improvements

This project now wires VM userspace communication through a syscall bridge and
tracks VM readiness with a VM-specific adaptation of:

- **Add layered driver model slice (#21)** (5 layers)
- **Improve system architecture for bare metal (#24)** (11 checkpoints)

## 5-Layer Driver System (VM)

1. **Layer 0 — Core host state**: `vm_host` memory/cpu created.
2. **Layer 1 — Driver substrate**: block/keyboard/display/timer/pic drivers available.
3. **Layer 2 — Device boundary**: virtual disk and VM port boundary active.
4. **Layer 3 — Service glue**: VM I/O + scheduler services compiled and active.
5. **Layer 4 — VM runtime**: VM run loop integration is available.

## 11-Point Architecture Improvements (VM adaptation of #24)

1. Core host/memory state initialized.
2. Block path present.
3. Keyboard path present.
4. Display path present.
5. Timer path present.
6. IRQ/PIC path present.
7. VM disk boundary active.
8. PCI virtual configuration active.
9. Reset line initialized.
10. Syscall bridge + serial path active.
11. Deterministic boot entry contract enforced (`CS=0x07C0`, `IP=0`).

## VM syscall bridge ports

- `0xE0`: syscall number
- `0xE1..0xE4`: arguments `a0..a3`
- `0xE5`: invoke syscall
- `0xE6`: return value (32-bit)

The VM now dispatches these calls to `fl_syscall_dispatch`, enabling guest-side
IPC and userspace connectivity through the kernel syscall bridge.
