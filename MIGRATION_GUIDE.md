# Migration Guide

This document outlines the reorganization of the codebase to match the new structure.

## Completed

✅ Directory structure created  
✅ CMake build system configured  
✅ Architecture-specific assembly files moved under `arch/` and `kernel/arch/`  
✅ Legacy root trees removed: `ARM/`, `alloc/`, `x86-64 (NASM)/`  
✅ Canonical assembly paths documented in `docs/ARCH.md`  
✅ Userland shell sources live under `userland/shell/` (formerly scattered `main.c`, `interpreter.c`, etc.)  
✅ Kernel core modules under `kernel/core/{vfs,mm,sched,identity,memory,time,sys}/`  
✅ VM sources under `vm/devices/` (formerly `VM/`)  
✅ Include paths use `fl/*` and contract headers (see `docs/ARCH.md`)

## File Mapping

### Kernel Core (Architecture-Independent)

| Old Location | New Location | Status |
|-------------|-------------|--------|
| `priority_queue.c/h` | `kernel/core/sched/priority_queue.c` | ✅ Moved |
| `vfs.c/h` | `kernel/core/vfs/vfs.c` | ✅ Present |
| `mem_domain.c/h` | `kernel/core/mm/kmalloc.c` | ✅ Present (mem_domain API in `mem_domain.c`) |
| `path_log.c/h` | `kernel/core/vfs/path.c` | ✅ Present (`path_log` in `kernel/core/vfs/path_log.c`) |
| `threadpool.c/h` | `kernel/core/sched/scheduler.c` | ✅ Hosted pool in `userland/shell/threadpool.c`; kernel sched in `kernel/core/sched/` |
| `task_manager.c/h` | `kernel/core/sched/task.c` | ✅ Present |
| `fs_*.c/h` | `kernel/core/vfs/` | ✅ Present |
| `cluster.c/h` | `kernel/core/vfs/` | ✅ Present |
| `disk.c/h` | `kernel/core/vfs/` | ✅ Present (`disk.c`, `disk_host.c`, etc.) |

### Kernel Architecture-Specific (x86_64)

| Old Location | New Location | Status |
|-------------|-------------|--------|
| `arch/x86_64/nasm/mem_asm.asm` | `kernel/arch/x86_64/asm/memcpy.asm` | ✅ Canonical under `arch/`; kernel copy |
| `arch/x86_64/nasm/alloc_*.asm` | `kernel/arch/x86_64/asm/` | ✅ Synced from `arch/x86_64/nasm/` |
| `arch/x86_64/nasm/port_io.asm` | `kernel/arch/x86_64/drivers/port_io.asm` | ✅ See `arch/x86_64/nasm/` |
| `drivers/*.c/h` | `kernel/arch/x86_64/drivers/` | ✅ Present |
| (legacy `x86-64 (NASM)/`) | — | 🗑️ Removed; use `arch/x86_64/nasm/` |

### Kernel Architecture-Specific (aarch64)

| Old Location | New Location | Status |
|-------------|-------------|--------|
| `arch/arm/gas/mem_asm.s` | `kernel/arch/aarch64/asm/memcpy.S` | ✅ Canonical under `arch/arm/gas/`; kernel copy |
| `arch/arm/gas/alloc_*.s` | `kernel/arch/aarch64/asm/` | ✅ Synced from `arch/arm/gas/` |
| `arch/arm/gas/port_io.s` | `kernel/arch/aarch64/drivers/port_io.S` | ✅ Stubs in `arch/arm/gas/` |
| (legacy `ARM/`) | — | 🗑️ Removed; use `arch/arm/gas/` (**`ARCH=arm` = AArch64**, not 32-bit ARM) |

### Userland

| Old Location | New Location | Status |
|-------------|-------------|--------|
| `main.c` | `userland/shell/sh.c` | ✅ Moved |
| `interpreter.c/h` | `userland/shell/interpreter.c` | ✅ Present |
| `terminal.c/h` | `userland/shell/terminal.c` | ✅ Present |
| `common.c/h` | `userland/shell/common.c` | ✅ Present |
| `util.c/h` | `userland/shell/util.c` | ✅ Present |
| `fs.c/h` | `userland/coreutils/` + vfs glue | ✅ Split across `userland/shell/` and `kernel/core/vfs/` |

### VM

| Old Location | New Location | Status |
|-------------|-------------|--------|
| `VM/*.c/h` | `vm/devices/` | ✅ Present |

### Tests

| Old Location | New Location | Status |
|-------------|-------------|--------|
| `tests/*.c` | `tests/` | ✅ Already in place |

## Include Path Updates

All includes should use the new structure:

```c
// Old
#include "priority_queue.h"
#include "mem_asm.h"

// New
#include "fl/sched.h"
#include "fl/arch.h"
```

## Architecture Hooks

Code that uses architecture-specific functions should use the arch API:

```c
// Old
asm_mem_copy(dst, src, n);

// New
arch_memcpy(dst, src, n);
```

The architecture-specific implementation provides the optimized version. Remaining `asm_mem_copy` references are confined to assembly sources and migration notes.

## Next Steps

1. Keep `arch/arm/gas/` and `ARCH=arm` naming documented (AArch64 GAS tree; see `docs/ARCH.md`).
2. Prefer `arch_memcpy` / `fl_stack` APIs in new C code.
3. Test build with `make` and `make ARCH=arm` after assembly or include changes.
