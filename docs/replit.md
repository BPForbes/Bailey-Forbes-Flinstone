# Replit support

This project can run on [Replit](https://replit.com/~) using Nix-backed system dependencies.

## Configuration files

| File | Role |
|------|------|
| [`.replit`](../.replit) | Run/compile commands, `cpp-clang14` module, Nix channel `stable-24_05` |
| [`replit.nix`](../replit.nix) | `deps` array — mirrors [AGENTS.md](../AGENTS.md) / [dependencies.md](dependencies.md) |
| [`replit.md`](../replit.md) | Replit onboarding; **Replit Agent policy** (bound to `CLAUDE.md`, `AGENTS.md`, `.cursor/rules/`, `.coderabbit.yaml`) |
| [`nix/deps.json`](../nix/deps.json) | JSON manifest (apt ↔ nix names, profiles) |

## Import from GitHub

Replit expects both `.replit` and `replit.nix` ([gitHubImport] in `.replit`). After import, open the **Shell** and run `make` if **Run** did not compile yet.

## Limits on Replit

- **SDL VM** (`make vm-sdl`) may need a graphical Repl; headless tiers should use `make` or `make vm` only.
- **AArch64 cross** (`make ARCH=arm`) is not preinstalled in `replit.nix`; use `flake.nix` / `nix develop .#cross-aarch64` locally, or extend `replit.nix` with cross packages.
- **dosfstools** / **file** for FAT image validation are optional; add `pkgs.dosfstools` and `pkgs.file` to `replit.nix` if needed.

## Related

- [dependencies.md](dependencies.md) — apt one-liner and linked libraries
- [AGENTS.md](../AGENTS.md) — full toolchain list
