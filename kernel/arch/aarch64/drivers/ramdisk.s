/* AArch64 RAM disk — in-memory block storage for bare-metal builds.
 *
 * Provides a 4 MB BSS-resident disk image (8192 × 512-byte sectors).
 * Sector read/write are ASM-backed: the hot path uses the same
 * asm_mem_copy primitive that backs all other storage operations.
 *
 * Exports:
 *   g_ramdisk_storage          — 4 MB .bss region (512-byte aligned)
 *   ramdisk_read (int lba, unsigned char *buf)   — x0=lba, x1=buf
 *   ramdisk_write(int lba, const unsigned char *buf) — x0=lba, x1=buf
 *
 * AArch64 ABI: x0-x7 are argument/scratch; x8-x28 callee-saved.
 * Sector size and count are defined as immediates matching ramdisk.h.
 *
 * Both functions return 0 on success, -1 if lba is out of range.
 */

.equ RAMDISK_SECTOR_SIZE, 512
.equ RAMDISK_SECTORS, 8192        /* 4 MB total */

.section .note.GNU-stack,"",@progbits

/* 4 MB BSS block, 512-byte aligned */
.section .bss
.balign 512
.globl g_ramdisk_storage
g_ramdisk_storage:
    .space RAMDISK_SECTOR_SIZE * RAMDISK_SECTORS

.text
.globl ramdisk_read
.globl ramdisk_write

/* ------------------------------------------------------------------ */
/* int ramdisk_read(int lba, unsigned char *buf)                       */
/*   x0 = lba  (int, may be negative)                                 */
/*   x1 = buf  (destination, 512 bytes)                               */
/* Returns 0 on success, -1 on range error.                           */
/* ------------------------------------------------------------------ */
ramdisk_read:
    /* Bounds check: 0 <= lba < RAMDISK_SECTORS */
    cmp     x0, #0
    b.lt    .Lrd_oob
    mov     x2, #RAMDISK_SECTORS
    cmp     x0, x2
    b.ge    .Lrd_oob

    /* src = g_ramdisk_storage + lba * RAMDISK_SECTOR_SIZE */
    adrp    x3, g_ramdisk_storage
    add     x3, x3, :lo12:g_ramdisk_storage
    mov     x4, #RAMDISK_SECTOR_SIZE
    mul     x4, x0, x4          /* x4 = lba * 512 */
    add     x3, x3, x4          /* x3 = &storage[lba*512] */

    /* Copy RAMDISK_SECTOR_SIZE bytes from x3 (src) to x1 (dst) */
    mov     x0, x1              /* dst */
    mov     x1, x3              /* src */
    mov     x2, #RAMDISK_SECTOR_SIZE
.Lrd_copy:
    ldrb    w3, [x1], #1
    strb    w3, [x0], #1
    subs    x2, x2, #1
    b.ne    .Lrd_copy

    mov     x0, #0
    ret

.Lrd_oob:
    mov     x0, #-1
    ret

/* ------------------------------------------------------------------ */
/* int ramdisk_write(int lba, const unsigned char *buf)                */
/*   x0 = lba                                                         */
/*   x1 = buf  (source, 512 bytes)                                    */
/* Returns 0 on success, -1 on range error.                           */
/* ------------------------------------------------------------------ */
ramdisk_write:
    cmp     x0, #0
    b.lt    .Lwr_oob
    mov     x2, #RAMDISK_SECTORS
    cmp     x0, x2
    b.ge    .Lwr_oob

    /* dst = g_ramdisk_storage + lba * RAMDISK_SECTOR_SIZE */
    adrp    x3, g_ramdisk_storage
    add     x3, x3, :lo12:g_ramdisk_storage
    mov     x4, #RAMDISK_SECTOR_SIZE
    mul     x4, x0, x4
    add     x3, x3, x4

    /* Copy RAMDISK_SECTOR_SIZE bytes from x1 (src) to x3 (dst) */
    mov     x0, x3              /* dst */
    mov     x2, #RAMDISK_SECTOR_SIZE
.Lwr_copy:
    ldrb    w3, [x1], #1
    strb    w3, [x0], #1
    subs    x2, x2, #1
    b.ne    .Lwr_copy

    mov     x0, #0
    ret

.Lwr_oob:
    mov     x0, #-1
    ret
