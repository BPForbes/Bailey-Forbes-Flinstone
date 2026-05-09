# CLAUDE.md — Project context for AI assistants

This repository implements **Bailey-Forbes-Flinstone**: a educational OS/shell-style codebase with a host shell (`BPForbes_Flinstone_Shell`), kernel-layer modules, drivers, and optional VM builds. Build with `make` from the repo root; see **AGENTS.md** for toolchain install and targets.

---

## Versioning (mandatory for merge-ready PRs)

### Lock system (AI assistants — mandatory)

- **Never edit older version files.** Do **not** change **`version/entries/*.ver`** files that already exist on the branch you are merging **into** (the merge base). They are the frozen release record; add **`version/entries/<A>_<B>_<C>_<slug>.ver`**, run **`./scripts/gen_version_def.sh`** (or **`make`**) to refresh **`userland/shell/version_def.h`**, and commit the regenerated header instead of editing **`VERSION_*` macros by hand.
- **Draft entries:** `.ver` files that exist only on your branch (not on the merge base) are **not** immutable until merge—you can edit or drop them while iterating; CI only guards entries already on the target.
- **`version/locked/`** mirrors **`version/entries/`** for visibility only. Refresh with **`./scripts/sync_version_locked_mirror.sh`**; do **not** edit **`version/locked/`** to “fix” content without syncing from **`version/entries/`**.
- **Do not** include historical **`.ver`** or **`version/locked/`** paths in automated refactors, formatting-only sweeps, or bulk renames.

### Canonical string

- **`VERSION`** is built from **`userland/shell/version_def.h`** (generated from **`version/entries/*.ver`**). Each bump adds a **new** file under **`version/entries/`** (`.ver` format; see **AGENTS.md**), run **`./scripts/gen_version_def.sh`** or **`make`**, then **`./scripts/sync_version_locked_mirror.sh`** so **`version/locked/`** stays in sync. **GitHub Actions** assembles **`userland/shell/version_changelog.c`** by compiling **`scripts/gen_version_changelog.c`** and running it against **`version/entries`**, then builds with **`CHANGELOG_CI=1`** (see `.github/workflows/c-cpp.yml`). Plain **`git clone` + `make`** does not compile changelog unless you generate that file and opt in with **`CHANGELOG_CI=1`**.
- Format is **semantic versioning**: **`A.B.C`** (not date-based).

### Component meanings

- **A (major)** — Milestones, architecture changes, large incompatible or foundational overhauls.
- **B (minor)** — New features (additive).
- **C (patch)** — Bug fixes and small fixes.

**Precedence (importance):** **`A` > `B` > `C`**. For each release, bump **only** the **highest** level that applies: **increment** that component; **keep** all more-significant components to the **left** **unchanged**; **set** all less-significant components to the **right** to **`0`**. Examples: **`2.2.4` → `2.3.0`** (minor: `C` resets), **`2.3.7` → `3.0.0`** (major), **`2.3.0` → `2.3.1`** (patch).

If work spans multiple categories (e.g. milestone + features + bugs), **increase only the highest applicable component once** (e.g. milestone-level change → bump **A** only → **`3.0.0`** after **`2.x.y`**).

### Branch comparison rule

When preparing or reviewing a merge **incoming → base** (e.g. `bug/…` → `develop`, `develop` → `main`):

1. Compare **`VERSION` on the incoming branch** with **`VERSION` on the target branch**.
2. **Incoming must be strictly greater** than the target for that merge.
3. If both branches report the **same** version (e.g. both `2.0.0`), **bump the incoming branch** so it is **one semver step ahead** of the target for the kind of change (example: same `2.0.0` on `bug/…` and `develop` → set incoming to **`2.0.1`** for a bugfix).

Implement the bump by adding **`version/entries/<A>_<B>_<C>_<slug>.ver`**, running **`./scripts/gen_version_def.sh`** or **`make`** so **`userland/shell/version_def.h`** updates, mirroring to **`version/locked/`**, and relying on CI for changelog assembly—not required for **`git clone` + `make`**.

Export current numbers without compiling: **`./scripts/export_version_record.sh`**, **`./scripts/export_version_record.sh --json`**, or **`make version-record`** (`--json` for one-line JSON).

---

## Where else this is documented

- **AGENTS.md** — Cursor Cloud agents + versioning summary  
- **`.coderabbit.yaml`** — CodeRabbit review hints  
- **`.cursor/rules/versioning.mdc`** — Cursor IDE rules  

Keep these documents aligned when changing policy.
