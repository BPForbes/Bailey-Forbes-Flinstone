# Replit — Bailey-Forbes-Flinstone

Run this repo on [Replit](https://replit.com/~) (import from GitHub or fork).

## Replit Agent policy (required — explicit rules)

> **INDICATOR — REPLIT AGENT:** Any **Replit Agent** (Replit AI, Ghostwriter, or other in-Repl coding assistants) **is bound to the same mandatory rules** as **Cursor** (`.cursor/rules/`, **AGENTS.md**), **Claude** (**CLAUDE.md**), and **CodeRabbit** (**.coderabbit.yaml**). These rules are **not optional** and **not a lighter Replit-only policy**. The sections below state them **explicitly** for Replit; when maintaining policy, keep this file aligned with those sources.

**Canonical copies (update together when policy changes):** [`CLAUDE.md`](CLAUDE.md) · [`AGENTS.md`](AGENTS.md) · [`.cursor/rules/versioning.mdc`](.cursor/rules/versioning.mdc) · [`.cursor/rules/review_tools.mdc`](.cursor/rules/review_tools.mdc) · [`.coderabbit.yaml`](.coderabbit.yaml) · [`docs/versioning.md`](docs/versioning.md)

> **`GM=1` (go-to-main):** Replit Agent **must not** set **`GM=1`** on any **`.ver`** file for **any** prerelease version (**`A.B.C`**) **unless** a **human maintainer** explicitly tells you that you may. Without that instruction, **never** add **`GM=1`** on any train—use **`GM=0`** or omit **`GM`**. Go-to-main is a **maintainer** decision, not an agent default on feature work.

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
- **`version/entries/`** is where you **author** new release rows (at the **entries root** only—see **§3.4**). After CI, **`preproduction <A>.<B>.<C>/`** trees may appear under **`version/entries/`**; they are **not** where Replit Agent hand-writes new PR rows.
- **Do not commit `version/locked/**`** on normal agent feature PRs. After merge to **`develop`**, **Version lock on merge** (GitHub Actions) relocates root **`PRERELEASE=1`** rows, finalizes **`version/locked/`**, regenerates **`version_def.h`**, and may open a housekeeping PR.
- **`version/entries/ABOUT.txt`** must **byte-match** the merge target’s **`version/locked/ABOUT.txt`** while **`version/locked/`** on the target is unchanged.

#### 3.3 Immutable `.ver` rows — add new files; do not rewrite history

- Any **`version/entries/**/*.ver`** path on the **merge base** is **immutable**: **do not** edit **`DESCRIPTION`**, **`DEV_VERSION`**, **`GM`**, **`PRERELEASE`**, or semver fields in those files (including rows already under **`preproduction */`** on the merge base).
- Each PR / iteration: **typically add one new** **`version/entries/A_B_C_short_slug.ver`** at the **`version/entries/` root** (prefer **`A_B_C_slug.ver`**; avoid legacy **`NNN_`** filename prefixes unless matching existing files). **Exception:** after **`git merge --no-ff`** of another agent branch, or when a new develop iteration needs its own train row, you may add **more than one** root **`.ver`**—**delete** duplicate **`.ver`** for the same **A.B.C** train from the merged-in branch, **do not edit** immutable merge-base rows, and fold combined prose into the **newest** root **`.ver`** this combined PR adds (or add **one new** root **`.ver`** with the next free **`DEV_VERSION`**).
- Each **new** root **`.ver` this PR adds** (authored at **`version/entries/`**, even after CI moves it under **`preproduction */`**): **`DESCRIPTION` only** until merge to **`develop`** (no **`DEV_VERSION`** re-bump). After merge, that path is immutable.
- **Do not** edit other **`.ver`** files on your branch—especially under **`preproduction */`** from an earlier train, another PR, or merge-base history. “Only on my branch” is **not** permission to rewrite semver fields or unrelated rows.

#### 3.4 Replit Agent — where to author `.ver` files (same-repo PRs)

| Action | Replit Agent |
|--------|----------------|
| New PR / train row | **Add** **`version/entries/A_B_C_slug.ver`** at the **`version/entries/` directory root only |
| **`PRERELEASE=1`** | Set on a **new root** **`*.ver`**; CI **`relocate_root_prerelease_ver_to_preproduction.sh`** moves it into **`preproduction <A>.<B>.<C>/`** |
| Hand-add under **`preproduction */`** | **Never** (no new **`*.ver`**, no **`PRERELEASE=1`**, no **`GM=1`**) |
| Edit a row already under **`preproduction */`** | **Only** the **one** **`.ver`** this PR created at the entries root (after CI relocate): **`DESCRIPTION` only**. **Never** edit other trains’ rows there; for a new train, add another **root** **`.ver`** |
| **`RELEASE_DATE=`** on new rows | **Omit**; CI stamps on relocate / Version lock |

#### 3.5 `DEV_VERSION`, `PRERELEASE`, and `GM`

- **`PRERELEASE`** and **`GM`**: when present, values must be exactly **`0`** or **`1`**.
- **`DEV_VERSION`:** compare to **`develop`** (merge base). Let **`N`** = **`DEV_VERSION`** on **`develop`** for that path, or **`0`** if absent. You may set **`DEV_VERSION`** to exactly **`N+1`** **once** when establishing that row; **never** change **`DEV_VERSION`** again on that path—update **`DESCRIPTION`** only. **Never** increase mid-PR (e.g. **8→9**). **Do not** run **`./scripts/bump_dev_version.sh`** to chase BUILD numbers.
- With **`PRERELEASE=1`**, use **`DEV_VERSION >= 1`**.
- **`(MAJOR, STANDARD, RELEASE, DEV_VERSION)`** must be **unique** across **`version/entries/**/*.ver`** (missing **`DEV_VERSION=`** counts as **0**). Before push: **`./scripts/check_version_entries_semver_dev_unique.sh`**.
- **`GM=1` (go-to-main):** **Replit Agent must not set `GM=1` unless a human maintainer explicitly instructs you that you may** (written go-ahead in the task or PR—do not infer it from “ready to merge” or “ship to main”). Without that instruction, **never** set **`GM=1`** on any **`.ver`** row for any **`A.B.C`** train (including under **`preproduction */`**); keep **`GM=0`** or omit **`GM`**. **Never** hand-add rows under **`version/entries/preproduction */`**. When a **maintainer** runs go-to-main, they add a **new** **`.ver`** under the existing **`preproduction <A>.<B>.<C>/`** folder (next **`DEV_VERSION`**, **`GM=1`**, **`DESCRIPTION`** starting with **`A.B.C:`**)—**never** on a root **`*.ver`**. **`promote_preproduction_for_main.sh`** then writes one GA root **`.ver`** at **`version/entries/`** before **`main`**.

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

Full rules are in **§3** above. Replit Agent: **typically one new** root **`version/entries/A_B_C_slug.ver`** per PR train (extra root rows only when combining branches or a new iteration requires it—see **§3.3**); **`PRERELEASE=1` at root only** (CI moves to **`preproduction */`**); **never** hand-add under **`preproduction */`**; **`GM=1` only if a human maintainer explicitly allows it** (never by default on any version); **never** merge-base **`.ver`** edits; **`DESCRIPTION` only** on each root row this PR added (even after relocate); **never** **`version/locked/**`** or **`version_def.h`** commits.
