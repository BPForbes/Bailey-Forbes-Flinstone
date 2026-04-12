# Agents

## Cursor Cloud specific instructions

### Project overview

Flinstone is a low-level C-based file system and shell with x86-64 assembly primitives. It is a standalone binary — no web services, databases, or containers required.

### Build

The primary build system is **GNU Make** (not CMake, which is a migration-in-progress):

```bash
make          # builds BPForbes_Flinstone_Shell
make clean    # removes all build artifacts
```

The file `userland/shell/util.c` requires `#include <limits.h>` (added in setup PR) — without it, compilation fails on `INT_MAX`.

### Running the shell

- **Batch mode:** `./BPForbes_Flinstone_Shell <command> [args...]` — e.g. `./BPForbes_Flinstone_Shell dir .`
- **Interactive mode:** `./BPForbes_Flinstone_Shell` (enters a shell prompt; type `exit -y` to leave)
- Batch commands that create a disk (e.g. `createdisk`) will drop into interactive mode after completing — avoid these in non-interactive contexts unless you pipe in `exit -y`.

### Testing

See `README.md` for full test targets. Key commands:

| Command | Notes |
|---|---|
| `make test_mem_asm` | ASM memory primitives (no CUnit needed) |
| `make test_priority_queue` | Priority queue invariants |
| `make test_drivers` | Driver subsystem (runs `check-stubs` gate first) |
| `make test_invariants` | Path + cluster invariants |
| `make test_alloc_libc` | Allocator with libc |
| `make test_core` | Runs `test_mem_asm` + `test_priority_queue` |
| `make check-layers` | Layer architecture enforcement |
| `make check-stubs` | Ensures no forbidden stub markers |
| `make BPForbes_Flinstone_Tests && ./BPForbes_Flinstone_Tests` | CUnit suite (requires `libcunit1-dev`) |
| `./scripts/baseline_tests.sh` | Runs all tests in sequence with timeouts |

### Known issues

- The CUnit test suite (`BPForbes_Flinstone_Tests`) hangs at test 24 (`test_O_command` — redirect output) in non-interactive environments. The `baseline_tests.sh` script handles this with a timeout. Tests 1-23 pass.
- `make test_vm_mem` fails due to a missing header (`VM/vm_mem.h` — the header is at `VM/devices/vm_mem.h`). This is a pre-existing issue.

### System dependencies

The following must be installed via `apt` (not part of the C codebase):
- `libcunit1-dev` — for CUnit test suite
- `automake`, `autoconf`, `libtool` — for building optional deps from source (`make deps`)
