# CLAUDE.md — Project context for AI assistants

This repository implements **Bailey-Forbes-Flinstone**: a educational OS/shell-style codebase with a host shell (`BPForbes_Flinstone_Shell`), kernel-layer modules, drivers, and optional VM builds. Build with `make` from the repo root; see **AGENTS.md** for toolchain install and targets.

## Module contracts (AI)

A **module contract** here means a **data-distribution contract**: a **frozen blueprint** for how data and outcomes **may** move across a boundary (ownership, lifetimes, error channels, and named **surfaces**). It **models** I/O between parts of the system; it does **not inherently add features**. **`contracts/foundations/*.h`** (P0) and **`contract_extend.h`** (prelude for future **Px** headers under **`contracts/extensions/`**) plus related **`fl/*`** headers carry the C-side contract bundle; **implementation** enforces contracts but is **not** the definition of the contract.

For a **P0–P9** row-by-row snapshot (**❌ / ⚠️ / ✅**) against **`develop`**, see **`docs/ROADMAP.md` → [Module contracts (abstraction and P0-P9 coverage)](#module-contracts-abstraction-and-p0-p9-coverage)**. Keep that table updated when contract coverage materially changes.

## Implementation boundaries (AI)

- Memory primitives, allocator internals, low-level synchronization, port I/O, and core hardware-facing routines should be backed by the architecture-specific ASM layer.
- Keep C code focused on higher-order application logic, driver orchestration, VM behavior, filesystem services, and policy/business rules.

### Kernel vs driver execution

When you propose new drivers for a module, ensure that the kernel functions solely as an orchestrator for those drivers. The kernel should delegate all module-specific behavior to the drivers rather than executing functionality on behalf of the module. This separation preserves a clean driver boundary, keeps execution logic where it belongs, and prevents the kernel from accumulating responsibilities that should remain in the driver layer.

**Scope note:** `kernel/core/net/` still executes cross-cutting protocol policy above the netdev boundary (routing, TCP/UDP, DHCP composition, session wire). Module-specific hardware or NIC behavior (scan, associate, frame TX/RX, firmware rings, UART AT, register pokes) belongs in `kernel/drivers/` and surfaces through narrow ops (`fl_net_driver_t`, backend routers such as `wifi_driver_backend`).

## Versioning (mandatory for merge-ready PRs)

### Lock system (AI assistants — mandatory)

- **Never edit finalized paths under `version/locked/`** that already exist on the branch you are merging **into** (merge base). That tree is the **published** record on **`develop`**.
- **`userland/shell/version_def.h`, `DEV_VERSION`, and `GM` (AI):** **Never** stage or commit **`userland/shell/version_def.h`** on same-repo feature PRs; **`make`** may refresh it locally—leave it unstaged or match the merge target. **Do not** hand-edit **`VERSION_*`** / **`VERSION_LINE`** or run **`./scripts/gen_version_def.sh`** to “sync” the header after **`GM=1`** on **`.ver`**—**GitHub Actions** (`.github/workflows/c-cpp.yml` relocate + **`gen_version_def.sh`**, bot push when needed; **Version lock on merge** on **`develop`**) refresh **`VERSION_MAJOR`**, **`VERSION_STANDARD`**, **`VERSION_PATCH`**, and **`VERSION_LINE`** (plain **`A.B.C`** when **`GM=1`** wins). **`DEV_VERSION`:** compared to **`develop`**, you may set **`DEV_VERSION`** to exactly **`N+1`** once per **`.ver`** path (**`N`** = value on **`develop`**, or **`0`** if missing); **never** change **`DEV_VERSION`** again after that—**`DESCRIPTION`** only. **Never increase** **`DEV_VERSION`** again after it is established on the branch (for example **8→9** mid-PR), including via **`bump_dev_version.sh`**. **Never** set **`GM=1`** without **explicit human maintainer** permission. **Reviewers:** before flagging header churn on a feature PR, read **full PR commit history**; treat **`github-actions[bot]`** / **`chore(version):`** / **`gen_version_def.sh`** / **`prepare-version-entries`** / **Version lock** commits as CI publish (aligned with **`version/entries/**/*.ver`**); on mixed branches, attribute **`VERSION_LINE`** changes to automation when they match **`gen_version_def.sh`** output (see **`.coderabbit.yaml`** and **AGENTS.md**).
- **Cursor / AI-authored feature PRs:** commit **`version/entries/*.ver`** (and keep **`version/entries/ABOUT.txt`** byte-identical to the merge target’s **`version/locked/ABOUT.txt`** while **`version/locked/`** is unchanged—CI `check_version_locked_subset_of_entries.sh`). **Do not** commit **`version/locked/**`** in those PRs: **Version lock on merge** (GitHub Actions after push to **`develop`**) runs **`relocate_root_prerelease_ver_to_preproduction.sh`**, **`finalize_version_locked.sh`**, stamps dates when applicable, **`gen_version_def.sh`**, and opens a PR to publish **`version/locked/`** and the header when needed. **CI** may push **`version/entries`** + **`userland/shell/version_def.h`** on same-repo pushes when needed. Fork PRs and offline checks follow **`docs/versioning.md`**. **Maintainers** may run finalize locally before merge if they want an early snapshot on the branch.
- **Immutable `.ver` rows / one PR entry:** do **not** edit any **`version/entries/**/*.ver`** path that already exists on the merge base. Before adding a **`.ver`**, inspect the current submitted PR/branch for an existing PR-owned **`.ver`** (including rows CI relocated under **`version/entries/preproduction */`**). If one exists, update that file's **`DESCRIPTION`** only; do **not** add another. If a PR-owned relocated row's **`DESCRIPTION`** changes, move that same file back to the **`version/entries/`** root and remove any stamped **`RELEASE_DATE`** so the version job can relocate and stamp it again. Each PR should carry **exactly one new** **`.ver`** total unless a human maintainer explicitly requests a separate release entry. After merge to **`develop`**, that path is immutable too.
- **`version/entries/*.ver`** may be **added** on your branch; CI allows **`version/entries/`** to contain drafts not yet copied to **`version/locked/`**.
- **Unique `(MAJOR, STANDARD, RELEASE, DEV_VERSION)`:** no two **`.ver`** files under **`version/entries/`** (including **`preproduction */`**) may share the same quadruple; omitting a **`DEV_VERSION=`** line counts as **0**. **`./scripts/check_version_entries_semver_dev_unique.sh`** enforces this in CI—scan existing rows for that **A.B.C** before picking **`DEV_VERSION`**.
- **Optional prerelease layout:** **`PRERELEASE=1`** rows are **authored at the `version/entries/` root** on same-repo branches (**do not** hand-add new **`*.ver`** under **`preproduction A.B.C/`**—CI runs **`relocate_root_prerelease_ver_to_preproduction.sh`** (stamps missing **`RELEASE_DATE`**, then moves non-GM rows into **`preproduction */`**) before layout checks; root **`GM=1`** rows stay at the root for promotion). That tree is **not** copied into **`version/locked/`** by **`finalize_version_locked.sh`**. To revise a PR-owned prerelease row that CI already relocated, move that same file back to the entries root, remove **`RELEASE_DATE`**, and update **`DESCRIPTION`** only; do not add another **`.ver`** for the same PR. Maintainers may use **`DEV_VERSION`** / **`./scripts/bump_dev_version.sh`** under maintainer policy. **AI:** follow the **`N+1`**-once rule vs **`develop`** above; **never** set **`GM=1`**. A **maintainer** sets **`GM=1`** when appropriate and runs **`./scripts/promote_preproduction_for_main.sh`** (or **`make promote-preproduction-for-main`**) to emit one root GA **`.ver`** under **`version/entries/`** from the selected **`GM=1`** row’s prose and semver; if several rows in one preproduction directory are **`GM=1`**, promotion selects the highest **`DEV_VERSION`** row and rewrites lower **`DEV_VERSION`** GM rows to **`GM=0`** (other rows in the folder are deleted with the **`preproduction *`** directory; **`version/locked`** is cleaned only if a legacy copy existed). Before any merge that lands **`version/**`** on **`main`**, ensure that promotion has already run — **`main`** must not ship **`preproduction *`**, **`PRERELEASE=1`**, **`GM=1`**, or **`DEV_VERSION=`** — CI fails otherwise.
- **`version/locked/`** is the **published snapshot** (automation copies **`version/entries/`** → **`version/locked/`**, **excluding** top-level **`preproduction */`**). Do not hand-tweak **`version/locked/`** on feature branches in agent workflows.
- **Do not** include historical **`version/locked/**`** or bulk-edit **`version/entries/**`** in automated refactors without an explicit release workflow.
- **`.ver` on first change:** When you start substantive code edits for a PR, first check the submitted branch for a PR-owned **`.ver`** (for example **`git diff --name-only <merge-base>...HEAD -- version/entries`**, plus any CI-relocated **`version/entries/preproduction */`** row). If the PR already has one, revise that row's **`DESCRIPTION`** only. If that row is under **`preproduction */`**, move it back to the **`version/entries/`** root and remove **`RELEASE_DATE`** before submitting so the version job can run relocation/stamping again. If it does not, **create exactly one** new **`version/entries/A_B_C_slug.ver`** at the **`version/entries/`** root (**never** edit **`.ver`** paths from the merge base; **do not** hand-add new files under **`version/entries/preproduction */`**—Actions relocates root **`PRERELEASE=1`** rows there).
- **`GM=1` go-to-main (maintainer / explicit AI instruction):** add a **new** **`.ver`** under **`version/entries/preproduction <A>.<B>.<C>/`** with the next **`DEV_VERSION`** and **`GM=1`** (**never** set **`GM=1`** on a root **`*.ver`** file); **`DESCRIPTION`** must start with **`A.B.C:`** (e.g. **`4.0.1:`**) and summarize the **overall** release. **`promote_preproduction_for_main.sh`** copies only the selected highest-**`DEV_VERSION`** GM row’s text to the GA root **`.ver`** and can demote lower duplicate GM rows with **`--normalize-gm-only`**.
- **PR target branch from `.ver` state (AI — mandatory):** The `.ver` your PR adds determines which branch to target — using the wrong base causes CI to fail. A `.ver` with **`PRERELEASE=1`** + **`DEV_VERSION=<N>`** means the PR must target **`develop`**; all prerelease (non-GM) work merges to `develop`, and `check_version_main_prerelease_policy.sh` rejects `PRERELEASE=1` or `DEV_VERSION=` rows on any PR targeting `main`. A `.ver` with **`GM=1`** means the PR targets **`main`** — but only after a human maintainer explicitly authorizes `GM=1`; agents must never self-authorize. These two states are mutually exclusive merge targets: **never open a PR against `main` while the branch `.ver` still has `PRERELEASE=1` or `DEV_VERSION=`.**
- **`PRERELEASE` / `GM`:** binary flags — when present, values must be exactly **`0`** or **`1`** (enforced by **`scripts/lib/ver_field_parse.sh`** and CI).
- **CI date stamp:** **`.github/actions/prepare-version-entries`** runs **`relocate_root_prerelease_ver_to_preproduction.sh`**, **`promote_preproduction_for_main.sh --normalize-gm-only`**, **`promote_preproduction_for_main.sh`** (GA promote when **`GM=1`** is present), then **`stamp_version_entries_release_date.sh`** on the **entries root** and every **`preproduction <A>.<B>.<C>/`** tree before **`gen_version_def.sh`**.
- **No semver backtracking (AI):** The **preproduction version** is **A.B.C** from **`version/entries/preproduction <A>.<B>.<C>/`** directory names (**`PRERELEASE=1`** rows land there **after** relocate). Do **not** add a **new root** **`version/entries/*.ver`** whose **A.B.C** is **numerically below** that **A.B.C** unless a **human maintainer** explicitly asks. For **`DEV_VERSION`** on the single **`.ver`** your PR owns, follow the **`N+1`**-once rule vs **`develop`** (see bullets above). For routine **GA** work at **`version/entries/`** root, add **one new** **`.ver`** per PR—do not edit merge-base rows.

### Canonical string

- **`VERSION`** on **`develop`** comes from **`userland/shell/version_def.h`**, generated from **`version/locked/*.ver`** (highest **A.B.C**) **unless** a winning **`PRERELEASE=1`** + **`GM=1`** row under **`version/entries/`** overrides **`VERSION_*`** / **`VERSION`** and sets **`VERSION_LINE`** to plain **`A.B.C`**. Otherwise **`VERSION_LINE`** (shell display) follows **`version/entries/**/*.ver`** **`PRERELEASE=1`** rules. Author **`version/entries/A_B_C_slug.ver`** (preferred; avoid optional **`NNN_`** serial filename prefixes unless matching legacy files). **GitHub Actions** runs **`gen_version_changelog.c`** against **`version/locked`**, then **`make CHANGELOG_CI=1`**. Plain **`git clone` + `make`** skips changelog unless you opt in with **`CHANGELOG_CI=1`**.
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

Implement the bump with a **new** **`version/entries/<A>_<B>_<C>_<slug>.ver`** (one new file per feature PR/iteration—**`DESCRIPTION` only** on that new file until merge). Pushing to **`develop`** triggers **Version lock on merge** (entries → **`version/locked/`**, regenerate **`version_def.h`**, opens a PR if updates are needed). For a local release build after that, use **`make deploy`** or the **Deploy** workflow.

**Automation version PRs:** PRs opened **only** by **Version lock on merge** (same **`chore(version): sync`** convention) need **no** automatic or proactive AI review. If a **human** requests a review, treat it like any other PR. On **mixed** feature PRs, do **not** treat **`version_def.h`** updates from bot **`chore(version):`** / **`prepare-version-entries`** commits as agent policy violations—use commit history (above).

Export current numbers without compiling: **`./scripts/export_version_record.sh`**, **`./scripts/export_version_record.sh --json`**, or **`make version-record`** (`--json` for one-line JSON).

---

## Where else this is documented

- **AGENTS.md** — Cursor Cloud agents, versioning summary, and **AI feature-branch workflow**
- **replit.md** — Replit hosting; **Replit Agent** bound to the same policy as this file, **AGENTS.md**, **`.cursor/rules/*.mdc`**, and **`.coderabbit.yaml`** (stay on one `cursor/*` / agent branch unless a human allows a split; merge side work with **`git merge --no-ff`**; delete duplicate **`.ver`** files, do not edit immutable merge-base rows, fold notes into the **newest** row or a **new** **`.ver`**, re-run **`./scripts/check_version_entries_semver_dev_unique.sh`**)  
- **`docs/ROADMAP.md`** — phased **P0–P9** work; **module contracts** abstraction + **❌/⚠️/✅** snapshot: [Module contracts (abstraction and P0-P9 coverage)](./docs/ROADMAP.md#module-contracts-abstraction-and-p0-p9-coverage)  
- **`docs/versioning.md`** — `.ver` authoring, `RELEASE_DATE` automation, **A / B / C** semantics  
- **`.coderabbit.yaml`** — optional CodeRabbit integration when enabled on the repo  
- **`.cursor/rules/versioning.mdc`** — Cursor IDE versioning rules  
- **`.cursor/rules/review_tools.mdc`** — optional tooling notes (no CLI push gate)
- **`.cursor/rules/driver_orchestration.mdc`** — kernel orchestrates drivers; drivers execute module-specific behavior

Keep these documents aligned when changing policy.
