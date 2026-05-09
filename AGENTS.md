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
- Optional (not required to compile or run the shell): `dosfstools` (`dosfsck`, `mkfs.fat`) helps validate FAT32 disk images the project creates; it is not linked into the binary. See `docs/dependencies.md` for a consolidated list of system packages versus `make deps`.

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

## Versioning

The shipped shell version **A.B.C** is in **`userland/shell/version_def.h`**, **generated** from **`version/locked/*.ver`**: the header reflects the **highest** semver among **finalized** `.ver` files there. Author new and revised **`.ver`** files under **`version/entries/`**; when you are ready to publish, run **`./scripts/finalize_version_locked.sh`** or **`make finalize-version-locked`** (copies **`version/entries/`** → **`version/locked/`**), then **`./scripts/gen_version_def.sh`** or **`make`**, and commit the updated header. **CMake** reads **`project(VERSION)`** from that same header at configure time—do not hardcode a separate semver triple in **`CMakeLists.txt`**.

### Release notes (`version/`)

Author each release as **`.ver`** files under **`version/entries/`** (see **`version/entries/ABOUT.txt`**). The tree currently ships **`001_2_2_4_baseline.ver`**; for a new release add another **`.ver`** whose basename encodes the semver (often with a serial prefix such as **`002_…`**). Ordering uses the numeric **MAJOR/STANDARD/RELEASE** fields inside the file, not the filename prefix. Supported keys (optional leading `int ` before the name):

- **`MAJOR_VERSION`** (alias **`VERSION_MAJOR`**)
- **`STANDARD_VERSION`** (alias **`VERSION_STANDARD`**)
- **`RELEASE_VERSION`** — third component **C** (aliases **`MINOR_VERSION`**, **`VERSION_PATCH`**)
- **`DESCRIPTION`** — single line, max **1023** characters (quotes optional)

On a **feature branch before merge**, you may freely add, edit, or remove **`.ver`** files under **`version/entries/`** that are **not** yet reflected in **`version/locked/`**.

### Lock system (agents, reviewers, and automation)

- **Never edit finalized release files.** Any path under **`version/locked/`** that already exists on the **merge base / target branch** is **immutable**: it is the published record. Do **not** rewrite it in place—add or adjust prose under **`version/entries/`**, then **finalize** again when appropriate. CI enforces immutability on **`version/locked/`** for PRs (`scripts/check_version_locked_immutable.sh`).
- **`version/locked/`** is the **published snapshot** copied from **`version/entries/`** via **`./scripts/finalize_version_locked.sh`** (alias: **`make finalize-version-locked`** / **`make sync-version-locked`**). Until you finalize, **`version/entries/`** may contain extra drafts not present in **`version/locked/`**.
- **CI** requires every file under **`version/locked/`** to exist under **`version/entries/`** with **identical content** (`scripts/check_version_locked_subset_of_entries.sh`)—so you cannot “advance” locked without aligning entries, and you cannot silently diverge a finalized file from the matching path under **`version/entries/`**.
- **AI assistants** must **not** propose edits to historical paths under **`version/locked/`** that shipped on the target branch, or to **`version/entries/`** files that must byte-match **`version/locked/`** for the same relative path, unless the change is part of an intentional finalize-and-regenerate workflow.

CI verifies that the committed **`version_def.h`** matches **`scripts/gen_version_def.sh`** output (highest triple in **`version/locked/*.ver`**).

**Changelog binary:** **GitHub Actions** compiles **`scripts/gen_version_changelog.c`**, which reads **`version/locked`** (headline version = highest finalized entry), then emits **`userland/shell/version_changelog.c`** (ignored by git). **`make … CHANGELOG_CI=1`** links **`VERSION_CHANGELOG[]`** in CI only. Plain **`make`** omits changelog unless you generate that file and pass **`CHANGELOG_CI=1`**. See **`scripts/templates/version_changelog.example.c`** for shape.

Use **semantic versioning**:

| Component | When to bump |
|-----------|----------------|
| **A** | Major milestones, architecture changes, large foundational overhauls |
| **B** | New features (additive behavior) |
| **C** | Bug fixes and small corrections |

**Precedence (importance):** **`A` > `B` > `C`**. For each new release, bump **only** the **highest** precedence that applies to the whole release: **increment** that component, **leave unchanged** every more-significant component to its **left**, and **set to `0`** every less-significant component to its **right**. Example: **`2.2.4` → `2.3.0`** for a minor release (not `2.3.4`). **`2.3.7` → `3.0.0`** for a major release. **`2.3.0` → `2.3.1`** for a patch.

If a single release mixes milestone/architecture work, features, and fixes: **increment only the most significant applicable component** once (e.g. milestone + architecture → bump **A** only → **`3.0.0`** after **`2.x.y`**).

### Merge / PR expectation

Before merging **incoming → base** (e.g. `bug/*` → `develop`, `develop` → `main`):

1. Compare **`VERSION_*` / `VERSION` on the incoming branch** to **`VERSION_*` / `VERSION` on the target branch** (see `userland/shell/version_def.h`, generated from **`version/locked/*.ver`**).
2. **Incoming must be strictly newer** than the target for that merge.
3. If both show the **same** version (e.g. both `2.0.0`), **update the incoming branch** so its version is **one appropriate semver step ahead** of the target.
4. Add a **new** **`version/entries/<A>_<B>_<C>_<slug>.ver`**, run **`make finalize-version-locked`**, then **`./scripts/gen_version_def.sh`** (or **`make`**) so **`version_def.h`** updates from **`version/locked/`**.

Example: **`bug/…` → `develop`**, both at **`2.0.0`** → bump incoming to **`2.0.1`** (patch for a bugfix).

Detailed wording also appears in **CLAUDE.md**, **`.coderabbit.yaml`**, and **`.cursor/rules/versioning.mdc`** — keep them aligned when policy changes. To print a machine-readable record from the current tree: `./scripts/export_version_record.sh`, `./scripts/export_version_record.sh --json`, or **`make version-record`**.

## Implementation boundaries

- Memory primitives, allocator internals, low-level synchronization, port I/O, and core hardware-facing routines should be backed by the architecture-specific ASM layer.
- Keep C code focused on higher-order application logic, driver orchestration, VM behavior, filesystem services, and policy/business rules.
- When adding core memory behavior, prefer extending the existing ASM-backed primitives and C wrappers instead of introducing parallel libc-only paths.
- Host-only fallbacks are acceptable for tests and non-baremetal builds, but preserve the ASM-backed contract for kernel, driver, and baremetal paths.

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
