/* ASM memory primitives for disk buffer operations (AArch64/GAS)
 * Same API as x86-64: asm_mem_copy(dst, src, n), asm_mem_zero(ptr, n), asm_block_fill(ptr, byte, n)
 * SysV ABI: x0=dst, x1=src, x2=n (asm_mem_copy); x0=ptr, x1=n (asm_mem_zero); x0=ptr, x1=byte, x2=n (asm_block_fill)
 */
.section .note.GNU-stack,"",@progbits
.text
.globl asm_mem_copy
.globl asm_mem_zero
.globl asm_block_fill

/* void asm_mem_copy(void *dst, const void *src, size_t n) */
asm_mem_copy:
    cbz     x2, .L_copy_done
    mov     x3, x2
    lsr     x4, x2, #3          /* n/8 */
    cbz     x4, .L_copy_bytes
.L_copy_qwords:
    ldr     x5, [x1], #8
    str     x5, [x0], #8
    subs    x4, x4, #1
    b.ne    .L_copy_qwords
.L_copy_bytes:
    and     x3, x3, #7
    cbz     x3, .L_copy_done
.L_copy_byte_loop:
    ldrb    w5, [x1], #1
    strb    w5, [x0], #1
    subs    x3, x3, #1
    b.ne    .L_copy_byte_loop
.L_copy_done:
    ret

/* void asm_mem_zero(void *ptr, size_t n) */
asm_mem_zero:
    cbz     x1, .L_zero_done
    mov     x3, x1
    lsr     x4, x1, #3
    cbz     x4, .L_zero_bytes
.L_zero_qwords:
    str     xzr, [x0], #8
    subs    x4, x4, #1
    b.ne    .L_zero_qwords
.L_zero_bytes:
    and     x3, x3, #7
    cbz     x3, .L_zero_done
.L_zero_byte_loop:
    strb    wzr, [x0], #1
    subs    x3, x3, #1
    b.ne    .L_zero_byte_loop
.L_zero_done:
    ret

/* void asm_block_fill(void *ptr, unsigned char byte, size_t n) */
asm_block_fill:
    cbz     x2, .L_fill_done
    and     w1, w1, #0xff        /* byte */
    dup     v0.8b, w1            /* replicate byte to 8 lanes */
    mov     x3, x2
    lsr     x4, x2, #3
    cbz     x4, .L_fill_bytes
.L_fill_qwords:
    str     d0, [x0], #8
    subs    x4, x4, #1
    b.ne    .L_fill_qwords
.L_fill_bytes:
    and     x3, x3, #7
    cbz     x3, .L_fill_done
.L_fill_byte_loop:
    strb    w1, [x0], #1
    subs    x3, x3, #1
    b.ne    .L_fill_byte_loop
.L_fill_done:
    ret
