# AGENTS.md

## Cursor Cloud specific instructions

Assume a fresh Linux image may have no build libraries installed. Before building
or testing, install the project toolchain and optional VM/test dependencies:

`sudo apt-get update && sudo apt-get install -y build-essential gcc g++ make binutils nasm gcc-aarch64-linux-gnu g++-aarch64-linux-gnu binutils-aarch64-linux-gnu pkg-config curl ca-certificates cmake autoconf automake libtool bzip2 tar libsdl2-dev libcunit1-dev libsqlite3-dev`

Notes:
- `build-essential`, `gcc`, `make`, and `binutils` are required for the default C/GAS build.
- `nasm` is required for `ARCH=x86_64_nasm`.
- `gcc-aarch64-linux-gnu`, `g++-aarch64-linux-gnu` (or `gcc-aarch64-linux-gnu` with `-x c++`), and `binutils-aarch64-linux-gnu` are required for `ARCH=arm` on x86 hosts.
- For AArch64 cross links that use SQLite (`user_db.c`), run **`./deps/fetch-sqlite-aarch64.sh`** (or **`make deps-sqlite-aarch64`**) to build **`deps/install-aarch64/lib/libsqlite3.a`** when apt multiarch is unavailable (GitHub Actions ARM job).
- For AArch64 cross links that use OpenSSL (`password_hash.cpp`), run **`./deps/fetch-openssl-aarch64.sh`** (or **`make deps-openssl-aarch64`**) — CI **build-arm** does this after **fetch-sqlite-aarch64**; host **`libssl-dev`** headers are the wrong architecture on x86 runners.
- `libsdl2-dev` and `pkg-config` are required for `make vm-sdl` when not using `deps/install`.
- `libcunit1-dev` is required for the CUnit test binary.
- `libsqlite3-dev` and `g++` are required for SQLite account storage and password hashing (`userland/identity/password_hash.cpp`).
- `curl`, `cmake`, `autoconf`, `automake`, `libtool`, `bzip2`, and `tar` are required by `make deps`, `make deps-sdl2`, and `make deps-cunit`.
- Optional (not required to compile or run the shell): `dosfstools` (`dosfsck`, `mkfs.fat`) helps validate FAT32 disk images the project creates; it is not linked into the binary. See `docs/dependencies.md` for a consolidated list of system packages versus `make deps`.

## Replit Agent

On [Replit](https://replit.com/~), the in-Repl **Replit Agent** uses **`replit.md`**, which states the **full explicit rules** (versioning, lock system, git workflow, build/test)—the **same** mandatory policy as **CLAUDE.md**, **AGENTS.md** (this file), **`.cursor/rules/*.mdc`**, and **`.coderabbit.yaml`**. App config: **`.replit`**, **`replit.nix`**.

## AI feature-branch workflow (Cursor / Claude / Codex / Replit)

When an agent opens a pull request or branch for one task and later needs to implement **another** item that is **not** part of that branch’s original goal, **do not** create a new branch **unless** a human explicitly allows it. **Stay on the current** feature branch and commit the additional work there—unless a human tells you to use a separate branch.

If a human **does** allow a side branch for the other work, **merge** that side branch into the main feature branch **without deleting** the side branch first, so history and review stay aligned.

When folding **two or more** AI-authored feature branches together (for example **`cursor/*`**, **`claude/*`**, **`codex/*`**, or other agent branches that eventually merge into **`main`** or **`develop`**), use a **non–fast-forward** merge so the combine point is visible:

`git checkout <main-feature-branch> && git merge --no-ff <other-agent-branch> -m "merge: …"`

Avoid a silent fast-forward when merging unrelated agent work. (Maintainer shorthand “merge collapse, no ff” here means **`git merge --no-ff`**, not fast-forward.)

**Versioning (merged AI branches):** after **`git merge --no-ff`**, **delete** the merged-in branch’s duplicate **`.ver`** for the same **A.B.C** train. **Do not edit** immutable **`.ver`** paths from the merge base—fold combined release prose into the **newest** **`.ver`** your combined PR adds (or add **one new** root **`.ver`** with the next **`DEV_VERSION`**). Re-run **`./scripts/check_version_entries_semver_dev_unique.sh`**.

**Immutable `.ver` rows:** any **`version/entries/**/*.ver`** on the merge base must not be edited; each iteration adds **one new** root **`A_B_C_slug.ver`**. **`GM=1`** go-to-main rows use **`DESCRIPTION`** starting with **`A.B.C:`** and an overall release summary (see **`docs/versioning.md`**).

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
- Contract umbrella revs (JSON): `./BPForbes_Flinstone_Shell contracts json` (includes `p8_virtualization_rev`, `p9_hardening_rev`, `bundle_rev`)
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

## P3 networking (implementation)

Hosted and loopback IPv4 probes (`ping`, `check requirements`) live in **`kernel/core/net/`**. See **`docs/P3_NETWORKING.md`** for layer map, ASM backing (**`arch/*/net_asm.*`**, **`arch/*/net_wire_host_asm.*`** on x86_64 NASM/GAS and AArch64), and P3-x status. Tests: **`make test_p3_network`**, **`make check-network-requirements`**.

## Module contracts (AI)

In this repository, a **module contract** is a **normative, stable model of how data and outcomes may cross a boundary** between subsystems: which buffers or handles move where, who allocates or frees them, which error or status channels apply, and which **surfaces** exist for interchange. It is **not product functionality by itself**—it is the **blueprint for allowed I/O and responsibility**. Headers under **`contracts/foundations/`** (P0 bundle) and closely related **`fl/*`** boundary headers are the primary **C artifacts** for contracts today; **`contract_extend.h`** is the documented prelude for future **Px** headers under **`contracts/extensions/`**. **Implementation** (drivers, rate limits, IRQ paths) may **enforce** a contract but is **not** a substitute for an explicit distribution model.

**AI / contributors should:**

- Extend **contract headers** and the **`contracts`** surface list when introducing a **new interchange boundary** other code must respect.
- Keep **enforcement code** (mutexes, rate limits, drivers) clearly secondary to the **written model** when the goal is “close a contract row” in the roadmap sense.
- Use the **❌ / ⚠️ / ✅** snapshot in **`docs/ROADMAP.md` → [Module contracts (abstraction and P0-P9 coverage)](#module-contracts-abstraction-and-p0-p9-coverage)** to see which **P0–P9** rows still lack a **data-distribution** contract on **`develop`**.

## Versioning

Long-form guide: **`docs/versioning.md`** (authoring **`.ver`** files, **`RELEASE_DATE`** at relocate and after merge to **`develop`**, **A / B / C** semantics, and what GitHub Actions updates).

The shipped shell version **A.B.C** is in **`userland/shell/version_def.h`**, **generated** by **`scripts/gen_version_def.sh`**: **`VERSION`**, **`VERSION_MAJOR`**, **`VERSION_STANDARD`**, and **`VERSION_PATCH`** normally reflect the **highest** semver among **`version/locked/**/*.ver`** (**`PRERELEASE`**, **`GM`**, **`DEV_VERSION`**, and **`preproduction A.B.C/`** paths are ignored **in locked files** for those macros). **Exception:** when any **`version/entries/**/*.ver`** row has **`PRERELEASE=1`** and **`GM=1`**, the **newest** such row (**highest semver, then highest `DEV_VERSION`**) supplies **`MAJOR`/`STANDARD`/`RELEASE`** for **`VERSION_*`** / **`VERSION`**, and **`VERSION_LINE`** is **plain `A.B.C`** (no **`PRERELEASE_TAG`**, no **`, BUILD <DEV_VERSION>`**). Otherwise **`VERSION_LINE`**, the string shown by the shell **`version`** command, follows **`PRERELEASE=1`** rows as **`PRERELEASE_TAG A.B.C`** plus optional **`, BUILD <DEV_VERSION>`** when **`DEV_VERSION`≥1**; if no **`PRERELEASE=1`** rows exist, **`VERSION_LINE`** matches **`VERSION`**. See **`docs/versioning.md`** and **`make promote-preproduction-for-main`** before **`main`**. Author new and revised **`.ver`** files under **`version/entries/`**; when those changes land on **`develop`** (push), **Version lock on merge** in GitHub Actions runs finalize + header generation and **opens a pull request** into **`develop`** with the updated **`version/locked/`** and **`version_def.h`** (direct pushes to **`develop`** are usually blocked; use **Settings → Actions → General** workflow **read/write** plus **Allow GitHub Actions to create and approve pull requests**, or optional repo secret **`VERSION_LOCK_PAT`**).

**AI — `userland/shell/version_def.h`, `DEV_VERSION`, and `GM`:** **Never** stage or **commit** **`userland/shell/version_def.h`** on Cursor- or AI-authored same-repo feature PRs (**`make`** may rewrite it locally—**do not** **`git add`** it; reset to the merge target if needed). **Do not** hand-edit **`VERSION_MAJOR`**, **`VERSION_STANDARD`**, **`VERSION_PATCH`**, or **`VERSION_LINE`**, and **do not** run **`./scripts/gen_version_def.sh`** just to publish header updates after a maintainer sets **`GM=1`** on **`.ver`**—**GitHub Actions** (`.github/workflows/c-cpp.yml` **version-requirements**: relocate + **`gen_version_def.sh`**, optional bot push; **Version lock on merge** on **`develop`**) applies **`gen_version_def.sh`** and updates those macros when **`GM=1`** (plain **`A.B.C`** **`VERSION_LINE`**) or ordinary prerelease rules apply. **Never** set **`GM=1`** (even under **`preproduction */`**) unless a **human maintainer** explicitly tells you to—go-to-main is not an agent decision. **`DEV_VERSION`:** for each **`.ver`** your PR adds or updates, compare to **`develop`** (merge base): let **`N`** be **`DEV_VERSION`** on **`develop`** for the same relative path, or **`0`** if that file does not exist there. You may set **`DEV_VERSION`** to exactly **`N+1`** once when establishing or superseding that row; **do not** change **`DEV_VERSION`** again after that—revise **`DESCRIPTION`** and other prose only. **Never increase** **`DEV_VERSION`** after it is **established** on the branch (for example **8→9** mid-PR); that is the same violation as a second bump, not a substitute for **`DESCRIPTION`**. **Do not** run **`./scripts/bump_dev_version.sh`** on your own to chase BUILD numbers mid-PR.

**Reviewers (CodeRabbit, Codex, humans) — `version_def.h` on feature PRs:** Inspect **all commits on the PR branch** (not only the latest diff hunk). **Do not** flag **`VERSION_LINE`** / **`VERSION_*`** churn when the change is **GitHub Actions** publish: author **`github-actions[bot]`** (or equivalent), and/or commit subject/body matching **`chore(version):`**, **`gen_version_def.sh`**, **`prepare-version-entries`**, **`sync entries promote, finalize locked, and version_def.h`**, or **Version lock on merge**. Treat that as legitimate CI aligned with **`version/entries/**/*.ver`** on the same branch—**skip** “do not commit **`version_def.h`**” / “revert **`VERSION_LINE`**” unless a **non-automation** commit hand-edits the header without a matching entries-only workflow. **Mixed branches:** attribute the header to automation when the **`VERSION_LINE`** delta matches **`gen_version_def.sh`** output for the branch’s **`.ver`** trees (see **`.coderabbit.yaml`**).

**Cursor / AI agents:** on feature PRs, **commit `version/entries/` only** (typically **one new** **`version/entries/A_B_C_slug.ver`** at the **`version/entries/`** root **per PR** for that PR’s scope—**do not edit** any **`.ver`** path that already exists on the merge base; add a **new** root row for each iteration instead). **`GM=1` (maintainer only):** add a **new** **`.ver`** under **`version/entries/preproduction <A>.<B>.<C>/`** with the next **`DEV_VERSION`** (**never** set **`GM=1`** on a root **`*.ver`** file); **`DESCRIPTION`** must begin **`A.B.C:`** with the overall release summary (see **`docs/versioning.md`**). **Never** hand-add new PR `.ver` files under **`version/entries/preproduction */`**; GitHub Actions **`relocate_root_prerelease_ver_to_preproduction.sh`** moves root **`PRERELEASE=1`** rows there). **Do not** commit **`version/locked/**`** on typical PRs—only **maintainers** running **`./scripts/finalize_version_locked.sh`** or **`make finalize-version-locked`** locally (or CI snapshots after merge to **`develop`**) should update **`version/locked/`**. The header is generated from **`version/locked/`** and **`version/entries/**/*.ver`** (normal prerelease **`VERSION_LINE`** rules, or **`GM=1`** override—see **`docs/versioning.md`**); **CI** relocates root **`PRERELEASE=1`** rows, runs **`gen_version_def.sh`**, and may **push** one commit for **`version/entries`** + the header when needed (same-repo only; **Version lock on merge** publishes on **`develop`**/**`main`**). Fork PRs and offline workflows follow **`docs/versioning.md`**. Keep **`version/entries/ABOUT.txt`** byte-identical to the merge target's **`version/locked/ABOUT.txt`** while **`version/locked/`** matches the target. **Maintainers** may run **`./scripts/finalize_version_locked.sh`** or **`make finalize-version-locked`** locally before merge when they want an early snapshot on the branch. **CMake** reads **`project(VERSION)`** from that same header at configure time—do not hardcode a separate semver triple in **`CMakeLists.txt`**.

### Release notes (`version/`)

Author each release as **`.ver`** files under **`version/entries/`** (see **`version/entries/ABOUT.txt`**). The tree ships a legacy example **`001_2_2_4_baseline.ver`**; for new work prefer **`A_B_C_short_slug.ver`** (semver in the first three underscore-separated components, not a numeric serial prefix like **`006_`**). Ordering uses the numeric **MAJOR/STANDARD/RELEASE** fields inside the file, not the filename. Supported keys (optional leading `int ` before the name):

- **`MAJOR_VERSION`** (alias **`VERSION_MAJOR`**)
- **`STANDARD_VERSION`** (alias **`VERSION_STANDARD`**)
- **`RELEASE_VERSION`** — third component **C** (aliases **`MINOR_VERSION`**, **`VERSION_PATCH`**)
- **`DESCRIPTION`** — single line (`DESCRIPTION=...`) or multiline heredoc (`DESCRIPTION<<TAG` … `TAG`); see `version/entries/ABOUT.txt`
- **`RELEASE_DATE`** — optional `YYYY-MM-DD`. On feature PRs, **`.github/actions/prepare-version-entries`** runs **`relocate_root_prerelease_ver_to_preproduction.sh`**, **`promote_preproduction_for_main.sh --normalize-gm-only`**, **`promote_preproduction_for_main.sh`** (GA promote when **`GM=1`** is present), then **`stamp_version_entries_release_date.sh`** (entries root **`*.ver`** and every **`preproduction <A>.<B>.<C>/`** tree). Omit **`RELEASE_DATE=`** on new rows; let CI stamp. **Version lock on merge** on **`develop`** also runs **`stamp_version_release_date.sh`** after finalize. If still absent, `scripts/gen_version_changelog.c` may use the generator’s local calendar date (`time.h`) at generation time
- **`PRERELEASE`** — **binary `0` or `1` only** when present; **`1`** = prerelease row **authored at the `version/entries/` root** on same-repo branches (CI **`relocate_root_prerelease_ver_to_preproduction.sh`** moves it under **`preproduction A.B.C/`**; **do not** hand-add new **`PRERELEASE=1`** **`.ver`** paths under that directory on same-repo PRs). Fork PRs must relocate locally — see **`docs/versioning.md`**.
- **`DEV_VERSION`** — optional non-negative int on **root** rows while still **`PRERELEASE=0`** (tracks develop-side iteration). With **`PRERELEASE=1`**, use **`>= 1`**. **Cursor / AI:** compare to **`develop`**: set **`DEV_VERSION`** to exactly **`N+1`** at most once per **`.ver`** path (**`N`** = value on **`develop`**, or **`0`** if absent); **never** edit **`DEV_VERSION`** again after that (see **AI —** above), and **never increase** it mid-branch once established. **Maintainers** may use **`./scripts/bump_dev_version.sh`** outside the default AI workflow.
- **`GM`** — **binary `0` or `1` only** when present (go-to-main). **`GM=1`** is intended for **`preproduction A.B.C/`** rows; if several files in one preproduction directory are accidentally marked **`GM=1`**, **`./scripts/promote_preproduction_for_main.sh`** selects the highest **`DEV_VERSION`** row and rewrites lower **`DEV_VERSION`** GM rows to **`GM=0`**. **Cursor / AI:** **never** set **`GM=1`** without **explicit human maintainer** permission. Before **`main`**, a **maintainer** runs **`./scripts/promote_preproduction_for_main.sh`**, which writes one root GA **`.ver`** under **`version/entries/`** (basename, semver, optional **`RELEASE_DATE`**, and **`DESCRIPTION`** from the selected **`GM=1`** row only), **drops** **`PRERELEASE`**, **`GM`**, and **`DEV_VERSION`**, deletes **every** prerelease **`.ver`** in that folder, and **removes** the **`preproduction *`** directory from **`version/entries`** (and from **`version/locked`** only if a legacy copy exists). **`finalize_version_locked.sh`** does **not** copy **`preproduction */`** into **`locked`**; the new root **`.ver`** is what publish sees on the next finalize. **`main`** must have no **`preproduction *`** dirs and no **`PRERELEASE=1`**, **`GM=1`**, or **`DEV_VERSION=`** lines — CI **`check_version_main_prerelease_policy.sh`**.

On a **feature branch before merge**, you may freely add, edit, or remove **`.ver`** files under **`version/entries/`** that are **not** yet reflected in **`version/locked/`**.

**AI assistants:** When you begin the **first** substantive code change for a pull request, **add** or extend **`version/entries/*.ver`** so the work is documented. **Unique `(MAJOR, STANDARD, RELEASE, DEV_VERSION)`** across **`version/entries/**/*.ver`** (missing **`DEV_VERSION=`** counts as **0**): scan existing **`.ver`** files for that **A.B.C** and pick the next free **`DEV_VERSION`**; CI runs **`./scripts/check_version_entries_semver_dev_unique.sh`**. For **`PRERELEASE=1`**, **author new rows at the `version/entries/` root only** on same-repo branches—**do not** hand-add new **`*.ver`** paths under **`preproduction <A>.<B>.<C>/`** (Actions moves root rows into that directory); **do not** add new files under **`version/locked/`**; **omit `RELEASE_DATE=`** on new rows (Actions dates on relocate / Version lock). See **`version/entries/ABOUT.txt`** (AI authoring). Fork PRs follow **`docs/versioning.md`** (relocate locally, then commit). **Do not backtrack semver against an active preproduction version:** the **preproduction version** is **A.B.C** from **`version/entries/preproduction <A>.<B>.<C>/`** after relocate (**`PRERELEASE=1`** rows land there); do **not** add a **new root** **`version/entries/*.ver`** whose **A.B.C** is **numerically below** that **A.B.C** unless a **human maintainer** explicitly asks for that separate shipped release—prefer **another root `*.ver` file** with the same **`MAJOR`/`STANDARD`/`RELEASE`** and **`PRERELEASE=1`** for a new train, and use the **`N+1`**-once **`DEV_VERSION`** rule vs **`develop`** (see **AI —** above). For a **single GA feature** at **`version/entries/`** root without **`preproduction */`**, **one** **`.ver`** revised in place is often enough. **Do not** commit **`version/locked/**`** on agent-authored feature PRs. **Do not** commit **`userland/shell/version_def.h`**; let **CI** or **Version lock on merge** publish it (see **AI — `userland/shell/version_def.h`, `DEV_VERSION`, and `GM`** above).

### Lock system (agents, reviewers, and automation)

- **Never edit finalized release files.** Any path under **`version/locked/`** that already exists on the **merge base / target branch** is **immutable**: it is the published record. Do **not** rewrite it in place—add or adjust prose under **`version/entries/`**; publishing **`version/locked/`** is done by **Version lock on merge** (or by **maintainers** running finalize locally). CI enforces immutability on **`version/locked/`** for PRs (`scripts/check_version_locked_immutable.sh`), except **`version/locked/ABOUT.txt`**, which is companion documentation and may change with **`version/entries/ABOUT.txt`** in the same PR.
- **`version/locked/`** is the **published snapshot** produced from **`version/entries/`** by **`./scripts/finalize_version_locked.sh`** (alias: **`make finalize-version-locked`** / **`make sync-version-locked`**): same as entries **except** top-level **`preproduction */`** directories are **not** copied into **`locked`**. Until automation (or a maintainer) finalizes, **`version/entries/`** may contain extra drafts (including under **`preproduction */`**) not present in **`version/locked/`**.
- **CI** requires every file under **`version/locked/`** to exist under **`version/entries/`** with **identical content** (`scripts/check_version_locked_subset_of_entries.sh`)—so you cannot “advance” locked without aligning entries, and you cannot silently diverge a finalized file from the matching path under **`version/entries/`**.
- **AI assistants** must **not** propose edits to historical paths under **`version/locked/`** that shipped on the target branch, or to **`version/entries/`** files that must byte-match **`version/locked/`** for the same relative path, except when intentionally updating **`version/entries/ABOUT.txt`** to stay identical to the target’s **`version/locked/ABOUT.txt`** while **`version/locked/`** is unchanged.

CI verifies that the committed **`version_def.h`** matches **`scripts/gen_version_def.sh`** output for the repo’s **`version/locked/**`** and **`version/entries/**/*.ver`** trees (see **`docs/versioning.md`** for **`GM=1`** overrides and **`VERSION_LINE`** rules).

**Changelog binary:** **GitHub Actions** compiles **`scripts/gen_version_changelog.c`**, which reads **`version/locked` recursively** (headline version = highest finalized entry), then emits **`userland/shell/version_changelog.c`** (ignored by git). **`make … CHANGELOG_CI=1`** links **`VERSION_CHANGELOG[]`** in CI only. Plain **`make`** omits changelog unless you generate that file and pass **`CHANGELOG_CI=1`**. See **`scripts/templates/version_changelog.example.c`** for shape.

### Version lock on merge (GitHub Actions)

On every **`push`** to **`develop`**, workflow **`version-lock-on-merge.yml`** runs **`relocate_root_prerelease_ver_to_preproduction.sh`**, then **`finalize_version_locked.sh`** (copies **`version/entries/`** → **`version/locked/`**, **excluding** top-level **`preproduction */`** trees), **`stamp_version_release_date.sh`**, then **`gen_version_def.sh`**. If anything changed, it **opens a PR** into **`develop`** (via **`peter-evans/create-pull-request`**) instead of pushing directly, so branch protection and checks apply when someone merges that PR. Use **`GITHUB_TOKEN`** with the **Actions** settings above, **or** secret **`VERSION_LOCK_PAT`** (PAT with **contents** + **pull-requests** write). Workflow permissions: **`contents: write`** and **`pull-requests: write`**.

**Automation version PRs (review policy):** PRs opened **only** by **Version lock on merge** (title/message like **`chore(version): sync version/locked from entries after merge`**, branch `cursor/version-lock-from-entries-*`) are **routine housekeeping**. **Do not** spend automatic or proactive bot review on them unless a **human** explicitly requests reviewers or asks for a review—then review normally. On **mixed** feature PRs, **`userland/shell/version_def.h`** updates from **`github-actions[bot]`** / **`prepare-version-entries`** / **`chore(version):`** commits are **expected**—use **PR commit history** (see **Reviewers — `version_def.h`** above), not the net diff alone.

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
