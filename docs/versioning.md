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
| **`userland/shell/version_def.h`** | **Generated.** Produced by **`scripts/gen_version_def.sh`** (also via **`make`**) from the **highest** `A.B.C` among **`version/locked/**/*.ver`**. **Never hand-edit** `VERSION_*` in this header. |

**Companion prose only:** **`version/entries/ABOUT.txt`** and **`version/locked/ABOUT.txt`** describe the `.ver` format. When CI requires them to match, update **both** in the same pull request with **identical** bytes. They are not release semver files; they are documentation.

## Creating and handling `.ver` files

1. On the **first** substantive code or docs change for a pull request, add **`version/entries/<A>_<B>_<C>_<short_slug>.ver`** if no entry yet covers that PR (prefer one `.ver` per PR; revise it as the branch evolves).
2. Set **`MAJOR_VERSION`**, **`STANDARD_VERSION`**, **`RELEASE_VERSION`**, and **`DESCRIPTION`** (single line or `DESCRIPTION<<DELIM` … `DELIM` heredoc). Optionally set **`PRERELEASE`** (**`0`** or **`1`**), **`GM`** (**`0`** or **`1`**, never **`1`** at the entries root), and **`DEV_VERSION`** (see **Preproduction directories** below). See **`version/entries/ABOUT.txt`** for syntax.
3. **Do not** commit **`version/locked/*.ver`** or **`userland/shell/version_def.h`** on typical **AI-authored feature PRs**—automation publishes those after merge to **`develop`** (see below).

Ordering of multiple `.ver` files is by the **numeric fields inside** each file, not by filename prefix.

## Preproduction directories, **`PRERELEASE`**, **`GM`**, and **`DEV_VERSION`**

Preproduction metadata uses **`PRERELEASE`**, **`GM`** (go-to-main), and **`DEV_VERSION`** (develop iteration “D” in an **`A.B.C.D`** sense — **`D`** is **not** part of the basename or **`version_def.h`**):

| Key | Meaning |
|-----|--------|
| **`PRERELEASE`** | **`0`** or omitted = GA-oriented row at the **`version/entries/`** root. **`1`** = prerelease row that must end under **`preproduction <A>.<B>.<C>/`** (you may commit it at the entries root on same-repo PRs; CI runs **`relocate_root_prerelease_ver_to_preproduction.sh`** before layout checks; fork PRs must relocate locally). |
| **`GM`** | **`0`** or omitted by default. **`1`** = this prerelease directory is ready to be merged to a single GA **`.ver`** at the tree root (**allowed only inside** **`preproduction <A>.<B>.<C>/`**; at most **one** file per directory may set **`GM=1`**). |
| **`DEV_VERSION`** | Non-negative integer. With **`PRERELEASE=1`**, use **`>= 1`** and bump per iteration using **`./scripts/bump_dev_version.sh`**. You may keep **`DEV_VERSION`** on a **root** row while **`PRERELEASE`** is still **`0`** to record develop-side iteration **before** moving the line under **`preproduction/`**. **`main`** must ship **`.ver`** files **without** any **`DEV_VERSION=`** line (CI). |

**Layout rule:** if **`PRERELEASE=1`**, the **`.ver`** file must end up under:

`version/entries/preproduction <A>.<B>.<C>/`

where **`<A>.<B>.<C>`** matches **`MAJOR`**, **`STANDARD`**, and **`RELEASE`** in that file (example directory name: **`preproduction 4.0.0`** — note the space after `preproduction`). **Filenames** stay **`A_B_C_short_slug.ver`**; the directory carries the logical “D” slot, not the basename.

On **same-repo** pull requests into **`develop`** and on **feature-branch pushes** (not **`develop`** / **`main`** themselves), GitHub Actions may commit the move for you: it runs **`./scripts/relocate_root_prerelease_ver_to_preproduction.sh`** (see **`.github/actions/prepare-version-entries`**) before **`check_version_prerelease_layout.sh`**. **Fork** pull requests cannot receive that push—run the relocate script locally, commit, and push. **`develop`** uses **Version lock on merge** to run the same relocate step before **`finalize_version_locked.sh`** and includes **`version/entries/**`** in the sync PR when paths change.

**Date stamp policy:** we date DEV-version **`.ver`** rows **just before** they move into **`preproduction A.B.C/`**: **`relocate_root_prerelease_ver_to_preproduction.sh`** appends **`RELEASE_DATE=YYYY-MM-DD`** (that run’s calendar day) while the file is still at **`version/entries/*.ver`**, then moves it into the directory. For merges (e.g. **`cursor/*`**), upload those **`.ver`** files **outside** the preproduction directory; Actions moves them **inside** **`preproduction A.B.C/`**.

**`VERSION` / `version_def.h`:** still **`A.B.C` only** — **`scripts/gen_version_def.sh`** walks **`version/locked/**/*.ver`** (recursive) and ignores **`PRERELEASE`**, **`GM`**, and **`DEV_VERSION`** for the numeric triple.

**Bump develop iteration:** **`./scripts/bump_dev_version.sh path/to/file.ver`** (or **`make bump-dev-version VER=…`**).

**Finalize before `main`:** **`./scripts/promote_preproduction_for_main.sh`** (or **`make promote-preproduction-for-main`**) — for each **`preproduction A.B.C/`** directory under **`version/entries`** and **`version/locked`** that contains **exactly one** **`GM=1`** row among its **`*.ver`** files, collects **every** **`*.ver`** in that folder (each must have **`PRERELEASE=1`** and **`DEV_VERSION>=1`**), sorts them by **`DEV_VERSION`** ascending, and writes **one** new root **`.ver`** under **`version/entries/`** named like the **`GM=1`** file. The merged file’s **`DESCRIPTION`** itemizes each source description in **`DEV_VERSION`** order and **omits** **`PRERELEASE`**, **`GM`**, and **`DEV_VERSION`** keys entirely. The script then **deletes** the **`preproduction A.B.C/`** directory from **`version/entries`** (and from **`version/locked`** only if a legacy copy exists there). **`finalize_version_locked.sh`** never copies **`preproduction */`** into **`locked`**, so after promotion the next finalize publishes **only** the new root **`.ver`** into **`version/locked/`**. Directories with **`PRERELEASE=1`** but **no** **`GM=1`** yet are left untouched.

**`main`** must have **no** **`preproduction *`** directories and **no** **`PRERELEASE=1`**, **`GM=1`**, or **`DEV_VERSION=`** lines — CI runs **`scripts/check_version_main_prerelease_policy.sh`**.

**Layout CI:** **`scripts/check_version_prerelease_layout.sh`** validates **`version/entries`** on every run.

## `RELEASE_DATE` — usually omit in entries

**Authors do not need to set `RELEASE_DATE=YYYY-MM-DD` manually** for routine merge metadata.

**Preproduction intake (relocate):** **`./scripts/relocate_root_prerelease_ver_to_preproduction.sh`** (or **`make relocate-root-prerelease-ver`**) runs in CI before prerelease layout checks. For each **`version/entries/*.ver`** at the repository root with **`PRERELEASE=1`**, if **`RELEASE_DATE`** is absent it appends **`RELEASE_DATE=`** using **that run’s calendar day** while the file is still at the root, **then** moves the file under **`preproduction <A>.<B>.<C>/`**.

After a push to **`develop`**, the **Version lock on merge** workflow (`.github/workflows/version-lock-on-merge.yml`) runs, in order:

1. **`relocate_root_prerelease_ver_to_preproduction.sh`** — same root → **`preproduction */`** rule as CI (stamps missing **`RELEASE_DATE`** on those root rows first).
2. **`finalize_version_locked.sh`** — copies **`version/entries/`** → **`version/locked/`**, **omitting** top-level **`preproduction A.B.C/`** directories.
3. **`stamp_version_release_date.sh`** — appends **`RELEASE_DATE=`** (merge calendar day) to any **`*.ver`** under **`version/locked`** and the mirrored paths under **`version/entries`** that **still** lack **`RELEASE_DATE`**, keeping the two trees aligned.
4. **`gen_version_def.sh`** — regenerates **`userland/shell/version_def.h`**.

If automation opens a follow-up PR (e.g. `chore(version): sync version/locked from entries after merge`), merging that PR completes publication of locked files, any **entries** path moves from relocate, and the header on **`develop`**.

For local or offline changelog experiments, **`scripts/gen_version_changelog.c`** may use the generator’s calendar date when **`RELEASE_DATE`** is absent; that is separate from the merge-time stamp above.

## Automation summary

- **Feature branch:** commit **`version/entries/*.ver`** (and matching **`ABOUT.txt`** updates when required). Prerelease rows may start at the entries root with **`PRERELEASE=1`**; CI relocates them into **`preproduction A.B.C/`** and stamps **`RELEASE_DATE`** when missing. Avoid committing **`version/locked/*.ver`** (except coordinated **`ABOUT.txt`** per CI) and **`userland/shell/version_def.h`**.
- **After merge to `develop`:** **Version lock on merge** relocates any remaining root **`PRERELEASE=1`** rows, syncs **`version/locked/`**, stamps missing **`RELEASE_DATE`**, regenerates **`version_def.h`**, and may open a PR for maintainers to merge when branch protection blocks direct pushes.

## Further reading

- **`AGENTS.md`** — Cursor Cloud, tests, and versioning summary.
- **`CLAUDE.md`** — AI assistant context and versioning rules.
- **`.cursor/rules/versioning.mdc`** and **`.cursor/rules/review_tools.mdc`** — Cursor IDE rules (versioning and optional review notes; no mandatory third-party CLI before push).
- **`.github/workflows/version-lock-on-merge.yml`** — exact automation steps.
- **`./scripts/export_version_record.sh`** — print current version from the tree (`--json` supported).
