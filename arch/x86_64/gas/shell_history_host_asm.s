/* Host shell: append one command line + LF using rep movsb (x86-64 System V ABI). */
.section .note.GNU-stack,"",@progbits
.text

.globl history_asm_append_record

/* size_t history_asm_append_record(char *buf, size_t cap, size_t used,
 *                                  const char *cmd, size_t cmd_len); */
history_asm_append_record:
    movq %rdx, %rax          /* used */
    cmpq %rsi, %rax          /* if used > cap */
    ja .L_overflow
    movq %rsi, %r9           /* cap */
    subq %rax, %r9           /* r9 = cap - used (available) */
    movq %r8, %r10
    cmpq $-1, %r8
    je .L_overflow
    incq %r10                /* need = cmd_len + 1 */
    cmpq %r10, %r9
    jb .L_overflow

    movq %rdi, %r11
    addq %rax, %r11          /* r11 = dst = buf + used */

    movq %r11, %rdi          /* dst for movsb */
    movq %rcx, %rsi          /* src = cmd */
    movq %r8, %rcx           /* count = cmd_len */
    cld
    rep movsb

    movb $10, (%rdi)         /* '\n' after command bytes */

    movq %rdx, %rax          /* used */
    addq %r8, %rax           /* + cmd_len */
    incq %rax                /* + newline */
    ret

.L_overflow:
    movq $-1, %rax
    ret
