# CLAUDE.md — Project context for AI assistants

This repository implements **Bailey-Forbes-Flinstone**: a educational OS/shell-style codebase with a host shell (`BPForbes_Flinstone_Shell`), kernel-layer modules, drivers, and optional VM builds. Build with `make` from the repo root; see **AGENTS.md** for toolchain install and targets.

---

## Versioning (mandatory for merge-ready PRs)

### Canonical string

- **`VERSION`** is built from integer macros in `userland/shell/version_def.h` (`VERSION_MAJOR`, `VERSION_STANDARD`, `VERSION_PATCH` → **A.B.C**). **GitHub Actions** assembles **`userland/shell/version_changelog.c`** by compiling and running **`scripts/gen_version_changelog.cpp`**, then builds with **`CHANGELOG_CI=1`** (see `.github/workflows/c-cpp.yml`). Plain **`git clone` + `make`** does not compile changelog unless you generate that file and opt in with **`CHANGELOG_CI=1`**.
- Format is **semantic versioning**: **`A.B.C`** (not date-based).

### Component meanings

- **A (major)** — Milestones, architecture changes, large incompatible or foundational overhauls.
- **B (minor)** — New features (additive).
- **C (patch)** — Bug fixes and small fixes.

If work spans multiple categories (e.g. milestone + features + bugs), **increase only the highest applicable component** for that release (e.g. milestone-level change → bump **A** only).

### Branch comparison rule

When preparing or reviewing a merge **incoming → base** (e.g. `bug/…` → `develop`, `develop` → `main`):

1. Compare **`VERSION` on the incoming branch** with **`VERSION` on the target branch**.
2. **Incoming must be strictly greater** than the target for that merge.
3. If both branches report the **same** version (e.g. both `2.0.0`), **bump the incoming branch** so it is **one semver step ahead** of the target for the kind of change (example: same `2.0.0` on `bug/…` and `develop` → set incoming to **`2.0.1`** for a bugfix).

Implement the bump by editing **`VERSION_*` in `version_def.h`** on the **incoming** branch before merge. Add deployment changelog artifacts in CI/deploy pipelines—not required for **`git clone` + `make`**.

Export current numbers without compiling: **`./scripts/export_version_record.sh`**, **`./scripts/export_version_record.sh --json`**, or **`make version-record`** (`--json` for one-line JSON).

---

## Where else this is documented

- **AGENTS.md** — Cursor Cloud agents + versioning summary  
- **`.coderabbit.yaml`** — CodeRabbit review hints  
- **`.cursor/rules/versioning.mdc`** — Cursor IDE rules  

Keep these documents aligned when changing policy.
