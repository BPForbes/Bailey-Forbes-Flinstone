# Freestanding boot and the KeyQuorum embedder

Recorded from source comments removed in PR #416 (2026-11).

This page covers the boot path of the freestanding x86-64 image (`kernel/freestanding/x86_64/`) as far as the
KeyQuorum build depends on it: the loader, the handoff, the heap, the platform hooks and entropy. The lab
side is in [browser-kernel-lab.md](browser-kernel-lab.md) and [LAB_KQ_SHELL.md](LAB_KQ_SHELL.md).

## Stage 2 loader (`stage2.asm`)

- The loader reads the kernel with BIOS extended reads. Each read is at most 127 sectors, so it stays inside one
  64 KiB real-mode segment with no wraparound. The destination advances after each read by the paragraphs the chunk
  filled.
- The total sector count is patched into the image after linking, by `build_freestanding_browser_kernel.sh`. The
  patch site is found by its tag (`FLS2PTCH`) and the total and kernel-end fields after it. The count must be non-zero and no
  larger than `KERNEL_MAX_SECTORS`.
- `KERNEL_MAX_SECTORS` is `(FL_LOW_BUDGET - FL_KERNEL_LOAD_ADDR) / 512`. `FL_LOW_BUDGET` is `0x9f000`, the top
  of the low memory the loaded image (code, read-only data, data) may use.
- The bss is never read from disk. `linker.ld` places it at `FL_KERNEL_BSS_ADDR` (`0x100000`), so it costs no
  conventional memory. The kernel end, which is patched in, is the bss end: it must lie above 1 MiB and inside the
  identity map, and one usable E820 entry must hold all of `[FL_KERNEL_BSS_ADDR, kernel end)`, because `entry.asm`
  zeroes the bss before any C check runs. Otherwise the loader stops (`FLINTSTONE_BOOT_FAIL kernel-size` or
  `FLINTSTONE_BOOT_FAIL bss-memory`).
- The boot info reserves the loaded image and the bss as two ranges, so the KeyQuorum heap starts past the bss.
- The kernel stack is `FL_KERNEL_STACK_BOTTOM` (`0xa000`) to `FL_KERNEL_STACK_TOP` (`0x10000`), 24 KiB with no guard page: it
  starts above stage 2, which is dead once the kernel runs. The KeyQuorum history checkpoints and the Rust verification under
  them run on it; the boot self-test peaks near 12 KiB, measured under QEMU by filling the stack with a marker.
- Stage 2 itself is `FL_STAGE2_SECTORS` (8) sectors read from LBA 1.

## Boot handoff and the KeyQuorum heap (`kernel.c`, `bootmap.c`, `kq_heap.[ch]`)

- The loader's handoff is validated first (`fl_bootinfo_validate`), against the kernel's start, the end of its loaded
  bytes (`__image_end`), and its bss: every loaded byte read and below the bss, both ranges reserved, and the bss
  usable RAM in the E820 map. A failure stops with `FLINTSTONE_BOOT_FAIL bootinfo`.
- The KeyQuorum heap is the largest E820 span above 1 MiB, inside the identity map and clear of every reserved range.
  `fl_bootmap_select_arena` makes the choice with a minimum of `FL_KQ_HEAP_MIN` (40 MiB) and 4096-byte alignment.
  Too little memory stops the boot with `FLINTSTONE_BOOT_FAIL memory`. There is no fallback.
- `fl_kq_heap_init` takes the arena once. It refuses a base that is not 16-byte aligned, a size below the minimum, a
  size that wraps, and a second call.
- The heap is a first-fit free list with coalescing. Allocations are 16-byte aligned (`FL_KQ_HEAP_ALIGN`). Each block
  has a header with its size, `used` flag and next pointer.
- A freed block is zeroed with `asm_mem_secure_zero` before it returns to the list, so freed key material does not
  stay behind. `fl_kq_heap_usable` reports the bytes available behind a pointer, which is what `realloc` uses.

## Platform hooks (`kq_platform.c`)

The image provides the four embedder hooks of `keyquorum_identity.h`, the `mem_domain_*` allocators the driver calls,
a UTC clock, and the libc symbols the compiler and the Rust archive expect under `-nostdlib`. The symbols are built
with `-fno-builtin` and `-fno-tree-loop-distribute-patterns`, so none of them calls itself. `rep movsb` and `stosb`
are used for the byte primitives, which needs no SSE. For the KeyQuorum checkpoints (FL-02 stage 2) the file also defines
`strcmp`, `strnlen`, `strrchr`, `strtoul` and a small `vsnprintf`/`snprintf` (`%s %c %d %i %u %x %X`, the `-` and `0` flags, width,
precision, `l`/`ll`/`z`; any other conversion returns -1).

| Hook | Behaviour |
|------|-----------|
| `kq_platform_alloc`, `kq_platform_free` | the KeyQuorum heap; `free` ignores its length and alignment arguments |
| `kq_platform_random` | fills the buffer from RDRAND, else from fw_cfg; returns non-zero when neither can supply the bytes (fail closed) |
| `kq_platform_panic` | prints `FLINTSTONE_KQ_PANIC` and halts for good (`cli; hlt` loop) |
| `mem_domain_*` | the same heap; `mem_domain_secure_zero` is `asm_mem_secure_zero`; `mem_domain_equal` is constant time |

A build that sets `FL_SECFEAT` (the security-features image) has no fw_cfg path. Its entropy is RDRAND only. That was
a size limit while the bss lived below `0x9f000`; since the bss moved above 1 MiB it is not, but the image is unchanged.

## Entropy

Entropy order, checked on every call:

1. **RDRAND**, when CPUID leaf 1 reports it (ECX bit 30). Each 8-byte word is tried up to 10 times. A word that never
   reports success fails the call.
2. **fw_cfg** (`opt/flintstone/entropy`), only without RDRAND. Each byte is handed out once; the cursor only moves
   forward. Running out, or a missing file or device, fails the call.

A failed call returns non-zero to KeyQuorum, which fails closed. The image never falls back to a predictable source.
The browser worker's part of this setup is in [browser-kernel-lab.md](browser-kernel-lab.md).

## Clock

The UTC clock comes from the CMOS real-time clock. The code waits for the update-in-progress flag to
clear, converts BCD if the RTC is in BCD mode, and converts the 12-hour form to 24-hour before it computes days from
the civil date. `fl_fs_utc_text` turns it back into KeyQuorum's revision timestamp (`YYYY-MM-DDTHH:MM:SS.000Z`) for the
signed file histories.

## Names in the freestanding identity

`kernel/freestanding/x86_64/identity.c` matches account names regardless of case, as the hosted shell does (`str_eq_ci`).
The canonical spellings are `Root`, the master, and `Flinstone`, the default user. This identity has no key ids, keys
or bridges (see [IDENTITY_HERITAGE.md](IDENTITY_HERITAGE.md#not-yet-covered)).

## Tests that pin these

`tests/test_fs_bootmap.c` covers the heap's refusals (misaligned, too small, wrapping, second init), the arena
selection, and the handoff checks, including a bss below 1 MiB, a loader that wrote into the bss, a bss that is not
reserved, and a bss that is not all usable RAM. Booting with 1 MiB of RAM stops at `FLINTSTONE_BOOT_FAIL bss-memory`. Other behaviour is covered by the lab build and the image's boot gate
(see [browser-kernel-lab.md](browser-kernel-lab.md#boot-success-contract)).
