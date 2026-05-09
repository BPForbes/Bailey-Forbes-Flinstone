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

## Versioning

The shipped shell version uses integer components in **`userland/shell/version_def.h`** (`VERSION_MAJOR`, `VERSION_STANDARD`, `VERSION_PATCH`) and builds the **`VERSION`** string macro as **A.B.C**.

### Release notes (`version/`)

Each release adds **one new text file** under **`version/entries/`** ending in **`.ver`** (see **`version/entries/ABOUT.txt`**). Supported keys (optional leading `int ` before the name):

- **`MAJOR_VERSION`** (alias **`VERSION_MAJOR`**)
- **`STANDARD_VERSION`** (alias **`VERSION_STANDARD`**)
- **`RELEASE_VERSION`** — third component **C** (aliases **`MINOR_VERSION`**, **`VERSION_PATCH`**)
- **`DESCRIPTION`** — single line, max **1023** characters (quotes optional)

After a change is **merged**, existing **`version/entries/*.ver`** files are **immutable**: do not edit them; add another **`.ver`** file for the next bump. CI enforces this on pull requests against the merge base.

On a **feature branch before merge**, new `.ver` files that do **not** yet exist on the merge base are ordinary drafts: you may revise or remove them as the PR evolves without treating each push as “locking” them. Only entries already present on the **target** (detected via merge base) are protected.

### Lock system (agents, reviewers, and automation)

- **Never edit older version files.** Any **`.ver`** file that already exists on the **merge base / target branch** is **locked**: it is part of the permanent release record. Do **not** rewrite description text, fix typos in place, or refactor filenames for entries that have already shipped—add a **new** **`version/entries/*.ver`** for the next semver instead.
- **`version/locked/`** is a **read-only mirror** for humans and tools: it must stay a **byte-identical copy** of **`version/entries/`**. Update it **only** by running **`./scripts/sync_version_locked_mirror.sh`** or **`make sync-version-locked`** after legitimate changes under **`version/entries/`**. Never hand-edit **`version/locked/`** to diverge from **`version/entries/`**.
- **AI assistants** (Cursor, CodeRabbit, CLAUDE context, etc.) must **not** propose changes that modify historical **`.ver`** files or unsynchronized **`version/locked/`** copies. If a PR touches those paths incorrectly, **request a new entry file** instead.
- **CI** rejects PRs that modify merged entries (`scripts/check_version_entries_immutable.sh`) and rejects **`version/locked`** drift (`scripts/check_version_locked_mirror.sh`).

CI verifies the mirror, and that **`version_def.h`** matches the **highest** **A.B.C** among **`version/entries/*.ver`**.

**Changelog binary:** **GitHub Actions** compiles **`scripts/gen_version_changelog.c`**, which reads **`version/entries`** and **`version_def.h`**, then emits **`userland/shell/version_changelog.c`** (ignored by git). **`make … CHANGELOG_CI=1`** links **`VERSION_CHANGELOG[]`** in CI only. Plain **`make`** omits changelog unless you generate that file and pass **`CHANGELOG_CI=1`**. See **`scripts/templates/version_changelog.example.c`** for shape.

Use **semantic versioning**:

| Component | When to bump |
|-----------|----------------|
| **A** | Major milestones, architecture changes, large foundational overhauls |
| **B** | New features (additive behavior) |
| **C** | Bug fixes and small corrections |

If a single release mixes milestone/architecture work, features, and fixes: **increment only the most significant applicable component** (e.g. milestone + architecture → bump **A** only).

### Merge / PR expectation

Before merging **incoming → base** (e.g. `bug/*` → `develop`, `develop` → `main`):

1. Compare **`VERSION_*` / `VERSION` on the incoming branch** to **`VERSION_*` / `VERSION` on the target branch** (see `userland/shell/version_def.h`).
2. **Incoming must be strictly newer** than the target for that merge.
3. If both show the **same** version (e.g. both `2.0.0`), **update the incoming branch** so its version is **one appropriate semver step ahead** of the target.
4. Add a **new** **`version/entries/*.ver`** file describing the release (never rewrite an entry file that already merged) and run **`make sync-version-locked`** so **`version/locked/`** mirrors **`version/entries/`**.

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
