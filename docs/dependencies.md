# Build dependencies

This project uses **system packages** (via `apt` on Debian/Ubuntu) for the toolchain and optional libraries, and **`make deps`** for in-tree builds of SDL2 and CUnit. The canonical one-line install for Cursor Cloud and similar images is in [AGENTS.md](../AGENTS.md) at the repository root.

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

On Linux x86-64 and AArch64 host builds (GAS), positioned file reads/writes for FAT32 images and cluster offsets use **`disk_host_io.s`** (`pread64` / `pwrite64` syscalls) with C fallbacks where ASM is not used (for example `ARCH=x86_64_nasm`, which defines `DISK_HOST_USE_LIBC_PREADV=1`). Cluster buffers still use **`mem_asm.s`** (`asm_mem_copy`, `asm_mem_zero`).
