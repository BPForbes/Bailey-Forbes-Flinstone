# Migration Guide

This document outlines the reorganization of the codebase to match the new structure.

## Completed

✅ Directory structure created
✅ CMake build system configured
✅ Architecture-specific assembly files moved
✅ CMake toolchain files created
✅ Documentation created

## File Mapping

### Kernel Core (Architecture-Independent)

| Old Location | New Location | Status |
|-------------|-------------|--------|
| `priority_queue.c/h` | `kernel/core/sched/priority_queue.c` | ✅ Moved |
| `vfs.c/h` | `kernel/core/vfs/vfs.c` | ⚠️ Needs update |
| `mem_domain.c/h` | `kernel/core/mm/kmalloc.c` | ⚠️ Needs update |
| `path_log.c/h` | `kernel/core/vfs/path.c` | ⚠️ Needs update |
| `threadpool.c/h` | `kernel/core/sched/scheduler.c` | ⚠️ Needs update |
| `task_manager.c/h` | `kernel/core/sched/task.c` | ⚠️ Needs update |
| `fs_*.c/h` | `kernel/core/vfs/` | ⚠️ Needs update |
| `cluster.c/h` | `kernel/core/vfs/` | ⚠️ Needs update |
| `disk.c/h` | `kernel/core/vfs/` | ⚠️ Needs update |

### Kernel Architecture-Specific (x86_64)

| Old Location | New Location | Status |
|-------------|-------------|--------|
| `x86-64 (NASM)/mem_asm.asm` | `kernel/arch/x86_64/asm/memcpy.asm` | ✅ Moved |
| `x86-64 (NASM)/alloc/*.asm` | `kernel/arch/x86_64/asm/` | ✅ Moved |
| `x86-64 (NASM)/drivers/port_io.asm` | `kernel/arch/x86_64/drivers/port_io.asm` | ✅ Moved |
| `drivers/*.c/h` | `kernel/arch/x86_64/drivers/` | ⚠️ Needs move |
| `x86-64 (NASM)/disk_asm.c/h` | `kernel/arch/x86_64/mm/` | ⚠️ Needs move |

### Kernel Architecture-Specific (aarch64)

| Old Location | New Location | Status |
|-------------|-------------|--------|
| `ARM/mem_asm.s` | `kernel/arch/aarch64/asm/memcpy.S` | ✅ Moved |
| `ARM/alloc/*.s` | `kernel/arch/aarch64/asm/` | ✅ Moved |
| `ARM/drivers/port_io.s` | `kernel/arch/aarch64/drivers/port_io.S` | ✅ Moved |
| `ARM/disk_asm.c/h` | `kernel/arch/aarch64/mm/` | ⚠️ Needs move |

### Userland

| Old Location | New Location | Status |
|-------------|-------------|--------|
| `main.c` | `userland/shell/sh.c` | ⚠️ Needs move |
| `interpreter.c/h` | `userland/shell/builtins.c` | ⚠️ Needs move |
| `terminal.c/h` | `userland/shell/` | ⚠️ Needs move |
| `common.c/h` | `userland/shell/` | ⚠️ Needs move |
| `util.c/h` | `userland/shell/` | ⚠️ Needs move |
| `fs.c/h` | `userland/coreutils/` | ⚠️ Needs move |

### VM

| Old Location | New Location | Status |
|-------------|-------------|--------|
| `VM/*.c/h` | `vm/devices/` | ⚠️ Needs move |

### Tests

| Old Location | New Location | Status |
|-------------|-------------|--------|
| `tests/*.c` | `tests/` | ✅ Already in place |

## Next Steps

1. **Move remaining source files** to their new locations
2. **Update include paths** in all source files
3. **Create stub implementations** for missing architecture-specific files
4. **Update function signatures** to match new kernel API
5. **Test build** with CMake

## Include Path Updates

All includes should be updated to use the new structure:

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

The architecture-specific implementation will provide the optimized version.
