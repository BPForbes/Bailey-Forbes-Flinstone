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
 * Each slot stub branches to a shared trampoline that saves caller-saved GPRs,
 * passes the slot index (0-15) in x0, calls aarch64_exc_dispatch, restores the
 * interrupted context, and issues eret.  ELR_EL1 and SPSR_EL1 are not touched,
 * so eret resumes correctly.
 *
 * arm_vbar_install():
 *   Computes the PC-relative address of arm_exc_vectors, writes it to
 *   VBAR_EL1, and issues ISB to ensure the new table is visible before
 *   the first exception can be taken.  Call after gic_init().
 */

.section .note.GNU-stack,"",@progbits

/* ------------------------------------------------------------------ */
/* Exception vector slot macro                                         */
/*
 * IMPORTANT: The trampoline does NOT save ELR_EL1, SPSR_EL1, or SP_EL0.
 * The current design relies on eret to restore PSTATE and PC from these
 * system registers.  This is only safe if:
 *   - DAIF bits remain masked (no explicit DAIFCLR in exception context)
 *   - The C dispatcher (aarch64_exc_dispatch) and all registered handlers
 *     cannot take nested exceptions or cause synchronous faults.
 *
 * If nested exceptions are to be supported in future, the trampoline
 * (aarch64_exc_trampoline) MUST push ELR_EL1, SPSR_EL1 (and SP_EL0 when
 * resuming lower ELs) onto the exception frame before calling the dispatcher,
 * and restore them before eret.
 */
/* ------------------------------------------------------------------ */
.macro EXC_SLOT num
    .balign 128
    stp  x0, x1, [sp, #-16]!
    mov  x0, #\num
    b    aarch64_exc_trampoline
.endm

/* ------------------------------------------------------------------ */
/* 2 KB-aligned vector table                                           */
/* ------------------------------------------------------------------ */
.text
.balign 2048
.globl arm_exc_vectors
arm_exc_vectors:

    /* --- Group 0: Current EL, SP_EL0 --- */
    EXC_SLOT  0
    EXC_SLOT  1
    EXC_SLOT  2
    EXC_SLOT  3

    /* --- Group 1: Current EL, SP_EL1 (kernel exceptions) --- */
    EXC_SLOT  4
    EXC_SLOT  5
    EXC_SLOT  6
    EXC_SLOT  7

    /* --- Group 2: Lower EL, AArch64 --- */
    EXC_SLOT  8
    EXC_SLOT  9
    EXC_SLOT 10
    EXC_SLOT 11

    /* --- Group 3: Lower EL, AArch32 --- */
    EXC_SLOT 12
    EXC_SLOT 13
    EXC_SLOT 14
    EXC_SLOT 15
    .balign 128

/* ------------------------------------------------------------------ */
/* Shared exception trampoline                                         */
/*
 * FP/SIMD register preservation:
 * This trampoline saves only general-purpose registers (x0-x30).
 * FP/SIMD registers (q0-q31 / v0-v31) are NOT saved or restored.
 *
 * REQUIREMENT: The C dispatcher (aarch64_exc_dispatch) and all exception
 * handlers MUST be compiled with -mgeneral-regs-only to prevent the compiler
 * from emitting SIMD instructions that would clobber q0-q31.
 *
 * If FP/SIMD preservation is needed in future, modify this trampoline to
 * push/pop the full SIMD state (512 bytes for q0-q31) around the dispatcher
 * call, ensuring 16-byte stack alignment.
 */
/* ------------------------------------------------------------------ */
.balign 16
aarch64_exc_trampoline:
    stp  x2,  x3,  [sp, #-16]!
    stp  x4,  x5,  [sp, #-16]!
    stp  x6,  x7,  [sp, #-16]!
    stp  x8,  x9,  [sp, #-16]!
    stp  x10, x11, [sp, #-16]!
    stp  x12, x13, [sp, #-16]!
    stp  x14, x15, [sp, #-16]!
    stp  x16, x17, [sp, #-16]!
    stp  x18, x19, [sp, #-16]!
    stp  x29, x30, [sp, #-16]!

    bl   aarch64_exc_dispatch

    ldp  x29, x30, [sp], #16
    ldp  x18, x19, [sp], #16
    ldp  x16, x17, [sp], #16
    ldp  x14, x15, [sp], #16
    ldp  x12, x13, [sp], #16
    ldp  x10, x11, [sp], #16
    ldp  x8,  x9,  [sp], #16
    ldp  x6,  x7,  [sp], #16
    ldp  x4,  x5,  [sp], #16
    ldp  x2,  x3,  [sp], #16
    ldp  x0,  x1,  [sp], #16
    eret

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
