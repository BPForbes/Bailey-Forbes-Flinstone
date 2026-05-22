# Replit — Bailey-Forbes-Flinstone

Run this repo on [Replit](https://replit.com/~) (import from GitHub or fork).

## Replit Agent policy (required — explicit rules)

> **INDICATOR — REPLIT AGENT:** Any **Replit Agent** (Replit AI, Ghostwriter, or other in-Repl coding assistants) **is bound to the same mandatory rules** as **Cursor** (`.cursor/rules/`, **AGENTS.md**), **Claude** (**CLAUDE.md**), and **CodeRabbit** (**.coderabbit.yaml**). These rules are **not optional** and **not a lighter Replit-only policy**. The sections below state them **explicitly** for Replit; when maintaining policy, keep this file aligned with those sources.

**Canonical copies (update together when policy changes):** [`CLAUDE.md`](CLAUDE.md) · [`AGENTS.md`](AGENTS.md) · [`.cursor/rules/versioning.mdc`](.cursor/rules/versioning.mdc) · [`.cursor/rules/review_tools.mdc`](.cursor/rules/review_tools.mdc) · [`.coderabbit.yaml`](.coderabbit.yaml) · [`docs/versioning.md`](docs/versioning.md)

---

### 1. Project context

- This repo is **Bailey-Forbes-Flinstone**: educational OS/shell-style C/C++ with **`BPForbes_Flinstone_Shell`**, kernel modules, drivers, optional VM builds. Build from repo root with **`make`**.
- A **module contract** is a **normative data-distribution model** across a boundary (ownership, lifetimes, errors, named surfaces)—**not** product features by themselves. Contract headers live under **`contracts/foundations/`** and related **`fl/*`**; **implementation enforces** but does not replace written contracts. See **`docs/ROADMAP.md`** (module contracts table) when changing contract coverage.

---

### 2. Code change rules (all agents)

- **Minimize scope:** smallest correct diff; no unrelated refactors or drive-by edits.
- **Match existing style:** naming, types, patterns, and documentation level of surrounding code.
- **Comments:** aim for roughly **15% comments / 85% code** in new or heavily touched areas—explain non-obvious intent and invariants, not line-by-line narration.
- **Tests:** before changing code, run the **most relevant** existing tests for that area; after each change, rerun until the affected behavior passes. Add or update tests for new behavior, bug fixes, and regressions when a suitable target exists (`make test_drivers`, `test_core`, `test_alloc_asm`, etc.).
- **Do not** bulk-edit **`version/locked/**`** or historical **`version/entries/**`** paths from the merge base in automated refactors.

---

### 3. Versioning and release files (mandatory)

Format is semantic **`A.B.C`** (not date-based). **`userland/shell/version_def.h`** is **generated** by **`scripts/gen_version_def.sh`** / **`make`** from **`version/locked/`** and **`version/entries/**/*.ver`** (with **`PRERELEASE=1`** / **`GM=1`** exceptions—see **`docs/versioning.md`**).

#### 3.1 Semver components and bumps

| Component | Field in `.ver` | Meaning |
|-----------|-----------------|--------|
| **A** | `MAJOR_VERSION` | Major milestones, architecture, large incompatible/foundational changes |
| **B** | `STANDARD_VERSION` | New additive features |
| **C** | `RELEASE_VERSION` | Bug fixes and small corrections |

**Precedence:** **`A` > `B` > `C`**. For each release, bump **only the highest** level that applies to the **whole** release:

- **Increment** that component only.
- **Leave unchanged** all components to the **left** (more significant).
- **Set to `0`** all components to the **right** (less significant).

Examples: **`2.2.4` → `2.3.0`** (minor); **`2.3.7` → `3.0.0`** (major); **`2.3.0` → `2.3.1`** (patch). If work mixes categories, **one bump** at the highest level only (e.g. milestone + features → bump **A** only → **`3.0.0`**).

#### 3.2 Lock system — never rewrite published history

- **`version/locked/`** on the **merge target** is the **published** record. **Do not modify** any path under **`version/locked/`** that already exists on the branch you merge **into**.
- **`version/entries/`** is where you **author** release prose. It may contain drafts (including **`preproduction */`**) not yet in **`locked`** until automation runs.
- **Do not commit `version/locked/**`** on normal agent feature PRs. After merge to **`develop`**, **Version lock on merge** (GitHub Actions) relocates root **`PRERELEASE=1`** rows, finalizes **`version/locked/`**, regenerates **`version_def.h`**, and may open a housekeeping PR.
- **`version/entries/ABOUT.txt`** must **byte-match** the merge target’s **`version/locked/ABOUT.txt`** while **`version/locked/`** on the target is unchanged.

#### 3.3 Immutable `.ver` rows — always add new files

- Any **`version/entries/**/*.ver`** path on the **merge base** is **immutable**: **do not** edit **`DESCRIPTION`**, **`DEV_VERSION`**, **`GM`**, **`PRERELEASE`**, or semver fields in those files.
- Each PR / iteration: **add exactly one new** **`version/entries/A_B_C_short_slug.ver`** at the **`version/entries/` root** (prefer **`A_B_C_slug.ver`**; avoid legacy **`NNN_`** filename prefixes unless matching existing files).
- The **new** `.ver` your branch introduces may change **`DESCRIPTION` only** until merge (no **`DEV_VERSION`** re-bump). After merge to **`develop`**, that path becomes immutable too.
- **`.ver` files that exist only on your feature branch** (not on merge base) may be edited freely until merge.

#### 3.4 Where to author `.ver` files (hard rule)

- **Author new PR release rows only as** **`version/entries/*.ver`** at the **`version/entries/`** directory itself—**not** under **`version/entries/preproduction <A>.<B>.<C>/`** by hand on same-repo branches.
- For **`PRERELEASE=1`**: create a **new root** **`*.ver`** with **`PRERELEASE=1`**. CI **`relocate_root_prerelease_ver_to_preproduction.sh`** moves it into **`preproduction <A>.<B>.<C>/`** and may stamp **`RELEASE_DATE`**. **Do not** hand-add new **`PRERELEASE=1`** paths under **`preproduction */`** to “save a step.”
- **Omit `RELEASE_DATE=`** on new root rows unless a maintainer workflow says otherwise; let CI stamp on relocate / Version lock.

#### 3.5 `DEV_VERSION`, `PRERELEASE`, `GM`

- **`PRERELEASE`** and **`GM`**: when present, values must be exactly **`0`** or **`1`**.
- **`DEV_VERSION`:** compare to **`develop`** (merge base). Let **`N`** = **`DEV_VERSION`** on **`develop`** for that path, or **`0`** if absent. You may set **`DEV_VERSION`** to exactly **`N+1`** **once** when establishing that row; **never** change **`DEV_VERSION`** again on that path—update **`DESCRIPTION`** only. **Never** increase mid-PR (e.g. **8→9**). **Do not** run **`./scripts/bump_dev_version.sh`** to chase BUILD numbers.
- With **`PRERELEASE=1`**, use **`DEV_VERSION >= 1`**.
- **`(MAJOR, STANDARD, RELEASE, DEV_VERSION)`** must be **unique** across **`version/entries/**/*.ver`** (missing **`DEV_VERSION=`** counts as **0**). Before push: **`./scripts/check_version_entries_semver_dev_unique.sh`**.
- **`GM=1`:** **Replit Agent must never set `GM=1`** unless a **human maintainer** explicitly instructs. **`GM=1`** belongs on a **new** row under **`version/entries/preproduction <A>.<B>.<C>/`** (next **`DEV_VERSION`**), **never** on a root **`*.ver`**. **`DESCRIPTION`** must start with **`A.B.C:`** and summarize the overall release. Maintainers run **`promote_preproduction_for_main.sh`** before **`main`**.

#### 3.6 No semver backtracking (AI)

- Active preproduction version is **A.B.C** from **`version/entries/preproduction <A>.<B>.<C>/`** (after relocate).
- **Do not** add a **new root** **`.ver`** whose **A.B.C** is **numerically below** that preproduction **A.B.C** unless a **human maintainer** explicitly requests a separate GA release.
- Default for a new train: **another root** **`PRERELEASE=1`** **`.ver`** with the same semver and the next free **`DEV_VERSION`**.

#### 3.7 `userland/shell/version_def.h` (generated — do not commit on feature PRs)

- **Never** stage or **commit** **`userland/shell/version_def.h`** on same-repo agent feature PRs. **`make`** may rewrite it locally—**do not** **`git add`** it; reset to merge target if needed.
- **Do not** hand-edit **`VERSION_MAJOR`**, **`VERSION_STANDARD`**, **`VERSION_PATCH`**, or **`VERSION_LINE`**.
- **Do not** run **`./scripts/gen_version_def.sh`** just to “publish” header updates after **`GM=1`** on **`.ver`**—GitHub Actions (**`.github/workflows/c-cpp.yml`**, **Version lock on merge**) owns that.
- Local verify only: **`./scripts/gen_version_def.sh`** or **`make`** then leave unstaged; or **`./scripts/export_version_record.sh --json`**.

#### 3.8 First substantive change on a PR

- On the **first** substantive code change for a pull request, **add** a **new** **`version/entries/A_B_C_slug.ver`** at the **entries root** if the branch has no entry covering **that** PR’s work.

#### 3.9 Merge version checks (incoming → base)

Before merging (e.g. **`bug/*` → `develop`**, **`develop` → `main`**):

1. Read target **`VERSION`** from **`userland/shell/version_def.h`** (from **`version/locked/`** on the target, unless **`PRERELEASE=1`** + **`GM=1`** in entries overrides per **`gen_version_def.sh`**).
2. Incoming **`version/entries/*.ver`** must record a semver **strictly greater** than target **VERSION** when the PR ships a new release (unless maintainer already finalized **`version/locked/`** on the branch).
3. If both are the **same** (e.g. both **`2.0.0`**), bump incoming **entries** one appropriate step (example: bugfix → **`2.0.1`**).

**Automation-only version PRs** (`chore(version): sync…`, **`cursor/version-lock-from-entries-*`**): routine housekeeping—no proactive deep review unless a human asks.

---

### 4. Git and branch workflow (Replit Agent)

- Stay on the **current feature branch** for related work. **Do not** open a new branch for extra scope **unless** a human explicitly allows it.
- When combining agent branches (**`cursor/*`**, **`claude/*`**, **`codex/*`**, Replit, etc.) into one PR:
  - Use **`git merge --no-ff <other-branch>`** (visible combine point—**not** silent fast-forward).
  - **Delete** duplicate **`.ver`** for the same **A.B.C** train from the merged-in branch.
  - **Do not edit** immutable merge-base **`.ver`** rows—fold prose into the **newest** **`.ver`** the combined PR adds, or add **one new** root **`.ver`** with the next **`DEV_VERSION`**.
  - Re-run **`./scripts/check_version_entries_semver_dev_unique.sh`**.

---

### 5. Build, test, and toolchain (Replit)

- Install/use packages from **`replit.nix`** (or **`nix/deps.json`** manifest). Equivalent apt list is in **`AGENTS.md`** / **`docs/dependencies.md`**.
- **Default:** `make` → **`BPForbes_Flinstone_Shell`**
- **Other targets:** `make clean`, `make ARCH=x86_64_nasm`, `make ARCH=arm` (cross; may need extra deps), `make USE_ASM_ALLOC=1`, `make test_alloc_asm`, `make test_drivers`, `make test_core`, `make vm`, `make vm-sdl` (SDL may need display tier), `make BPForbes_Flinstone_Tests`, `make deps` for in-tree SDL2/CUnit.
- **Policy:** run relevant tests for the area you change; do not skip because “Replit”—document real blockers with the failing command.
- **Implementation boundaries:** low-level memory, sync, port I/O, and hardware-facing paths use the **ASM layer**; keep C for orchestration, drivers, VM, filesystem policy. Prefer extending existing ASM-backed primitives over parallel libc-only paths for kernel/driver/baremetal.

---

### 6. Pull request and review alignment (CodeRabbit / human review)

- Incoming version must **exceed** target when the change warrants a release bump.
- **Do not** approve or produce edits to **`version/locked/**`** on the merge base.
- **Do not** commit **`userland/shell/version_def.h`** on agent feature PRs (CI/GitHub Actions may commit it on automation flows—that is not agent-authored feature work).
- Optional **CodeRabbit** / **Codex** CLI is **not** required before **`git push`**; ordinary GitHub PR review applies (**`.cursor/rules/review_tools.mdc`**).
- When changing versioning policy text, keep **`replit.md`**, **`CLAUDE.md`**, **`AGENTS.md`**, **`.cursor/rules/versioning.mdc`**, **`.coderabbit.yaml`**, and **`docs/versioning.md`** aligned.

---

### 7. Conflict resolution

If this **`replit.md`** explicit section and **`CLAUDE.md`**, **`AGENTS.md`**, **`.cursor/rules/*.mdc`**, or **`.coderabbit.yaml`** disagree, **follow those four sources** and open a follow-up change to fix **`replit.md`**.

## Required files

- `.replit` — run/compile, Nix channel, modules
- `replit.nix` — system packages (gcc, make, nasm, sqlite, openssl, SDL2, CUnit, …)

## Quick start

1. Import the repository into a Replit App (GitHub import keeps `.replit` and `replit.nix`).
2. Wait for the Nix environment to sync (shell reload after `replit.nix` changes).
3. **Run** — builds via `compile`, then runs `./BPForbes_Flinstone_Shell help`.
4. In the **Shell** pane for an interactive session:

   ```bash
   make
   ./BPForbes_Flinstone_Shell
   ```

## Common make targets

| Command | Purpose |
|---------|---------|
| `make` | Default host shell |
| `make USE_ASM_ALLOC=1 test_alloc_asm` | ASM allocator tests |
| `make ARCH=x86_64_nasm` | NASM build |
| `make vm-sdl` | VM + SDL2 window (needs display; may not work on all Replit tiers) |
| `make BPForbes_Flinstone_Tests` | CUnit tests |

## Optional: in-repo deps

If Nix SDL2/CUnit versions are insufficient:

```bash
make deps
make vm-sdl
```

## Manifest

Package list and profiles: `nix/deps.json`. Local Nix flakes: `flake.nix` (not used by Replit directly).

## Versioning (quick reminder)

Full rules are in **§3** above. Replit Agent: **entries only** on feature PRs; **never** merge-base **`.ver`** edits; **never** **`version/locked/**`** or **`version_def.h`** commits; **one new** root **`version/entries/A_B_C_slug.ver`** per PR train when needed.
