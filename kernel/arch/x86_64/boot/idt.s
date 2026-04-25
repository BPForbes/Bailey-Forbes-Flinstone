/* x86_64 GAS — IDT with 256 entries and IRQ0 (PIT) handler
 *
 * Gate format: 64-bit interrupt gate, CS=0x08, DPL=0, present (type 0x8E).
 *
 * ISR stubs for vectors 0-47:
 *   Exceptions without error code: push 0 (dummy), push vector, jmp common.
 *   Exceptions with error code (8,10-14,17,29,30): push vector, jmp common.
 *   IRQ lines (32-47): push 0, push vector, jmp common.
 *
 * Vectors 48-255: filled with default_stub which just iretqs immediately.
 *
 * isr_common_stub:
 *   Saves caller-save regs, calls x86_idt_dispatch(vector) (C function in
 *   timer_driver.c / pic_driver.c), restores regs, iretqs.
 *
 * idt_install():
 *   Iterates all 256 vectors: 0-47 get their specific stub via a static
 *   address table; 48-255 get default_stub.  Loads IDTR via LIDT.
 */

.section .note.GNU-stack,"",@progbits

/* ------------------------------------------------------------------ */
/* IDT storage                                                         */
/* ------------------------------------------------------------------ */
.section .bss
.align 16
.globl idt_table
idt_table:
    .space 256 * 16

.section .data
.align 2
idt_ptr:
    .word 256 * 16 - 1
    .quad idt_table

/* ------------------------------------------------------------------ */
/* ISR stubs 0-47                                                      */
/* ------------------------------------------------------------------ */
.text

.macro ISR_NOERR num
.globl isr_stub_\num
isr_stub_\num:
    pushq $0
    pushq $\num
    jmp   isr_common_stub
.endm

.macro ISR_ERR num
.globl isr_stub_\num
isr_stub_\num:
    pushq $\num
    jmp   isr_common_stub
.endm

/* CPU exceptions */
ISR_NOERR 0;  ISR_NOERR 1;  ISR_NOERR 2;  ISR_NOERR 3
ISR_NOERR 4;  ISR_NOERR 5;  ISR_NOERR 6;  ISR_NOERR 7
ISR_ERR   8;  ISR_NOERR 9;  ISR_ERR   10; ISR_ERR   11
ISR_ERR   12; ISR_ERR   13; ISR_ERR   14; ISR_NOERR 15
ISR_NOERR 16; ISR_ERR   17; ISR_NOERR 18; ISR_NOERR 19
ISR_NOERR 20; ISR_NOERR 21; ISR_NOERR 22; ISR_NOERR 23
ISR_NOERR 24; ISR_NOERR 25; ISR_NOERR 26; ISR_NOERR 27
ISR_NOERR 28; ISR_ERR   29; ISR_ERR   30; ISR_NOERR 31
/* IRQ 0-15 (PIC-remapped to vectors 32-47) */
ISR_NOERR 32; ISR_NOERR 33; ISR_NOERR 34; ISR_NOERR 35
ISR_NOERR 36; ISR_NOERR 37; ISR_NOERR 38; ISR_NOERR 39
ISR_NOERR 40; ISR_NOERR 41; ISR_NOERR 42; ISR_NOERR 43
ISR_NOERR 44; ISR_NOERR 45; ISR_NOERR 46; ISR_NOERR 47

/* Default stub for vectors 48-255: just return from interrupt */
.globl default_stub
default_stub:
    iretq

/* ------------------------------------------------------------------ */
/* Address table for vectors 0-47                                      */
/* ------------------------------------------------------------------ */
.section .rodata
.align 8
stub_table:
    .quad isr_stub_0,  isr_stub_1,  isr_stub_2,  isr_stub_3
    .quad isr_stub_4,  isr_stub_5,  isr_stub_6,  isr_stub_7
    .quad isr_stub_8,  isr_stub_9,  isr_stub_10, isr_stub_11
    .quad isr_stub_12, isr_stub_13, isr_stub_14, isr_stub_15
    .quad isr_stub_16, isr_stub_17, isr_stub_18, isr_stub_19
    .quad isr_stub_20, isr_stub_21, isr_stub_22, isr_stub_23
    .quad isr_stub_24, isr_stub_25, isr_stub_26, isr_stub_27
    .quad isr_stub_28, isr_stub_29, isr_stub_30, isr_stub_31
    .quad isr_stub_32, isr_stub_33, isr_stub_34, isr_stub_35
    .quad isr_stub_36, isr_stub_37, isr_stub_38, isr_stub_39
    .quad isr_stub_40, isr_stub_41, isr_stub_42, isr_stub_43
    .quad isr_stub_44, isr_stub_45, isr_stub_46, isr_stub_47

/* ------------------------------------------------------------------ */
/* isr_common_stub                                                     */
/* Stack on entry:                                                     */
/*   [rsp+0 ] vector  (pushed by ISR macro)                           */
/*   [rsp+8 ] error code (0 or real)                                  */
/*   [rsp+16] CPU interrupt frame (rip, cs, rflags, rsp, ss)          */
/* ------------------------------------------------------------------ */
.text
isr_common_stub:
    pushq %rax
    pushq %rbx
    pushq %rcx
    pushq %rdx
    pushq %rsi
    pushq %rdi
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11

    /* vector is 10 quads (80 bytes) above current rsp */
    movq  80(%rsp), %rdi
    call  x86_idt_dispatch

    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rdi
    popq %rsi
    popq %rdx
    popq %rcx
    popq %rbx
    popq %rax

    addq $16, %rsp   /* discard vector + error code */
    iretq

/* ------------------------------------------------------------------ */
/* idt_set_gate helper                                                 */
/* rdi=vector  rsi=handler_addr                                       */
/* ------------------------------------------------------------------ */
idt_set_gate:
    movq  %rdi, %rax
    shlq  $4, %rax
    leaq  idt_table(%rip), %rcx
    addq  %rax, %rcx

    movw  %si, 0(%rcx)                /* offset[15:0] */
    movw  $0x08, 2(%rcx)              /* kernel CS */
    movb  $0x00, 4(%rcx)              /* IST=0 */
    movb  $0x8E, 5(%rcx)              /* P=1, DPL=0, type=0xE */
    movq  %rsi, %rax
    shrq  $16, %rax
    movw  %ax, 6(%rcx)               /* offset[31:16] */
    movq  %rsi, %rax
    shrq  $32, %rax
    movl  %eax, 8(%rcx)              /* offset[63:32] */
    movl  $0, 12(%rcx)               /* reserved */
    ret

/* ------------------------------------------------------------------ */
/* idt_install                                                         */
/* ------------------------------------------------------------------ */
.globl idt_install
idt_install:
    pushq %rbx
    pushq %r12

    /* Install vectors 0-47 from stub_table */
    xorl  %ebx, %ebx
    leaq  stub_table(%rip), %r12
.Lnamed_loop:
    movq  (%r12,%rbx,8), %rsi
    movq  %rbx, %rdi
    call  idt_set_gate
    incl  %ebx
    cmpl  $48, %ebx
    jl    .Lnamed_loop

    /* Install vectors 48-255 with default_stub */
    leaq  default_stub(%rip), %r12
.Ldefault_loop:
    movq  %r12, %rsi
    movq  %rbx, %rdi
    call  idt_set_gate
    incl  %ebx
    cmpl  $256, %ebx
    jl    .Ldefault_loop

    lidt  idt_ptr(%rip)   /* RIP-relative avoids R_X86_64_32S in PIE builds */

    popq  %r12
    popq  %rbx
    ret
