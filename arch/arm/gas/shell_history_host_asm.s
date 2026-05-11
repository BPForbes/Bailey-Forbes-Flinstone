/* Host shell: append one command line + LF (AArch64 AAPCS64). */
.section .note.GNU-stack,"",@progbits
.text

.globl history_asm_append_record

/* size_t history_asm_append_record(char *buf, size_t cap, size_t used,
 *                                  const char *cmd, size_t cmd_len); */
history_asm_append_record:
    cmp     x2, x1                  /* if used > cap */
    b.hi    .L_overflow
    sub     x5, x1, x2              /* avail = cap - used */
    add     x6, x4, #1              /* need = cmd_len + 1 */
    cmp     x5, x6
    b.lo    .L_overflow

    add     x7, x0, x2              /* dst = buf + used */
    mov     x8, x7
    mov     x9, x3                  /* src = cmd */
    mov     x10, x4                 /* remaining */
.L_copy_byte:
    cbz     x10, .L_after_copy
    ldrb    w11, [x9], #1
    strb    w11, [x8], #1
    sub     x10, x10, #1
    b       .L_copy_byte
.L_after_copy:
    mov     w11, #10
    strb    w11, [x8]

    add     x0, x2, x4
    add     x0, x0, #1
    ret

.L_overflow:
    mov     x0, #-1
    ret
