# AGENTS.md

## Cursor Cloud specific instructions

Assume a fresh Linux image may have no build libraries installed. Before building
or testing, install the project toolchain and optional VM/test dependencies:

`sudo apt-get update && sudo apt-get install -y build-essential gcc make binutils nasm gcc-aarch64-linux-gnu binutils-aarch64-linux-gnu pkg-config curl ca-certificates cmake autoconf automake libtool bzip2 tar libsdl2-dev libcunit1-dev`

Notes:
- `build-essential`, `gcc`, `make`, and `binutils` are required for the default C/GAS build.
- `nasm` is required for `ARCH=x86_64_nasm`.
- `gcc-aarch64-linux-gnu` and `binutils-aarch64-linux-gnu` are required for `ARCH=arm`.
- `libsdl2-dev` and `pkg-config` are required for `make vm-sdl` when not using `deps/install`.
- `libcunit1-dev` is required for the CUnit test binary.
- `curl`, `cmake`, `autoconf`, `automake`, `libtool`, `bzip2`, and `tar` are required by `make deps`, `make deps-sdl2`, and `make deps-cunit`.

## Build targets

Run builds from the repository root.

- Default host build: `make`
- Clean build artifacts: `make clean`
- x86_64 GAS build: `make ARCH=x86_64_gas`
- x86_64 NASM build: `make ARCH=x86_64_nasm`
- AArch64 cross build: `make ARCH=arm`
- x86 bare-metal-style build: `make baremetal`
- VM-enabled host build: `make vm`
- VM with SDL2 window: `make vm-sdl`
- Build local third-party dependencies into `deps/install`: `make deps`

## Test targets

- Driver subsystem tests: `make test_drivers`
- Core ASM and priority queue tests: `make test_core`
- Invariant tests: `make test_invariants`
- libc allocator tests: `make test_alloc_libc`
- ASM allocator tests: `make test_alloc_asm`
- VM memory tests: `make test_vm_mem`
- VM replay test: `make test_replay`
- Full architecture parity check: `make parity`

## Development testing policy

- Before changing code, run the most relevant existing tests for the affected area to establish a baseline.
- During development, test the changed item directly as soon as it is runnable; do not wait until the end to discover failures.
- After every code change, rerun the relevant tests and keep fixing until they pass. Treat the work as incomplete unless tests are passing with 100% confidence for the affected behavior.
- Add or update unit tests for new code, new behavior, bug fixes, and regressions whenever the repository has a suitable test target.
- For driver, architecture, VM, or allocator changes, include the closest focused target from the list above plus a build target that compiles the affected architecture or feature flags.
- If a required toolchain or library is missing, install the packages listed in the Cursor Cloud section, then rerun the tests instead of skipping them.
- Document any true environment blocker in the final response, including the exact command that failed and the missing prerequisite.

## Running executables

Build before running:

- Host shell: `make && ./BPForbes_Flinstone_Shell`
- Batch commands: `make && ./BPForbes_Flinstone_Shell help`
- CUnit suite: `make BPForbes_Flinstone_Tests && ./BPForbes_Flinstone_Tests`
- Embedded VM: `make vm && ./BPForbes_Flinstone_Shell -Virtualization -y -vm`
- SDL VM: `make vm-sdl && ./BPForbes_Flinstone_Shell -Virtualization -y -vm`

Prerequisites before running:
- The executable must exist from a successful build.
- For SDL VM runs, the environment must provide a display server or WSLg-style GUI session.
- If using local dependency builds, run `make deps` before `make vm-sdl` or CUnit tests.
- If switching `ARCH`, `VM_ENABLE`, `VM_SDL`, or `USE_ASM_ALLOC`, run `make clean` first to avoid stale objects.

## Useful validation sequence

For broad verification on a fully provisioned image:

`make clean && make && make test_drivers && make test_core && make test_invariants && make baremetal && make ARCH=arm`

For VM work:

`make clean && make vm && make test_vm_mem && make test_replay`

For SDL VM work:

`make deps && make clean && make vm-sdl`
