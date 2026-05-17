# CLAUDE.md — Project context for AI assistants

This repository implements **Bailey-Forbes-Flinstone**: a educational OS/shell-style codebase with a host shell (`BPForbes_Flinstone_Shell`), kernel-layer modules, drivers, and optional VM builds. Build with `make` from the repo root; see **AGENTS.md** for toolchain install and targets.

## Module contracts (AI)

A **module contract** here means a **data-distribution contract**: a **frozen blueprint** for how data and outcomes **may** move across a boundary (ownership, lifetimes, error channels, and named **surfaces**). It **models** I/O between parts of the system; it does **not inherently add features**. **`contracts/foundations/*.h`** (P0) and **`contract_extend.h`** (prelude for future **Px** headers under **`contracts/extensions/`**) plus related **`fl/*`** headers carry the C-side contract bundle; **implementation** enforces contracts but is **not** the definition of the contract.

For a **P0–P9** row-by-row snapshot (**❌ / ⚠️ / ✅**) against **`develop`**, see **`docs/ROADMAP.md` → [Module contracts (abstraction and P0-P9 coverage)](#module-contracts-abstraction-and-p0-p9-coverage)**. Keep that table updated when contract coverage materially changes.

## Versioning (mandatory for merge-ready PRs)

### Lock system (AI assistants — mandatory)

- **Never edit finalized paths under `version/locked/`** that already exist on the branch you are merging **into** (merge base). That tree is the **published** record on **`develop`**.
- **`userland/shell/version_def.h`, `DEV_VERSION`, and `GM` (AI):** **Never** stage or commit **`userland/shell/version_def.h`** on same-repo feature PRs; **`make`** may refresh it locally—leave it unstaged or match the merge target. **`DEV_VERSION`:** compared to **`develop`**, you may set **`DEV_VERSION`** to exactly **`N+1`** once per **`.ver`** path (**`N`** = value on **`develop`**, or **`0`** if missing); **never** change **`DEV_VERSION`** again after that—**`DESCRIPTION`** only. **Never** set **`GM=1`** without **explicit human maintainer** permission.
- **Cursor / AI-authored feature PRs:** commit **`version/entries/*.ver`** (and keep **`version/entries/ABOUT.txt`** byte-identical to the merge target’s **`version/locked/ABOUT.txt`** while **`version/locked/`** is unchanged—CI `check_version_locked_subset_of_entries.sh`). **Do not** commit **`version/locked/**`** in those PRs: **Version lock on merge** (GitHub Actions after push to **`develop`**) runs **`relocate_root_prerelease_ver_to_preproduction.sh`**, **`finalize_version_locked.sh`**, stamps dates when applicable, **`gen_version_def.sh`**, and opens a PR to publish **`version/locked/`** and the header when needed. **CI** may push **`version/entries`** + **`userland/shell/version_def.h`** on same-repo pushes when needed. Fork PRs and offline checks follow **`docs/versioning.md`**. **Maintainers** may run finalize locally before merge if they want an early snapshot on the branch.
- **`version/entries/*.ver`** may be added, edited, or removed on your branch; CI allows **`version/entries/`** to contain drafts not yet copied to **`version/locked/`**.
- **Optional prerelease layout:** **`PRERELEASE=1`** rows are **authored at the `version/entries/` root** on same-repo branches (**do not** hand-add new **`*.ver`** under **`preproduction A.B.C/`**—CI runs **`relocate_root_prerelease_ver_to_preproduction.sh`** (stamps missing **`RELEASE_DATE`**, then moves into **`preproduction */`**) before layout checks). That tree is **not** copied into **`version/locked/`** by **`finalize_version_locked.sh`**. To record a new prerelease iteration **as a separate train**, add **another root `*.ver`** with the same semver and **`PRERELEASE=1`**, or follow maintainer policy for **`DEV_VERSION`** / **`./scripts/bump_dev_version.sh`**. **AI:** follow the **`N+1`**-once rule vs **`develop`** above; **never** set **`GM=1`**. A **maintainer** sets **`GM=1`** when appropriate and runs **`./scripts/promote_preproduction_for_main.sh`** (or **`make promote-preproduction-for-main`**) so every **`*.ver`** in that folder is merged into one root GA row under **`version/entries/`** and the **`preproduction *`** directory is removed from **`version/entries`** (and from **`version/locked`** only if a legacy copy existed). Before any merge that lands **`version/**`** on **`main`**, ensure that promotion has already run — **`main`** must not ship **`preproduction *`**, **`PRERELEASE=1`**, **`GM=1`**, or **`DEV_VERSION=`** — CI fails otherwise.
- **`version/locked/`** is the **published snapshot** (automation copies **`version/entries/`** → **`version/locked/`**, **excluding** top-level **`preproduction */`**). Do not hand-tweak **`version/locked/`** on feature branches in agent workflows.
- **Do not** include historical **`version/locked/**`** or bulk-edit **`version/entries/**`** in automated refactors without an explicit release workflow.
- **`.ver` on first change:** When you start substantive code edits for a PR, **create** a **new** **`version/entries/A_B_C_slug.ver`** at the **`version/entries/`** root for **that** PR (one new file per PR is the default—**do not** revise **`DEV_VERSION` / `DESCRIPTION`** on **`.ver`** already under **`version/entries/preproduction */`** from another train). If the branch does not yet have an entry for **this** PR’s work, add the new root row immediately (**do not** hand-add new files under **`version/entries/preproduction */`**—Actions relocates root **`PRERELEASE=1`** rows there).
- **No semver backtracking (AI):** The **preproduction version** is **A.B.C** from **`version/entries/preproduction <A>.<B>.<C>/`** directory names (**`PRERELEASE=1`** rows land there **after** relocate). Do **not** add a **new root** **`version/entries/*.ver`** whose **A.B.C** is **numerically below** that **A.B.C** unless a **human maintainer** explicitly asks. Prefer **another root `*.ver` file** with the same semver and **`PRERELEASE=1`** for a new PR train; for **`DEV_VERSION`** on a **`.ver`** your PR owns, follow the **`N+1`**-once rule vs **`develop`** (see bullets above). For routine **GA** work at **`version/entries/`** root, **one** **`.ver`** updated in place is often enough.

### Canonical string

- **`VERSION`** on **`develop`** comes from **`userland/shell/version_def.h`**, generated from **`version/locked/*.ver`** (highest **A.B.C**). The same header defines **`VERSION_LINE`** (shell display) from **`version/entries/**/*.ver`** when **`PRERELEASE=1`** rows exist. Author **`version/entries/A_B_C_slug.ver`** (preferred; avoid optional **`NNN_`** serial filename prefixes unless matching legacy files). **GitHub Actions** runs **`gen_version_changelog.c`** against **`version/locked`**, then **`make CHANGELOG_CI=1`**. Plain **`git clone` + `make`** skips changelog unless you opt in with **`CHANGELOG_CI=1`**.
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
- **`docs/ROADMAP.md`** — phased **P0–P9** work; **module contracts** abstraction + **❌/⚠️/✅** snapshot: [Module contracts (abstraction and P0-P9 coverage)](./docs/ROADMAP.md#module-contracts-abstraction-and-p0-p9-coverage)  
- **`docs/versioning.md`** — `.ver` authoring, `RELEASE_DATE` automation, **A / B / C** semantics  
- **`.coderabbit.yaml`** — optional CodeRabbit integration when enabled on the repo  
- **`.cursor/rules/versioning.mdc`** — Cursor IDE versioning rules  
- **`.cursor/rules/review_tools.mdc`** — optional tooling notes (no CLI push gate)

Keep these documents aligned when changing policy.
