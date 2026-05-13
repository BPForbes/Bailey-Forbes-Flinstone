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
| **`version/locked/*.ver`** | **Published mirror.** Filled by **`finalize_version_locked.sh`** (full copy **`version/entries/`** → **`version/locked/`**) run locally by maintainers or by **Version lock on merge** GitHub Actions after changes land on **`develop`**. **Do not hand-edit** locked `.ver` files for routine version bumps—edit **`version/entries/`** instead. |
| **`userland/shell/version_def.h`** | **Generated.** Produced by **`scripts/gen_version_def.sh`** (also via **`make`**) from the **highest** `A.B.C` among **`version/locked/*.ver`**. **Never hand-edit** `VERSION_*` in this header. |

**Companion prose only:** **`version/entries/ABOUT.txt`** and **`version/locked/ABOUT.txt`** describe the `.ver` format. When CI requires them to match, update **both** in the same pull request with **identical** bytes. They are not release semver files; they are documentation.

## Creating and handling `.ver` files

1. On the **first** substantive code or docs change for a pull request, add **`version/entries/<A>_<B>_<C>_<short_slug>.ver`** if no entry yet covers that PR (prefer one `.ver` per PR; revise it as the branch evolves).
2. Set **`MAJOR_VERSION`**, **`STANDARD_VERSION`**, **`RELEASE_VERSION`**, and **`DESCRIPTION`** (single line or `DESCRIPTION<<DELIM` … `DELIM` heredoc). Optionally set **`DEV_PHASE`** for develop-only iteration on the same triple (see **Optional `DEV_PHASE`** above). See **`version/entries/ABOUT.txt`** for syntax.
3. **Do not** commit **`version/locked/*.ver`** or **`userland/shell/version_def.h`** on typical **AI-authored feature PRs**—automation publishes those after merge to **`develop`** (see below).

Ordering of multiple `.ver` files is by the **numeric fields inside** each file, not by filename prefix.

## Optional `DEV_PHASE` (develop phase **D**)

You may add **`DEV_PHASE=<positive integer>`** to a **`.ver`** file to record extra integration iterations on **`develop`** for the **same `A.B.C`** (think **`A.B.C.D`** with **D** only in prose, not in **`VERSION`**).

- **`userland/shell/version_def.h`** and the **`VERSION`** string stay **`A.B.C` only** — **`scripts/gen_version_def.sh`** ignores **`DEV_PHASE`**.
- **Filenames** stay **`A_B_C_short_slug.ver`** — **D** is not part of the basename.
- **Default:** if **`DEV_PHASE`** is omitted, **`scripts/gen_version_changelog.c`** treats the entry as phase **`1`** when sorting same-triple releases (higher **D** sorts first for changelog headline metadata among equal **`A.B.C`**).
- **Bumping:** run **`./scripts/bump_dev_phase_in_ver.sh path/to/file.ver`** (increments an existing **`DEV_PHASE`**, or inserts **`DEV_PHASE=1`** after the patch line). After **`finalize_version_locked.sh`**, **`version/locked`** carries the same lines as **`version/entries`**.
- **`main` policy:** **`DEV_PHASE`** must **not** appear in any **`*.ver`** under **`version/entries/`** or **`version/locked/`** on **`main`**. Before merging **`develop` → `main`**, run **`./scripts/strip_dev_phase_from_ver_trees.sh`** (or **`make strip-dev-phase-from-ver`**). CI runs **`scripts/check_version_dev_phase_absent_on_main.sh`** on pushes to **`main`** and on pull requests whose **base** is **`main`**.

## `RELEASE_DATE` — usually omit in entries

**Authors do not need to set `RELEASE_DATE=YYYY-MM-DD` manually** in **`version/entries/*.ver`** for merge metadata.

After a push to **`develop`**, the **Version lock on merge** workflow (`.github/workflows/version-lock-on-merge.yml`) runs, in order:

1. **`finalize_version_locked.sh`** — copies **`version/entries/`** → **`version/locked/`** (published snapshot).
2. **`stamp_version_release_date.sh`** — appends **`RELEASE_DATE=`** (merge calendar day) to any **`*.ver`** under **`version/locked`** and **`version/entries`** that **do not** already declare **`RELEASE_DATE`**, keeping the two trees aligned.
3. **`gen_version_def.sh`** — regenerates **`userland/shell/version_def.h`**.

If automation opens a follow-up PR (e.g. `chore(version): sync version/locked from entries after merge`), merging that PR completes publication of locked files and the header on **`develop`**.

For local or offline changelog experiments, **`scripts/gen_version_changelog.c`** may use the generator’s calendar date when **`RELEASE_DATE`** is absent; that is separate from the merge-time stamp above.

## Automation summary

- **Feature branch:** commit **`version/entries/*.ver`** (and matching **`ABOUT.txt`** updates when required). Avoid committing **`version/locked/*.ver`** (except coordinated **`ABOUT.txt`** per CI) and **`userland/shell/version_def.h`**.
- **After merge to `develop`:** **Version lock on merge** syncs **`version/locked/`**, stamps missing **`RELEASE_DATE`**, regenerates **`version_def.h`**, and may open a PR for maintainers to merge when branch protection blocks direct pushes.

## Further reading

- **`AGENTS.md`** — Cursor Cloud, tests, and versioning summary.
- **`CLAUDE.md`** — AI assistant context and versioning rules.
- **`.cursor/rules/versioning.mdc`** and **`.cursor/rules/review_tools.mdc`** — Cursor IDE rules (versioning and optional review notes; no mandatory third-party CLI before push).
- **`.github/workflows/version-lock-on-merge.yml`** — exact automation steps.
- **`./scripts/export_version_record.sh`** — print current version from the tree (`--json` supported).
