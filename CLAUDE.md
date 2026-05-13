# CLAUDE.md — Project context for AI assistants

This repository implements **Bailey-Forbes-Flinstone**: a educational OS/shell-style codebase with a host shell (`BPForbes_Flinstone_Shell`), kernel-layer modules, drivers, and optional VM builds. Build with `make` from the repo root; see **AGENTS.md** for toolchain install and targets.

## Versioning (mandatory for merge-ready PRs)

### Lock system (AI assistants — mandatory)

- **Never edit finalized paths under `version/locked/`** that already exist on the branch you are merging **into** (merge base). That tree is the **published** record on **`develop`**.
- **Cursor / AI-authored feature PRs:** commit **`version/entries/*.ver`** (and keep **`version/entries/ABOUT.txt`** byte-identical to the merge target’s **`version/locked/ABOUT.txt`** while **`version/locked/`** is unchanged—CI `check_version_locked_subset_of_entries.sh`). **Do not** commit **`version/locked/**`** or **`userland/shell/version_def.h`** in those PRs: **Version lock on merge** (GitHub Actions after push to **`develop`**) runs **`finalize_version_locked.sh`**, stamps dates when applicable, **`gen_version_def.sh`**, and opens a PR to publish **`version/locked/`** and the header when needed. **Maintainers** may run finalize locally before merge if they want an early snapshot on the branch.
- **`version/entries/*.ver`** may be added, edited, or removed on your branch; CI allows **`version/entries/`** to contain drafts not yet copied to **`version/locked/`**.
- **Optional prerelease layout:** **`PRERELEASE=1`** rows live under **`version/entries/preproduction A.B.C/`** (that tree is **not** copied into **`version/locked/`** by **`finalize_version_locked.sh`**); bump **`DEV_VERSION`** with **`./scripts/bump_dev_version.sh`**. Set **`GM=1`** on exactly one file when the line is ready, then run **`./scripts/promote_preproduction_for_main.sh`** (or **`make promote-preproduction-for-main`**) so every **`*.ver`** in that folder is merged into one root GA row under **`version/entries/`** and the **`preproduction *`** directory is removed from **`version/entries`** (and from **`version/locked`** only if a legacy copy existed). Before any merge that lands **`version/**`** on **`main`**, ensure that promotion has already run — **`main`** must not ship **`preproduction *`**, **`PRERELEASE=1`**, **`GM=1`**, or **`DEV_VERSION=`** — CI fails otherwise.
- **`version/locked/`** is the **published snapshot** (automation copies **`version/entries/`** → **`version/locked/`**, **excluding** top-level **`preproduction */`**). Do not hand-tweak **`version/locked/`** on feature branches in agent workflows.
- **Do not** include historical **`version/locked/**`** or bulk-edit **`version/entries/**`** in automated refactors without an explicit release workflow.
- **`.ver` on first change:** When you start substantive code edits for a PR, **create** **`version/entries/… .ver`** immediately if the branch does not already have an entry for **this** PR’s work.
- **One entry per PR:** For a single pull request, **one** **`version/entries/*.ver`** file is enough—**update** that file as the branch evolves (description and semver only if scope truly changes), instead of adding multiple new **`.ver`** files for the same PR.

### Canonical string

- **`VERSION`** on **`develop`** comes from **`userland/shell/version_def.h`**, generated from **`version/locked/*.ver`** (highest **A.B.C**). Author **`version/entries/A_B_C_slug.ver`** (preferred; avoid optional **`NNN_`** serial filename prefixes unless matching legacy files). **GitHub Actions** runs **`gen_version_changelog.c`** against **`version/locked`**, then **`make CHANGELOG_CI=1`**. Plain **`git clone` + `make`** skips changelog unless you opt in with **`CHANGELOG_CI=1`**.
- Format is **semantic versioning**: **`A.B.C`** (not date-based).

### Component meanings

See **`docs/versioning.md`** for the full table mapping **`MAJOR_VERSION` / `STANDARD_VERSION` / `RELEASE_VERSION`** to **A / B / C**.

- **A (major)** — Milestones, architecture changes, large incompatible or foundational overhauls.
- **B (minor)** — New features (additive).
- **C (patch)** — Bug fixes and small fixes.

**Precedence (importance):** **`A` > `B` > `C`**. For each release, bump **only** the **highest** level that applies: **increment** that component; **keep** all more-significant components to the **left** **unchanged**; **set** all less-significant components to the **right** to **`0`**. Examples: **`2.2.4` → `2.3.0`** (minor: `C` resets), **`2.3.7` → `3.0.0`** (major), **`2.3.0` → `2.3.1`** (patch).

If work spans multiple categories (e.g. milestone + features + bugs), **increase only the highest applicable component once** (e.g. milestone-level change → bump **A** only → **`3.0.0`** after **`2.x.y`**).

### Branch comparison rule

When preparing or reviewing a merge **incoming → base** (e.g. `bug/…` → `develop`, `develop` → `main`):

1. Read **published** **`VERSION` on the target branch** from **`userland/shell/version_def.h`** (from **`version/locked/`** on the target).
2. On the **incoming** branch, ensure **`version/entries/*.ver`** includes a release whose internal semver is **strictly greater** than that target **VERSION** (or that incoming **`version_def.h`** is greater if the PR already published locked via a maintainer finalize).
3. If both show the **same** version (e.g. both `2.0.0`), **bump the incoming branch** so it is **one semver step ahead** of the target for the kind of change (example: same `2.0.0` on `bug/…` and `develop` → set incoming **entries** to **`2.0.1`** for a bugfix).

Implement the bump with **`version/entries/<A>_<B>_<C>_<slug>.ver`** (usually **one** file per feature PR—create it on first substantive change if missing, then **edit** it as the PR evolves). Pushing to **`develop`** triggers **Version lock on merge** (entries → **`version/locked/`**, regenerate **`version_def.h`**, opens a PR if updates are needed). For a local release build after that, use **`make deploy`** or the **Deploy** workflow.

**Automation version PRs:** PRs opened **only** by **Version lock on merge** (same **`chore(version): sync`** convention) need **no** automatic or proactive AI review. If a **human** requests a review, treat it like any other PR.

Export current numbers without compiling: **`./scripts/export_version_record.sh`**, **`./scripts/export_version_record.sh --json`**, or **`make version-record`** (`--json` for one-line JSON).

---

## Where else this is documented

- **AGENTS.md** — Cursor Cloud agents and versioning summary  
- **`docs/versioning.md`** — `.ver` authoring, `RELEASE_DATE` automation, **A / B / C** semantics  
- **`.coderabbit.yaml`** — optional CodeRabbit integration when enabled on the repo  
- **`.cursor/rules/versioning.mdc`** — Cursor IDE versioning rules  
- **`.cursor/rules/review_tools.mdc`** — optional tooling notes (no CLI push gate)

Keep these documents aligned when changing policy.
