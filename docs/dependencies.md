# Build dependencies

This project uses **system packages** (via `apt` on Debian/Ubuntu) for the toolchain and optional libraries, and **`make deps`** for in-tree builds of SDL2 and CUnit. The canonical one-line install for Cursor Cloud and similar images is in [AGENTS.md](../AGENTS.md) at the repository root.

## Replit ([replit.com/~](https://replit.com/~))

Import the repo as a Replit App with **`.replit`** and **`replit.nix`** (see [docs/replit.md](replit.md) and [replit.md](../replit.md)). Nix package names are also listed in [`nix/deps.json`](../nix/deps.json).

## Required to compile (typical Linux host)

| Component | Purpose |
|-----------|---------|
| `build-essential`, `gcc`, `make`, `binutils` | Default C + GAS build |
| `nasm` | `make ARCH=x86_64_nasm` |
| `gcc-aarch64-linux-gnu`, `binutils-aarch64-linux-gnu` | `make ARCH=arm` cross build |
| `pkg-config`, `curl`, `ca-certificates`, `cmake`, `autoconf`, `automake`, `libtool`, `bzip2`, `tar` | `make deps`, `make deps-sdl2`, `make deps-cunit` |
| `libsdl2-dev` | `make vm-sdl` when not using `deps/install` |
| `libcunit1-dev` | CUnit test binary (`make BPForbes_Flinstone_Tests`) |

## Optional (development and validation)

| Package / tool | Purpose |
|----------------|---------|
| **`dosfstools`** (`dosfsck`, `mkfs.fat`) | **Not linked at build time.** Useful to validate or compare host FAT32 super-floppy images produced by the shell (same layout expectations as common FAT32 tools). Example: `dosfsck -n drive.img` |
| `file(1)` | Quick magic-string checks on disk images |

## In-repo dependency shortcut

From the repository root:

```bash
make deps          # SDL2 + CUnit into deps/install (uses cmake/curl/etc. from the table above)
```

Then prefer `deps/install` paths as described in `AGENTS.md` for SDL2 and CUnit.

## Host disk I/O and assembly

On Linux x86-64 and AArch64 host builds, positioned file reads/writes for FAT32 images and cluster offsets use **`disk_host_io`** assembly (`pread64` / `pwrite64` syscalls): GAS **`disk_host_io.s`**, NASM **`disk_host_io.asm`**. Shell history and audit tail append use **`shell_history_host_asm`** (GAS **`.s`**, NASM **`.asm`**). Cluster buffers use **`mem_asm`** (`asm_mem_copy`, `asm_mem_zero`).

**`USE_ASM_ALLOC=1`:** only **`malloc`**, **`calloc`**, **`realloc`**, and **`free`** are global in the final link; allocator internals are **`.hidden`** in asm and **`local:`** via **`scripts/linker/alloc_internal_local.ver`**.

**`ARCH=x86_64_nasm`:** userland asm under **`arch/x86_64/nasm/`**; kernel boot/driver asm under **`kernel/arch/x86_64/`** is still assembled with GAS (**`$(CC) -c`** on **`.s`**).
