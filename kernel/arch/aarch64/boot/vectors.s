/* AArch64 EL1 exception vector table — ARMv8-A Architecture Reference §D1.10
 *
 * The VBAR_EL1 register must point to a 2 KB-aligned table of 16 entries.
 * Each entry is 128 bytes (32 instructions max).  The four groups are:
 *
 *   Group 0 (slots  0- 3): Current EL, SP_EL0
 *   Group 1 (slots  4- 7): Current EL, SP_EL1 (kernel-mode exceptions)
 *   Group 2 (slots  8-11): Lower EL, AArch64
 *   Group 3 (slots 12-15): Lower EL, AArch32
 *
 * Each slot stub:
 *   1. Saves x29 (frame pointer) and x30 (link register) — required so
 *      the bl to aarch64_exc_dispatch does not corrupt the interrupted LR.
 *   2. Passes the slot index (0-15) in x0 and calls aarch64_exc_dispatch.
 *   3. Restores x29/x30 and issues eret to resume the interrupted context.
 *      ELR_EL1 and SPSR_EL1 are not touched, so eret resumes correctly.
 *
 * arm_vbar_install():
 *   Computes the PC-relative address of arm_exc_vectors, writes it to
 *   VBAR_EL1, and issues ISB to ensure the new table is visible before
 *   the first exception can be taken.  Call after gic_init().
 */

.section .note.GNU-stack,"",@progbits

/* ------------------------------------------------------------------ */
/* Exception vector slot macro                                         */
/* ------------------------------------------------------------------ */
.macro EXC_SLOT num
    .balign 128
    stp  x29, x30, [sp, #-16]!
    mov  x0, #\num
    bl   aarch64_exc_dispatch
    ldp  x29, x30, [sp], #16
    eret
.endm

/* ------------------------------------------------------------------ */
/* 2 KB-aligned vector table                                           */
/* ------------------------------------------------------------------ */
.text
.balign 2048
.globl arm_exc_vectors
arm_exc_vectors:

    /* --- Group 0: Current EL, SP_EL0 --- */
    EXC_SLOT  0   /* Synchronous  */
    EXC_SLOT  1   /* IRQ          */
    EXC_SLOT  2   /* FIQ          */
    EXC_SLOT  3   /* SError       */

    /* --- Group 1: Current EL, SP_EL1 (kernel exceptions) --- */
    EXC_SLOT  4   /* Synchronous  */
    EXC_SLOT  5   /* IRQ          */
    EXC_SLOT  6   /* FIQ          */
    EXC_SLOT  7   /* SError       */

    /* --- Group 2: Lower EL, AArch64 --- */
    EXC_SLOT  8   /* Synchronous  */
    EXC_SLOT  9   /* IRQ          */
    EXC_SLOT 10   /* FIQ          */
    EXC_SLOT 11   /* SError       */

    /* --- Group 3: Lower EL, AArch32 --- */
    EXC_SLOT 12   /* Synchronous  */
    EXC_SLOT 13   /* IRQ          */
    EXC_SLOT 14   /* FIQ          */
    EXC_SLOT 15   /* SError       */

/* ------------------------------------------------------------------ */
/* arm_vbar_install                                                    */
/* Writes arm_exc_vectors to VBAR_EL1 and issues ISB.                 */
/* No arguments.  Preserves all registers per ABI (uses only x0).     */
/* ------------------------------------------------------------------ */
.globl arm_vbar_install
arm_vbar_install:
    adr  x0, arm_exc_vectors
    msr  vbar_el1, x0
    isb
    ret
