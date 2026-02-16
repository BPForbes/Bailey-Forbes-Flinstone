/* ASM memory primitives for disk buffer operations (ARM64) */
.section .note.GNU-stack,"",%progbits
.text
.globl asm_mem_copy
.globl asm_mem_zero
.globl asm_block_fill

/* void asm_mem_copy(void *dst, const void *src, size_t n) */
/* x0 = dst, x1 = src, x2 = n */
asm_mem_copy:
    mov x3, x0          /* dst */
    mov x4, x1          /* src */
    mov x5, x2          /* n */
    lsr x6, x5, #3      /* n/8 for 64-bit copies */
    cbz x6, .L_copy_bytes
.L_copy_qwords:
    ldr x7, [x4], #8    /* load and post-increment */
    str x7, [x3], #8    /* store and post-increment */
    sub x6, x6, #1
    cbnz x6, .L_copy_qwords
.L_copy_bytes:
    and x5, x5, #7      /* n % 8 */
    cbz x5, .L_copy_done
.L_copy_byte_loop:
    ldrb w7, [x4], #1   /* load byte and post-increment */
    strb w7, [x3], #1   /* store byte and post-increment */
    sub x5, x5, #1
    cbnz x5, .L_copy_byte_loop
.L_copy_done:
    ret

/* void asm_mem_zero(void *ptr, size_t n) */
/* x0 = ptr, x1 = n */
asm_mem_zero:
    mov x2, x1          /* n */
    mov x3, xzr         /* zero register */
    lsr x4, x2, #3      /* n/8 */
    cbz x4, .L_zero_bytes
.L_zero_qwords:
    str x3, [x0], #8    /* store zero and post-increment */
    sub x4, x4, #1
    cbnz x4, .L_zero_qwords
.L_zero_bytes:
    and x2, x2, #7      /* n % 8 */
    cbz x2, .L_zero_done
.L_zero_byte_loop:
    strb wzr, [x0], #1  /* store zero byte and post-increment */
    sub x2, x2, #1
    cbnz x2, .L_zero_byte_loop
.L_zero_done:
    ret

/* void asm_block_fill(void *ptr, unsigned char byte, size_t n) */
/* x0 = ptr, x1 = byte, x2 = n */
asm_block_fill:
    uxtb w1, w1         /* zero-extend byte to 32-bit */
    mov x3, x2          /* n */
    lsr x4, x3, #3      /* n/8 */
    cbz x4, .L_fill_bytes
    /* Replicate byte to 64-bit: BBBBBBBB */
    mov x5, x1          /* x5 = B */
    lsl x5, x5, #8
    orr x5, x5, x1      /* x5 = BB */
    mov x6, x5
    lsl x5, x5, #16
    orr x5, x5, x6      /* x5 = BBBB */
    mov x6, x5
    lsl x5, x5, #32
    orr x5, x5, x6      /* x5 = BBBBBBBB */
.L_fill_qwords:
    str x5, [x0], #8    /* store and post-increment */
    sub x4, x4, #1
    cbnz x4, .L_fill_qwords
.L_fill_bytes:
    and x3, x3, #7      /* n % 8 */
    cbz x3, .L_fill_done
.L_fill_byte_loop:
    strb w1, [x0], #1   /* store byte and post-increment */
    sub x3, x3, #1
    cbnz x3, .L_fill_byte_loop
.L_fill_done:
    ret
