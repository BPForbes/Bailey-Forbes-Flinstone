# Versioning and `.ver` release files

This document describes how **Flinstone** records shipped versions, how **`.ver`** files are authored, and how automation updates **`version/locked/`** and **`userland/shell/version_def.h`**.

## Semantic version `A.B.C`

The shell reports **`VERSION`** as **`A.B.C`**. The numeric fields in `.ver` files map as follows:

| Field in `.ver` | Typical macro name | Meaning (semver role) |
|-----------------|------------------|-------------------------|
| **`MAJOR_VERSION`** (alias `VERSION_MAJOR`) | **A** | Milestones and architecture-scale changes; breaking or foundational work. |
| **`STANDARD_VERSION`** (alias `VERSION_STANDARD`) | **B** | New features (additive / semver “minor”). |
| **`RELEASE_VERSION`** (aliases `MINOR_VERSION`, `VERSION_PATCH`) | **C** | Bug fixes and small corrections (semver “patch”). |

**Precedence:** **`A` > `B` > `C`**. For each release, bump **only** the single most significant component that applies to the release as a whole: increment that component, leave more significant (left) components unchanged, and set less significant (right) components to **`0`** (e.g. `2.2.4` → `2.3.0` for a minor bump, `2.3.7` → `3.0.0` for major, `2.3.0` → `2.3.1` for patch).

## Where to edit

| Path | Role |
|------|------|
| **`version/entries/*.ver`** | **Author here.** Add or revise release notes and semver fields for work on a feature branch. |
| **`version/locked/*.ver`** | **Published mirror.** Filled by **`finalize_version_locked.sh`** (copies **`version/entries/`** → **`version/locked/`**, **excluding** top-level **`preproduction A.B.C/`** directories so prerelease folders stay **entries-only** until **`promote_preproduction_for_main.sh`** emits a root GA **`.ver`**) run locally by maintainers or by **Version lock on merge** GitHub Actions after changes land on **`develop`**. **Do not hand-edit** locked `.ver` files for routine version bumps—edit **`version/entries/`** instead. |
| **`userland/shell/version_def.h`** | **Generated.** **`scripts/gen_version_def.sh`** (also **`make`**) sets **`VERSION_*`** / **`VERSION`** from the highest **`A.B.C`** among **`version/locked/**/*.ver`**, **unless** any **`version/entries/**/*.ver`** row has **`PRERELEASE=1`** and **`GM=1`**: then the **newest** such row (**highest semver, then highest `DEV_VERSION`**) supplies **`MAJOR`/`STANDARD`/`RELEASE`** for **`VERSION_*`** / **`VERSION`**, and **`VERSION_LINE`** is **plain `A.B.C`** (no **`PRERELEASE_TAG`**, no **`, BUILD n`**). Otherwise **`VERSION_LINE`** follows **`PRERELEASE=1`** rows (**`PRERELEASE_TAG`** + semver + optional **`, BUILD n`**) when any exist; if none, **`VERSION_LINE`** matches **`VERSION`**. **Never hand-edit** `VERSION_*` in this header. |

**Companion prose only:** **`version/entries/ABOUT.txt`** and **`version/locked/ABOUT.txt`** describe the `.ver` format. When CI requires them to match, update **both** in the same pull request with **identical** bytes. They are not release semver files; they are documentation.

## Creating and handling `.ver` files

**Cursor / AI — path rule:** New PR **`.ver`** files must be added **only** as **`version/entries/<A>_<B>_<C>_<short_slug>.ver`** (the parent directory is **`version/entries/`** itself). **Do not** commit them first under **`version/entries/preproduction <A>.<B>.<C>/`**; **`relocate_root_prerelease_ver_to_preproduction.sh`** and the **GitHub Actions** versioning / **Version lock** steps **move** root **`PRERELEASE=1`** rows into **`preproduction */`** after optional **`RELEASE_DATE`** stamping.

**One new root `.ver` per PR (AI):** Record each PR's work in **its own** new **`version/entries/A_B_C_slug.ver`** at the entries root. **Do not** append unrelated prose or bump **`DEV_VERSION`** on **`.ver`** files already sitting under **`version/entries/preproduction */`** from another train—those rows are **that** train's relocated record; open a **new** root **`*.ver`** for your PR instead.

**`userland/shell/version_def.h`, `DEV_VERSION`, and `GM` (AI):** **Never** stage or commit **`userland/shell/version_def.h`** on same-repo feature PRs (**`make`** may refresh it locally—leave it unstaged or reset to the merge target). **Do not** hand-edit **`VERSION_MAJOR`**, **`VERSION_STANDARD`**, **`VERSION_PATCH`**, or **`VERSION_LINE`**, and **do not** run **`./scripts/gen_version_def.sh`** only to update the header after a maintainer sets **`GM=1`** on **`.ver`**—**GitHub Actions** (`.github/workflows/c-cpp.yml` **version-requirements** job, then optional bot push; **Version lock on merge** on **`develop`**) run **`gen_version_def.sh`** and publish **`VERSION_*`** / **`VERSION_LINE`** (including plain **`A.B.C`** when **`GM=1`** applies). **`DEV_VERSION`:** for each **`.ver`** your PR adds or updates, compare to **`develop`**: let **`N`** be **`DEV_VERSION`** on **`develop`** for the same relative path, or **`0`** if that file is absent there. You may set **`DEV_VERSION`** to exactly **`N+1`** at most once; **do not** change **`DEV_VERSION`** again after that—revise **`DESCRIPTION`** only. **Never increase** **`DEV_VERSION`** again after it is established on the branch (for example **8→9** mid-PR), including via **`bump_dev_version.sh`**. **Never** set **`GM=1`** (even under **`preproduction */`**) unless a **human maintainer** explicitly instructs you.

1. On the **first** substantive code or docs change for a pull request, add **`version/entries/<A>_<B>_<C>_<short_slug>.ver`** at the **`version/entries/`** root if nothing yet covers that PR. For **`PRERELEASE=1`**, **do not** hand-add new **`.ver`** paths under **`preproduction <A>.<B>.<C>/`** on same-repo branches—author at the root; CI **`relocate_root_prerelease_ver_to_preproduction.sh`** moves them into that directory. **AI/automation:** the **preproduction version** is **A.B.C** read from **`version/entries/preproduction <A>.<B>.<C>/`** directory names (**`PRERELEASE=1`** rows land there **after** relocate)—do **not** add a **new root** **`.ver`** at **`version/entries/`** whose **A.B.C** is **numerically below** that **A.B.C** unless a **maintainer explicitly** requests that separate GA release. Prefer **additional root `*.ver` files** with the same semver and **`PRERELEASE=1`** for a **new** PR train instead of re-bumping **`DEV_VERSION`** on the same **`.ver`**. For a **single GA feature** at the entries root without **`preproduction */`**, prefer **one** **`.ver`** revised in place.
2. Set **`MAJOR_VERSION`**, **`STANDARD_VERSION`**, **`RELEASE_VERSION`**, and **`DESCRIPTION`** (single line or `DESCRIPTION<<DELIM` … `DELIM` heredoc). Optionally set **`PRERELEASE`** (**`0`** or **`1`**), **`GM`** (**`0`** or **`1`**, never **`1`** at the entries root), and **`DEV_VERSION`** (see **Preproduction directories** below). See **`version/entries/ABOUT.txt`** for syntax.
3. **Do not** commit **`version/locked/*.ver`** on typical **AI-authored feature PRs**—automation publishes **`version/locked/`** after merge to **`develop`** (see below). **`userland/shell/version_def.h`** is generated from **`version/locked/`** and **`version/entries/**/*.ver`** (normal prerelease **`VERSION_LINE`** rules, or **`GM=1`** override—see table row for **`version_def.h`**). On same-repo **feature pushes** and **pull requests** (not direct pushes to **`develop`** or **`main`**), **CI** runs relocate + **`gen_version_def.sh`** and **pushes one commit** for **`version/entries`** and **`userland/shell/version_def.h`** when either changed. **AI:** do **not** commit that header; fork PRs and offline checks may still run **`gen_version_def.sh`** per maintainer guidance when **`check_version_header_matches_locked`** must pass before the bot lands. **Version lock on merge** still publishes the header with **`version/locked/`** sync after merges to **`develop`**.

Ordering of multiple `.ver` files is by the **numeric fields inside** each file, not by filename prefix.

**Unique iteration keys (CI):** Across **`version/entries/**/*.ver`**, the quadruple **`(MAJOR_VERSION, STANDARD_VERSION, RELEASE_VERSION, DEV_VERSION)`** must be **unique**. Rows with **no** **`DEV_VERSION=`** line are treated as **`DEV_VERSION=0`** for this check (so two GA-oriented rows at the same **A.B.C** without **`DEV_VERSION`** collide). Before choosing **`DEV_VERSION`** for a given **A.B.C**, scan **`version/entries/`** and **`version/entries/preproduction <A>.<B>.<C>/`** and pick the **smallest unused** non-negative integer (**O(kn)** over directories and files). **`scripts/check_version_entries_semver_dev_unique.sh`** enforces this on every CI run (after **`check_version_prerelease_layout.sh`**).

## Preproduction directories, **`PRERELEASE`**, **`GM`**, and **`DEV_VERSION`**

Preproduction metadata uses **`PRERELEASE`**, optional **`PRERELEASE_TAG`**, **`GM`** (go-to-main), and **`DEV_VERSION`** (develop iteration “D” in an **`A.B.C.D`** sense — **`D`** is **not** part of the basename). **`DEV_VERSION`** feeds the **`, BUILD n`** suffix in **`VERSION_LINE`** for ordinary prerelease rows. **`GM=1`** is special: **`scripts/gen_version_def.sh`** then takes **`MAJOR`/`STANDARD`/`RELEASE`** from that row for **`VERSION_*`** / **`VERSION`** and sets **`VERSION_LINE`** to **plain `A.B.C`** (no tag, no **`, BUILD n`**), until promotion removes **`GM`**.

| Key | Meaning |
|-----|--------|
| **`PRERELEASE`** | **`0`** or omitted = GA-oriented row at the **`version/entries/`** root. **`1`** = prerelease: **author at the `version/entries/` root** on same-repo branches; **`relocate_root_prerelease_ver_to_preproduction.sh`** moves the file under **`preproduction <A>.<B>.<C>/`** (do **not** hand-add new **`PRERELEASE=1`** **`.ver`** paths under that directory on same-repo PRs). Fork PRs must relocate locally, then commit. |
| **`PRERELEASE_TAG`** | Optional short token (letters/digits/`._+-` only) shown before the prerelease semver in **`VERSION_LINE`** (default **`PRE`** when **`PRERELEASE=1`** and the key is absent). |
| **`GM`** | **`0`** or omitted by default. **`1`** = maintainer signals this **`preproduction <A>.<B>.<C>/`** train is ready for **`promote_preproduction_for_main.sh`** to emit one GA root **`.ver`** at **`version/entries/`** using the **`GM=1`** file’s basename, semver, optional **`RELEASE_DATE`**, and **`DESCRIPTION`** (**allowed only inside** **`preproduction <A>.<B>.<C>/`**; at most **one** file per directory may set **`GM=1`**). **Cursor / AI:** **never** set **`GM=1`** without **explicit human maintainer** permission. |
| **`DEV_VERSION`** | Non-negative integer. With **`PRERELEASE=1`**, use **`>= 1`**. **Maintainers** may bump per iteration using **`./scripts/bump_dev_version.sh`** when policy calls for it. **Cursor / AI on same-repo feature PRs:** compare to **`develop`**: set **`DEV_VERSION`** to exactly **`N+1`** at most once per **`.ver`** path (**`N`** = value on **`develop`**, or **`0`** if absent); **never** edit **`DEV_VERSION`** again after that—**`DESCRIPTION`** only. **Never increase** **`DEV_VERSION`** again after it is established on the branch (including via **`bump_dev_version.sh`**). You may keep **`DEV_VERSION`** on a **root** row while **`PRERELEASE`** is still **`0`** to record develop-side iteration **before** moving the line under **`preproduction/`**. **`main`** must ship **`.ver`** files **without** any **`DEV_VERSION=`** line (CI). |

**Layout rule:** if **`PRERELEASE=1`**, the **`.ver`** file must end up under:

`version/entries/preproduction <A>.<B>.<C>/`

where **`<A>.<B>.<C>`** matches **`MAJOR`**, **`STANDARD`**, and **`RELEASE`** in that file (example directory name: **`preproduction 2.3.0`** — note the space after `preproduction`). **Filenames** stay **`A_B_C_short_slug.ver`**; the directory carries the logical “D” slot, not the basename.

On **same-repo** pull requests into **`develop`** and on **feature-branch pushes** (not **`develop`** / **`main`** themselves), GitHub Actions may commit the move for you: it runs **`./scripts/relocate_root_prerelease_ver_to_preproduction.sh`** (see **`.github/actions/prepare-version-entries`**) before **`check_version_prerelease_layout.sh`**. The same workflow job then runs **`./scripts/gen_version_def.sh`** and may push one automated commit covering relocated **`version/entries`** and **`userland/shell/version_def.h`** when needed. **Fork** pull requests cannot receive that push—run the relocate script and **`gen_version_def.sh`** locally, commit, and push. **`develop`** uses **Version lock on merge** to run the same relocate step before **`finalize_version_locked.sh`** and includes **`version/entries/**`** in the sync PR when paths change.

**Date stamp policy:** we date DEV-version **`.ver`** rows **just before** they move into **`preproduction A.B.C/`**: **`relocate_root_prerelease_ver_to_preproduction.sh`** appends **`RELEASE_DATE=YYYY-MM-DD`** (that run’s calendar day) while the file is still at **`version/entries/*.ver`**, then moves it into the directory. For merges (e.g. **`cursor/*`**), upload those **`.ver`** files **outside** the preproduction directory; Actions moves them **inside** **`preproduction A.B.C/`**.

**`VERSION` / `version_def.h`:** **`A.B.C` only** in the header — **`scripts/gen_version_def.sh`** normally walks **`version/locked/**/*.ver`** (recursive) and ignores **`PRERELEASE`**, **`GM`**, and **`DEV_VERSION`** there for the numeric triple. **Exception:** if any **`version/entries/**/*.ver`** has **`PRERELEASE=1`** and **`GM=1`**, the **newest** such row (**highest semver, then highest `DEV_VERSION`**) supplies **`VERSION_*`** / **`VERSION`**, and **`VERSION_LINE`** is that **`A.B.C`** string alone.

**Bump develop iteration:** **`./scripts/bump_dev_version.sh path/to/file.ver`** (or **`make bump-dev-version VER=…`**).

**Merge simulations (develop vs main):** **`./scripts/version_merge_sim_status.sh`** (or **`make version-merge-sim-status ARGS='…'`**) reports **`version/entries`** and **`version/locked`** state without checking out both branches: use **`--develop origin/develop --main origin/main`** from one clone, or **`--worktree /path/to/develop-clone`** on a second checkout after setting **`GM=1`**. **`--promote-dry-run`** runs **`promote_preproduction_for_main.sh --dry-run`** (preview only); **`--check`** runs layout and main-line policy scripts on the working tree. **`--json`** emits machine-readable output.

**Finalize before `main`:** **`./scripts/promote_preproduction_for_main.sh`** (or **`make promote-preproduction-for-main`**) — a **maintainer** sets **`GM=1`** on **exactly one** **`.ver`** in a **`preproduction A.B.C/`** directory when that train is ready, then runs the script. Add **`--dry-run`** to preview the GA root **`.ver`** and preproduction directory removal without writing files. For each such directory under **`version/entries`** and **`version/locked`** that contains **exactly one** **`GM=1`** row among its **`*.ver`** files, the script writes **one** new root GA **`.ver`** under **`version/entries/`** using the **basename** of the **`GM=1`** file. **`MAJOR_VERSION`**, **`STANDARD_VERSION`**, **`RELEASE_VERSION`**, optional **`RELEASE_DATE`**, and **`DESCRIPTION`** are taken **only** from that **`GM=1`** file; the shipped GA row **omits** **`PRERELEASE`**, **`GM`**, and **`DEV_VERSION`** entirely. It then **removes every** **`*.ver`** in that **`preproduction A.B.C/`** folder and **deletes** the directory from **`version/entries`** (and from **`version/locked`** only if a legacy copy existed there). Other prerelease rows in that folder are **not** merged into prose—they remain in git history only. **`finalize_version_locked.sh`** never copies **`preproduction */`** into **`locked`**, so after promotion the next finalize copies **only** that new root **`.ver`** (and the rest of **`version/entries/`** without **`preproduction */`**) into **`version/locked/`**. Directories with **`PRERELEASE=1`** but **no** **`GM=1`** yet are left untouched.

**`main`** must have **no** **`preproduction *`** directories and **no** **`PRERELEASE=1`**, **`GM=1`**, or **`DEV_VERSION=`** lines — CI runs **`scripts/check_version_main_prerelease_policy.sh`**.

**Layout CI:** **`scripts/check_version_prerelease_layout.sh`** validates **`version/entries`** on every run. **`scripts/check_version_entries_semver_dev_unique.sh`** rejects duplicate **`(MAJOR, STANDARD, RELEASE, DEV_VERSION)`** keys across the same tree.

## `RELEASE_DATE` — usually omit in entries

**Authors do not need to set `RELEASE_DATE=YYYY-MM-DD` manually** for routine merge metadata.

**Preproduction intake (relocate):** **`./scripts/relocate_root_prerelease_ver_to_preproduction.sh`** (or **`make relocate-root-prerelease-ver`**) runs in CI before prerelease layout checks. For each **`version/entries/*.ver`** at the repository root with **`PRERELEASE=1`**, if **`RELEASE_DATE`** is absent it appends **`RELEASE_DATE=`** using **that run’s calendar day** while the file is still at the root, **then** moves the file under **`preproduction <A>.<B>.<C>/`**.

After a push to **`develop`**, the **Version lock on merge** workflow (`.github/workflows/version-lock-on-merge.yml`) runs, in order:

1. **`relocate_root_prerelease_ver_to_preproduction.sh`** — same root → **`preproduction */`** rule as CI (stamps missing **`RELEASE_DATE`** on those root rows first).
2. **`finalize_version_locked.sh`** — copies **`version/entries/`** → **`version/locked/`**, **omitting** top-level **`preproduction A.B.C/`** directories.
3. **`stamp_version_release_date.sh`** — appends **`RELEASE_DATE=`** (merge calendar day) to any **`*.ver`** under **`version/locked`** and the same relative paths under **`version/entries`**, then to **every** **`*.ver`** under **`version/entries`** recursively (including top-level **`preproduction */`** trees that **finalize** does not copy into **`locked`**) that **still** lack **`RELEASE_DATE`**.
4. **`gen_version_def.sh`** — regenerates **`userland/shell/version_def.h`**.

If automation opens a follow-up PR (e.g. `chore(version): sync version/locked from entries after merge`), merging that PR completes publication of locked files, any **entries** path moves from relocate, and the header on **`develop`**.

For local or offline changelog experiments, **`scripts/gen_version_changelog.c`** may use the generator’s calendar date when **`RELEASE_DATE`** is absent; that is separate from the merge-time stamp above.

## Automation summary

- **Feature branch:** commit **`version/entries/*.ver`** (and matching **`ABOUT.txt`** updates when required). Prerelease rows may start at the entries root with **`PRERELEASE=1`**; CI relocates them into **`preproduction A.B.C/`** and stamps **`RELEASE_DATE`** when missing. Avoid committing **`version/locked/*.ver`** (except coordinated **`ABOUT.txt`** per CI). **CI** may **push** relocated **`version/entries`** and **`userland/shell/version_def.h`** in one commit after **`gen_version_def.sh`** (same-repo only; see **`.github/workflows/c-cpp.yml`**). **AI:** never hand-commit or **`git add`** the header on same-repo feature PRs.
- **After merge to `develop`:** **Version lock on merge** relocates any remaining root **`PRERELEASE=1`** rows, syncs **`version/locked/`**, stamps missing **`RELEASE_DATE`**, regenerates **`version_def.h`**, and may open a PR for maintainers to merge when branch protection blocks direct pushes.

## Further reading

- **`AGENTS.md`** — Cursor Cloud, tests, and versioning summary.
- **`CLAUDE.md`** — AI assistant context and versioning rules.
- **`.cursor/rules/versioning.mdc`** and **`.cursor/rules/review_tools.mdc`** — Cursor IDE rules (versioning and optional review notes; no mandatory third-party CLI before push).
- **`.github/workflows/version-lock-on-merge.yml`** — exact automation steps.
- **`./scripts/export_version_record.sh`** — print current version from the tree (`--json` supported).
