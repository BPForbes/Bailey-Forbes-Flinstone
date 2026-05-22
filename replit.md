# Replit — Bailey-Forbes-Flinstone

Run this repo on [Replit](https://replit.com/~) (import from GitHub or fork).

## Replit Agent policy (required)

> **Indicator:** Any **Replit Agent** (Replit AI, Ghostwriter, or other in-Repl coding assistants) working in this repository **must** follow the **same** project rules as **Cursor**, **Claude**, and **CodeRabbit**—not a separate or relaxed policy.

Treat these files as **one combined, authoritative policy set** (read all that apply before substantive edits):

| Source | Path | Role |
|--------|------|------|
| **Replit (this file)** | [`replit.md`](replit.md) | Replit onboarding + explicit agent binding to the rows below |
| **Claude** | [`CLAUDE.md`](CLAUDE.md) | Project context, versioning, module contracts, merge expectations |
| **Cursor** | [`AGENTS.md`](AGENTS.md) | Cloud/toolchain install, build targets, tests, AI feature-branch workflow |
| **Cursor IDE rules** | [`.cursor/rules/versioning.mdc`](.cursor/rules/versioning.mdc) | Version lock, `.ver` authoring, `version_def.h`, preproduction layout |
| **Cursor IDE rules** | [`.cursor/rules/review_tools.mdc`](.cursor/rules/review_tools.mdc) | PR review tooling notes (optional CodeRabbit/Codex CLI) |
| **CodeRabbit** | [`.coderabbit.yaml`](.coderabbit.yaml) | PR review instructions (versioning, lock system, semver, path-specific hints) |
| **Versioning (long form)** | [`docs/versioning.md`](docs/versioning.md) | Full `.ver` and CI/automation guide |

**Replit Agent must:**

- Follow **semantic versioning** and the **lock system** in `CLAUDE.md` / `.cursor/rules/versioning.mdc` / `.coderabbit.yaml` (author `version/entries/*.ver` only on feature work; **never** edit merge-base `.ver` or `version/locked/**`; **never** commit `userland/shell/version_def.h` on same-repo feature PRs).
- Follow **AGENTS.md** for builds (`make`, tests, `ARCH=`, `USE_ASM_ALLOC=1`) and the **AI feature-branch workflow** (`git merge --no-ff` when combining agent branches; one train per PR).
- Match **CodeRabbit** expectations on incoming vs target **VERSION** before merge and on comment/code balance where applicable.
- Prefer **minimal, focused diffs** and existing code conventions (same bar as Cursor/Claude review hints).

If `replit.md` and another file disagree, **resolve in favor of `CLAUDE.md`, `AGENTS.md`, `.cursor/rules/*.mdc`, and `.coderabbit.yaml`**, then update `replit.md` in a follow-up PR.

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

## Versioning

Same rules as **CLAUDE.md** / **`.cursor/rules/versioning.mdc`** / **`.coderabbit.yaml`**: do not commit `userland/shell/version_def.h` from Replit agent edits; CI owns the generated header on merge. Add a **new** root `version/entries/A_B_C_slug.ver` on first substantive change when none covers the PR.
