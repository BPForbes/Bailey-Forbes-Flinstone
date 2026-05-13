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

Long-form guide: **`docs/versioning.md`** (authoring **`.ver`** files, **`RELEASE_DATE`** at relocate and after merge to **`develop`**, **A / B / C** semantics, and what GitHub Actions updates).

The shipped shell version **A.B.C** is in **`userland/shell/version_def.h`**, **generated** from all **`version/locked/**/*.ver`** files: **`VERSION`**, **`VERSION_MAJOR`**, **`VERSION_STANDARD`**, and **`VERSION_PATCH`** reflect the **highest** semver among them (**`PRERELEASE`**, **`GM`**, **`DEV_VERSION`**, and **`preproduction A.B.C/`** paths are ignored for those macros). The same header defines **`VERSION_LINE`**, the string shown by the shell **`version`** command: when **`version/entries/**/*.ver`** contains **`PRERELEASE=1`**, **`VERSION_LINE`** uses the newest such row (**highest semver, then highest `DEV_VERSION`**) as **`PRERELEASE_TAG A.B.C`** plus optional **`, BUILD <DEV_VERSION>`** (comma, space, literal BUILD, space, number); otherwise **`VERSION_LINE`** matches **`VERSION`**. See **`docs/versioning.md`** and **`make promote-preproduction-for-main`** before **`main`**. Author new and revised **`.ver`** files under **`version/entries/`**; when those changes land on **`develop`** (push), **Version lock on merge** in GitHub Actions runs finalize + header generation and **opens a pull request** into **`develop`** with the updated **`version/locked/`** and **`version_def.h`** (direct pushes to **`develop`** are usually blocked; use **Settings → Actions → General** workflow **read/write** plus **Allow GitHub Actions to create and approve pull requests**, or optional repo secret **`VERSION_LOCK_PAT`**).

**Cursor / AI agents:** on feature PRs, **commit `version/entries/` only** (typically new or revised **`version/entries/*.ver`**). **Do not** commit **`version/locked/**`** on typical PRs—only **maintainers** running **`./scripts/finalize_version_locked.sh`** or **`make finalize-version-locked`** locally (or CI snapshots after merge to **`develop`**) should update **`version/locked/`**. **`userland/shell/version_def.h`** is generated from **`version/locked/`** plus **`VERSION_LINE`** from **`version/entries/**/*.ver`** when **`PRERELEASE=1`** exists—**regenerate with `./scripts/gen_version_def.sh` and commit the header** whenever your **`.ver`** edits change **`VERSION_LINE`**, or CI **`check_version_header_matches_locked`** fails. Keep **`version/entries/ABOUT.txt`** byte-identical to the merge target's **`version/locked/ABOUT.txt`** while **`version/locked/`** matches the target. **Maintainers** may run **`./scripts/finalize_version_locked.sh`** or **`make finalize-version-locked`** locally before merge when they want an early snapshot on the branch. **CMake** reads **`project(VERSION)`** from that same header at configure time—do not hardcode a separate semver triple in **`CMakeLists.txt`**.

### Release notes (`version/`)

Author each release as **`.ver`** files under **`version/entries/`** (see **`version/entries/ABOUT.txt`**). The tree ships a legacy example **`001_2_2_4_baseline.ver`**; for new work prefer **`A_B_C_short_slug.ver`** (semver in the first three underscore-separated components, not a numeric serial prefix like **`006_`**). Ordering uses the numeric **MAJOR/STANDARD/RELEASE** fields inside the file, not the filename. Supported keys (optional leading `int ` before the name):

- **`MAJOR_VERSION`** (alias **`VERSION_MAJOR`**)
- **`STANDARD_VERSION`** (alias **`VERSION_STANDARD`**)
- **`RELEASE_VERSION`** — third component **C** (aliases **`MINOR_VERSION`**, **`VERSION_PATCH`**)
- **`DESCRIPTION`** — single line (`DESCRIPTION=...`) or multiline heredoc (`DESCRIPTION<<TAG` … `TAG`); see `version/entries/ABOUT.txt`
- **`RELEASE_DATE`** — optional `YYYY-MM-DD`. Root **`PRERELEASE=1`** rows get **`RELEASE_DATE`** on the **relocate** run (calendar day) if missing, then move under **`preproduction A.B.C/`** (`./scripts/relocate_root_prerelease_ver_to_preproduction.sh`, CI). Other rows usually omit **`RELEASE_DATE`** in **`version/entries/`** and let **Version lock on merge** append the merge calendar date via **`stamp_version_release_date.sh`** after finalize (**`stamp_version_release_date.sh`** stamps **`version/locked/**/*.ver`**, matching paths under **`version/entries`**, and **every** **`version/entries/**/*.ver`**, including under **`preproduction */`**, so preproduction-only rows are covered). If still absent, `scripts/gen_version_changelog.c` may use the generator’s local calendar date (`time.h`) at generation time
- **`PRERELEASE`** — **`0`** or **`1`**; **`1`** = prerelease row **authored at the `version/entries/` root** on same-repo branches (CI **`relocate_root_prerelease_ver_to_preproduction.sh`** moves it under **`preproduction A.B.C/`**; **do not** hand-add new **`PRERELEASE=1`** **`.ver`** paths under that directory on same-repo PRs). Fork PRs must relocate locally — see **`docs/versioning.md`**.
- **`DEV_VERSION`** — optional non-negative int on **root** rows while still **`PRERELEASE=0`** (tracks develop-side iteration). With **`PRERELEASE=1`**, use **`>= 1`** and bump with **`./scripts/bump_dev_version.sh`**.
- **`GM`** — **`0`** or **`1`** (go-to-main). **`GM=1`** is allowed **only** under **`preproduction A.B.C/`** and at most once per directory. Before **`main`**, run **`./scripts/promote_preproduction_for_main.sh`**, which merges **all** prerelease **`.ver`** rows in that folder (sorted by **`DEV_VERSION`**) into one root GA file under **`version/entries/`**, **drops** **`PRERELEASE`**, **`GM`**, and **`DEV_VERSION`**, and **removes** the **`preproduction *`** directory from **`version/entries`** (and from **`version/locked`** only if a legacy copy exists). **`finalize_version_locked.sh`** does **not** copy **`preproduction */`** into **`locked`**; the merged root **`.ver`** is what publish sees on the next finalize. **`main`** must have no **`preproduction *`** dirs and no **`PRERELEASE=1`**, **`GM=1`**, or **`DEV_VERSION=`** lines — CI **`check_version_main_prerelease_policy.sh`**.

On a **feature branch before merge**, you may freely add, edit, or remove **`.ver`** files under **`version/entries/`** that are **not** yet reflected in **`version/locked/`**.

**AI assistants:** When you begin the **first** substantive code change for a pull request, **add** or extend **`version/entries/*.ver`** so the work is documented. For **`PRERELEASE=1`**, **author new rows at the `version/entries/` root only** on same-repo branches—**do not** hand-add new **`*.ver`** paths under **`preproduction <A>.<B>.<C>/`**; CI runs **`relocate_root_prerelease_ver_to_preproduction.sh`** (stamps missing **`RELEASE_DATE`**, then moves into that directory). Fork PRs follow **`docs/versioning.md`** (relocate locally, then commit). **Do not backtrack semver against an active preproduction version:** the **preproduction version** is **A.B.C** from **`version/entries/preproduction <A>.<B>.<C>/`** after relocate (**`PRERELEASE=1`** rows land there); do **not** add a **new root** **`version/entries/*.ver`** whose **A.B.C** is **numerically below** that **A.B.C** unless a **human maintainer** explicitly asks for that separate shipped release—iterate with **`DEV_VERSION`** or **another root `*.ver` file** with the same **`MAJOR`/`STANDARD`/`RELEASE`** and **`PRERELEASE=1`**. For a **single GA feature** at **`version/entries/`** root without **`preproduction */`**, **one** **`.ver`** revised in place is often enough. **Do not** commit **`version/locked/**`** on agent-authored feature PRs; commit **`userland/shell/version_def.h`** **only** when regenerated from a changed **`VERSION_LINE`** in **`version/entries/*.ver`** (see **Versioning** above).

### Lock system (agents, reviewers, and automation)

- **Never edit finalized release files.** Any path under **`version/locked/`** that already exists on the **merge base / target branch** is **immutable**: it is the published record. Do **not** rewrite it in place—add or adjust prose under **`version/entries/`**; publishing **`version/locked/`** is done by **Version lock on merge** (or by **maintainers** running finalize locally). CI enforces immutability on **`version/locked/`** for PRs (`scripts/check_version_locked_immutable.sh`), except **`version/locked/ABOUT.txt`**, which is companion documentation and may change with **`version/entries/ABOUT.txt`** in the same PR.
- **`version/locked/`** is the **published snapshot** produced from **`version/entries/`** by **`./scripts/finalize_version_locked.sh`** (alias: **`make finalize-version-locked`** / **`make sync-version-locked`**): same as entries **except** top-level **`preproduction */`** directories are **not** copied into **`locked`**. Until automation (or a maintainer) finalizes, **`version/entries/`** may contain extra drafts (including under **`preproduction */`**) not present in **`version/locked/`**.
- **CI** requires every file under **`version/locked/`** to exist under **`version/entries/`** with **identical content** (`scripts/check_version_locked_subset_of_entries.sh`)—so you cannot “advance” locked without aligning entries, and you cannot silently diverge a finalized file from the matching path under **`version/entries/`**.
- **AI assistants** must **not** propose edits to historical paths under **`version/locked/`** that shipped on the target branch, or to **`version/entries/`** files that must byte-match **`version/locked/`** for the same relative path, except when intentionally updating **`version/entries/ABOUT.txt`** to stay identical to the target’s **`version/locked/ABOUT.txt`** while **`version/locked/`** is unchanged.

CI verifies that the committed **`version_def.h`** matches **`scripts/gen_version_def.sh`** output (**`VERSION_*`** / **`VERSION`** from **`version/locked/**/*.ver`**, **`VERSION_LINE`** from **`version/entries/**/*.ver`** when **`PRERELEASE=1`** rows exist).

**Changelog binary:** **GitHub Actions** compiles **`scripts/gen_version_changelog.c`**, which reads **`version/locked` recursively** (headline version = highest finalized entry), then emits **`userland/shell/version_changelog.c`** (ignored by git). **`make … CHANGELOG_CI=1`** links **`VERSION_CHANGELOG[]`** in CI only. Plain **`make`** omits changelog unless you generate that file and pass **`CHANGELOG_CI=1`**. See **`scripts/templates/version_changelog.example.c`** for shape.

### Version lock on merge (GitHub Actions)

On every **`push`** to **`develop`**, workflow **`version-lock-on-merge.yml`** runs **`relocate_root_prerelease_ver_to_preproduction.sh`**, then **`finalize_version_locked.sh`** (copies **`version/entries/`** → **`version/locked/`**, **excluding** top-level **`preproduction */`** trees), **`stamp_version_release_date.sh`**, then **`gen_version_def.sh`**. If anything changed, it **opens a PR** into **`develop`** (via **`peter-evans/create-pull-request`**) instead of pushing directly, so branch protection and checks apply when someone merges that PR. Use **`GITHUB_TOKEN`** with the **Actions** settings above, **or** secret **`VERSION_LOCK_PAT`** (PAT with **contents** + **pull-requests** write). Workflow permissions: **`contents: write`** and **`pull-requests: write`**.

**Automation version PRs (review policy):** PRs opened **only** by that workflow (title/message like **`chore(version): sync version/locked from entries after merge`**, branch `cursor/version-lock-from-entries-*`) are **routine housekeeping**. **Do not** spend automatic or proactive bot review on them unless a **human** explicitly requests reviewers or asks for a review—then review normally.

### Optional deploy build (GitHub Actions / local)

Workflow **Deploy** (`.github/workflows/deploy.yml`, **workflow_dispatch**) assumes **`version/locked/`** is already up to date: it builds the changelog and runs **`make CHANGELOG_CI=1`**. **`make deploy`** does the same locally (changelog + **`make CHANGELOG_CI=1 all`**). Use **`make finalize-version-locked`** locally if you need **`version/locked/`** refreshed before merge without waiting for the merge workflow.

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

1. Compare **published** **`VERSION_*` / `VERSION` on the target branch** (`userland/shell/version_def.h`, from **`version/locked/*.ver`** on the target) to the **incoming** release: either the semver in **`version/entries/*.ver`** on the incoming branch (must exceed the target) or incoming **`version_def.h`** if the PR already updated **`version/locked/`** via a maintainer finalize.
2. **Incoming must be strictly newer** than the target for that merge.
3. If both show the **same** version (e.g. both `2.0.0`), **update the incoming branch** so its **entries** record a version **one appropriate semver step ahead** of the target.
4. Add **`version/entries/<A>_<B>_<C>_<slug>.ver`** as needed for the bump (typically **one** new file per feature PR; **edit** that file as the PR evolves rather than stacking several). Pushing to **`develop`** runs **Version lock on merge** in GitHub Actions (**relocate**, copies **`version/entries/`** → **`version/locked/`** without **`preproduction */`**, regenerates **`version_def.h`**, then opens a PR to merge those updates when needed). **Maintainers** may run **`make finalize-version-locked`** locally if they want **`version/locked/`** updated on the branch before merge.

Example: **`bug/…` → `develop`**, both at **`2.0.0`** → bump incoming to **`2.0.1`** (patch for a bugfix).

Detailed wording also appears in **CLAUDE.md**, **`docs/versioning.md`**, **`.coderabbit.yaml`**, **`.cursor/rules/versioning.mdc`**, and **`.cursor/rules/review_tools.mdc`** — keep them aligned when versioning or optional review-hint text changes. To print a machine-readable record from the current tree: `./scripts/export_version_record.sh`, `./scripts/export_version_record.sh --json`, or **`make version-record`**.

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
