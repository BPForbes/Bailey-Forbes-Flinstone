# Architecture Documentation

## Overview

The Bailey-Forbes-Flinstone project is organized into several major components:

- **kernel/**: Core kernel code, split into architecture-independent core and architecture-specific implementations
- **userland/**: User-space programs (shell, coreutils, package manager)
- **vm/**: VM harness for running guest code
- **tests/**: Test suite

## Directory Structure

```
kernel/
├── include/fl/          # Kernel API headers
├── core/                # Architecture-independent code
│   ├── mm/              # Memory management
│   ├── sched/           # Scheduler and task management (MLQ, priority_queue)
│   ├── vfs/             # Virtual file system
│   ├── ipc/             # Inter-process communication
│   └── sys/             # System calls
└── arch/                # Architecture-specific code
    ├── x86_64/          # x86-64 implementation (NASM)
    └── aarch64/         # ARM64 implementation (GAS)
```

## Architecture Abstraction

The kernel uses an architecture abstraction layer defined in `kernel/include/fl/arch.h`. This provides:

- Memory management hooks
- Interrupt handling
- Context switching
- CPU-specific operations

Each architecture implements these hooks in their respective `arch/` directories.

## Scheduler (MLQ)

The multi-level queue (`priority_queue.c/h`) provides O(1) push/pop/update/remove:

- **Priority semantics**: Priority 0 is highest; lower number = higher priority. `ctzll(nonempty_mask)` selects the highest-priority non-empty layer.
- **FIFO within layer**: Tasks in the same priority level are served first-in-first-out.
- **Invariants**: `PQ_NUM_PRIORITIES <= 64` (bitmap width), `PQ_MAX_ITEMS <= 255` (handle slot packing).

## Build System

The project uses CMake with architecture-specific toolchains. See `docs/build.md` for build instructions.
