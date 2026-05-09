# CLAUDE.md — Project context for AI assistants

This repository implements **Bailey-Forbes-Flinstone**: a educational OS/shell-style codebase with a host shell (`BPForbes_Flinstone_Shell`), kernel-layer modules, drivers, and optional VM builds. Build with `make` from the repo root; see **AGENTS.md** for toolchain install and targets.

---

## Versioning (mandatory for merge-ready PRs)

### Lock system (AI assistants — mandatory)

- **Never edit finalized paths under `version/locked/`** that already exist on the branch you are merging **into** (merge base). That tree is the **published** record; add or edit prose under **`version/entries/`**, then run **`./scripts/finalize_version_locked.sh`** (or **`make finalize-version-locked`**) and **`./scripts/gen_version_def.sh`** (or **`make`**) so **`userland/shell/version_def.h`** reflects **`version/locked/*.ver`**. Do not hand-edit **`VERSION_*`** macros in the header.
- **Draft work:** **`version/entries/*.ver`** may be added, edited, or removed on your branch until you **finalize** (copy entries → locked). CI guards **locked** paths that already shipped on the target.
- **`version/locked/`** is the **published snapshot** copied from **`version/entries/`** by **`finalize_version_locked.sh`**. Do not hand-tweak **`version/locked/`** to diverge from the last finalize without going through **`version/entries/`** first.
- **Do not** include historical **`version/locked/**`** or bulk-edit **`version/entries/**`** in automated refactors without an explicit release workflow.

### Canonical string

- **`VERSION`** comes from **`userland/shell/version_def.h`**, generated from **`version/locked/*.ver`** (highest **A.B.C**). Author new **`version/entries/<slug>.ver`** files, **finalize** to refresh **`version/locked/`**, then run **`make`** / **`gen_version_def.sh`**. **GitHub Actions** runs **`gen_version_changelog.c`** against **`version/locked`**, then **`make CHANGELOG_CI=1`**. Plain **`git clone` + `make`** skips changelog unless you opt in with **`CHANGELOG_CI=1`**.
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

Implement the bump by adding **`version/entries/<A>_<B>_<C>_<slug>.ver`**, running **`make finalize-version-locked`**, then **`./scripts/gen_version_def.sh`** or **`make`** so **`version_def.h`** updates, and relying on CI for changelog assembly—not required for **`git clone` + `make`**.

Export current numbers without compiling: **`./scripts/export_version_record.sh`**, **`./scripts/export_version_record.sh --json`**, or **`make version-record`** (`--json` for one-line JSON).

---

## Where else this is documented

- **AGENTS.md** — Cursor Cloud agents + versioning summary  
- **`.coderabbit.yaml`** — CodeRabbit review hints  
- **`.cursor/rules/versioning.mdc`** — Cursor IDE rules  

Keep these documents aligned when changing policy.
