# Assembly audit — issue reports (no fixes)

Structured audits of hand-written assembly. Each architecture has its own index:

| Architecture | Build flag | Canonical path | Index |
|--------------|------------|----------------|-------|
| x86-64 NASM | `ARCH=x86_64_nasm` | `arch/x86_64/nasm/` | [nasm/README.md](nasm/README.md) |
| AArch64 GAS | `ARCH=arm` | `arch/arm/gas/` | [arm/README.md](arm/README.md) |

**Status (2026-05-22):** Allocator ABI fixes applied under `arch/`; legacy **`ARM/`** and **`x86-64 (NASM)/`** removed. Individual reports in `nasm/` and `arm/` note fixed vs open items.
