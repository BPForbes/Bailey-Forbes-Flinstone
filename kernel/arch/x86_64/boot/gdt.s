/* x86_64 GAS — minimal flat GDT
 *
 * Three descriptors (24 bytes):
 *   [0x00] null descriptor
 *   [0x08] kernel code  CS=0x08  (64-bit, DPL=0, execute/read)
 *   [0x10] kernel data  DS=0x10  (64-bit, DPL=0, read/write)
 *
 * gdt_install():
 *   1. Loads the GDT via LGDT.
 *   2. Reloads DS/ES/FS/GS/SS with 0x10.
 *   3. Far-returns to reload CS with 0x08 — required to activate the new
 *      code descriptor on x86_64 (MOV CR0 or MOV CS is not enough; a far
 *      branch is mandatory).
 *
 * System V AMD64 ABI: no arguments; returns void; preserves rbx, rbp, r12-r15.
 */

.section .note.GNU-stack,"",@progbits

/* ------------------------------------------------------------------ */
/* GDT data                                                            */
/* ------------------------------------------------------------------ */
.section .rodata
.align 8

gdt_table:
    /* [0x00] null descriptor */
    .quad 0x0000000000000000

    /* [0x08] kernel 64-bit code: base=0, limit=0xFFFFF, G=1, L=1, P=1, DPL=0, type=0xA */
    /* Encoding: limit[15:0]=0xFFFF, base[23:0]=0, type/S/DPL/P=0x9A, limit[19:16]/flags=0xAF, base[31:24]=0 */
    .quad 0x00AF9A000000FFFF

    /* [0x10] kernel 64-bit data: base=0, limit=0xFFFFF, G=1, P=1, DPL=0, type=0x2 */
    .quad 0x00AF92000000FFFF

gdt_end:

gdt_ptr:
    .word gdt_end - gdt_table - 1   /* limit */
    .quad gdt_table                  /* base  */

/* ------------------------------------------------------------------ */
/* gdt_install                                                         */
/* ------------------------------------------------------------------ */
.text
.globl gdt_install

gdt_install:
    lgdt    gdt_ptr(%rip)        /* RIP-relative avoids R_X86_64_32S in PIE builds */

    /* Reload data segment registers */
    movw    $0x10, %ax
    movw    %ax,   %ds
    movw    %ax,   %es
    movw    %ax,   %fs
    movw    %ax,   %gs
    movw    %ax,   %ss

    /* Far return to reload CS=0x08 (pushes CS:RIP onto stack, then retfq) */
    popq    %rax                 /* save return address */
    pushq   $0x08               /* new CS */
    pushq   %rax                /* return RIP */
    lretq
