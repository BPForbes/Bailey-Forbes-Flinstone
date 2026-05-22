# Replit — Bailey-Forbes-Flinstone

Run this repo on [Replit](https://replit.com/~) (import from GitHub or fork).

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

Do not commit `userland/shell/version_def.h` from Replit edits; CI owns the generated header on merge.
