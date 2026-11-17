# Architecture Layout

The project supports multiple assembly backends for memory primitives, port I/O, and allocator.

## Supported Architectures

| ARCH | Assembly | Compiler | Notes |
|------|----------|----------|-------|
| `x86_64_gas` | GAS (GNU as) | gcc | Default. AT&T syntax. |
| `x86_64_nasm` | NASM | gcc | Intel syntax. Requires `nasm`. |
| `arm` | GAS (AArch64) | aarch64-linux-gnu-gcc | ARM64/Linux. **`ARCH=arm` selects AArch64**; sources live under `arch/arm/gas/` (historical directory name, not 32-bit ARM). Port I/O stubs (no x86 in/out). |

## Build

```bash
# Default: x86_64 GAS
make

# NASM (requires: apt install nasm)
make ARCH=x86_64_nasm

# ARM64 cross-compile (requires: apt install gcc-aarch64-linux-gnu)
make ARCH=arm
```

## Layout

```
arch/
  x86_64/
    gas/           # GAS/AT&T: mem_asm.s, port_io.s, alloc/*.s
    nasm/          # NASM: mem_asm.asm, port_io.asm, alloc/*.asm
  arm/
    gas/           # AArch64: mem_asm.s, port_io.s (stubs), alloc/*.s
```

## API Parity

All variants expose the same C-callable symbols:

- **mem_asm**: `asm_mem_copy`, `asm_mem_zero`, `asm_block_fill`, `asm_mem_secure_zero`, `asm_mem_ct_equal`. The KeyQuorum consumers (`kq_service.c`, `kq_mem_storage.c`, the key and seed buffers) reach them only through `mem_domain_copy` / `_zero` / `_secure_zero` / `_equal`, the same on x86_64 NASM, x86_64 GAS and ARM GAS; `strlen` and ordered compares stay libc (no memory primitive covers them)
- **port_io**: `port_inb`, `port_outb`, `port_inw`, `port_outw` (ARM: stubs return 0/no-op)
- **alloc** (USE_ASM_ALLOC=1): `malloc`, `calloc`, `realloc`, `free`

## Drivers

All architectures use the same drivers and unified I/O stack (see docs/IO_STACK.md):

- **block_driver**: host file I/O; bare-metal would use AHCI/NVMe (MMIO)
- **keyboard_driver**: host stdin / bare-metal port 0x60 (x86 PS/2)
- **display_driver**: host printf / bare-metal VGA (MMIO at 0xB8000)
- **timer_driver**: host sleep / bare-metal PIT (port 0x40)
- **pic_driver**: host no-op / bare-metal 8259 (port 0x20/0xA0)
- **pci**: config space; x86 uses port 0xCF8/0xCFC or ECAM; ARM uses ECAM

On ARM bare-metal, port_io is stubbed; MMIO and PCI (ECAM) work.

## Memory domains (`mem_domain`)

Recorded from source comments removed in PR #416 (2026-11).

`kernel/core/mm/mem_domain.[ch]` is the one allocator interface. Its behaviour depends on the build:

- **Host builds** wrap libc `malloc`, `calloc`, `realloc` and `free`.
- **Bare-metal drivers builds** (`DRIVERS_BAREMETAL`; `make baremetal-drivers` compiles these paths into the hosted program, which cannot boot; the bootable image is `make baremetal`, the same as `make browser-kernel`) give each domain a static arena in BSS: kernel 64 KiB,
  driver 64 KiB, file system 128 KiB, user 32 KiB. Allocation bumps a pointer; a freed block goes on a
  per-domain first-fit free list. Each block carries a `size_t` header, and every block is 8-byte aligned.
- A free of a block that is not inside its domain's arena spins forever. The free list is never linked
  with a foreign block, so heap corruption stops the machine rather than spreading.
- `mem_domain_secure_zero` is a zero followed by a barrier, so the stores finish before the buffer is freed or
  reused. Keys and passwords are freed this way.
- Buffer primitives (copy, zero, constant-time compare) always go through the assembly layer, in every mode.

The freestanding KeyQuorum image does not use these arenas. It has its own heap, described in
[FREESTANDING_BOOT.md](FREESTANDING_BOOT.md).

## Server shared file placement

`kernel/core/vfs/server_shared_fs.[ch]` is the placement layer under the server share, the KeyQuorum
checkpoints and delivery store, the lab and the local `fs_provider_t`. None of them writes files its own way.
The rules that matter when changing it:

| Operation | Rule |
|-----------|------|
| `fl_fs_read_file` | one open, size from `fstat`, one allocation, then an EOF probe, so a file that changed size is never returned truncated |
| read failure | `*data` is NULL and `*len` is 0; bytes already read are wiped |
| `fl_fs_write_file_atomic` | stages `<path>.part` and renames it over the path, so the old content stays whole if the write fails |
| `fl_fs_stage_file` | a leftover `.part` (or a link there) is removed first; a link is never written through |
| contained calls (`*_at`, `*_in`) | the directory is opened once; names are opened relative to its descriptor, and links are refused with `ELOOP` |
| contained calls without `*at` support | they fail with `ENOSYS`; `fl_fs_contained_available` says whether they work |
| `fl_fs_trusted_root_open` | the share root must not be a link, must be a directory and must be owned by the effective user (`EPERM` otherwise); its mode is set to 0700 on the held descriptor |
| `fl_fs_join_path` | an empty directory gives the name alone; a result that does not fit is `ENAMETOOLONG`, never truncated |
| path limits | `FL_FS_PATH_MAX` 4096 (including `.part`), `FL_FS_NAME_MAX` 255 |

Resolving a local path for writing (`fl_server_shared_resolve_local_path`):

- a path that is absolute, contains a backslash or has a `..` component is refused with `ACCES`;
- more components than the depth limit is `INVAL`;
- each directory component is opened by descriptor without following links; missing components are created
  owner-only when the caller allows it;
- the result is the open parent and its last component. The caller closes the descriptor.

`fl_server_shared_overwrite_local` replaces a local destination through the same contained walk. It refuses links
and non-regular files. The data is staged as a new 0600 file and renamed into place, so a failed write keeps the old
content and the result always has mode 0600. The old file's owner, ACL and extended attributes are not carried over.
The caller needs write access to an existing destination (`ACCES` otherwise).

The commit and reserved-name rules are in [KQ_STORAGE.md](KQ_STORAGE.md#landing-names-and-commits).
