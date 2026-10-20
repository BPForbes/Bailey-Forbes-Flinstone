/* ASM memory primitives for disk buffer operations (x86-64 GAS) */
.section .note.GNU-stack,"",@progbits
.text
.globl asm_mem_copy
.globl asm_mem_zero
.globl asm_block_fill
.globl asm_mem_secure_zero
.globl asm_mem_ct_equal

/* void asm_mem_copy(void *dst, const void *src, size_t n) */
asm_mem_copy:
    movq %rdi, %rax          /* dst */
    movq %rsi, %rcx          /* src */
    movq %rdx, %r8           /* n */
    movq %rdx, %r9
    shrq $3, %r9             /* n/8 for 64-bit copies */
    testq %r9, %r9
    jz .L_copy_bytes
.L_copy_qwords:
    movq (%rsi), %r10
    movq %r10, (%rdi)
    addq $8, %rsi
    addq $8, %rdi
    decq %r9
    jnz .L_copy_qwords
.L_copy_bytes:
    andq $7, %r8
    testq %r8, %r8
    jz .L_copy_done
.L_copy_byte_loop:
    movb (%rsi), %r10b
    movb %r10b, (%rdi)
    incq %rsi
    incq %rdi
    decq %r8
    jnz .L_copy_byte_loop
.L_copy_done:
    ret

/* void asm_mem_zero(void *ptr, size_t n) */
asm_mem_zero:
    movq %rsi, %r8           /* n */
    xorq %r10, %r10
    movq %rsi, %r9
    shrq $3, %r9
    testq %r9, %r9
    jz .L_zero_bytes
.L_zero_qwords:
    movq %r10, (%rdi)
    addq $8, %rdi
    decq %r9
    jnz .L_zero_qwords
.L_zero_bytes:
    andq $7, %r8
    testq %r8, %r8
    jz .L_zero_done
.L_zero_byte_loop:
    movb $0, (%rdi)
    incq %rdi
    decq %r8
    jnz .L_zero_byte_loop
.L_zero_done:
    ret

/* void asm_block_fill(void *ptr, unsigned char byte, size_t n) */
asm_block_fill:
    movzbl %sil, %esi       /* byte in low 8 bits */
    movq %rdx, %r8
    shrq $3, %r8
    testq %r8, %r8
    jz .L_fill_bytes
    movq %rsi, %r10         /* r10 = B */
    shlq $8, %r10
    orq %rsi, %r10          /* r10 = BB */
    movq %r10, %r11
    shlq $16, %r10
    orq %r11, %r10          /* r10 = BBBB */
    movq %r10, %r11
    shlq $32, %r10
    orq %r11, %r10          /* r10 = BBBBBBBB */
.L_fill_qwords:
    movq %r10, (%rdi)
    addq $8, %rdi
    decq %r8
    jnz .L_fill_qwords
.L_fill_bytes:
    movq %rdx, %r8
    andq $7, %r8
    testq %r8, %r8
    jz .L_fill_done
.L_fill_byte_loop:
    movb %sil, (%rdi)
    incq %rdi
    decq %r8
    jnz .L_fill_byte_loop
.L_fill_done:
    ret

/* void asm_mem_secure_zero(void *ptr, size_t n)
 * Wipes a secret. Extends asm_mem_zero (the parent does the stores; an opaque
 * call the compiler cannot elide) with the one thing a secret wipe adds: mfence,
 * so the zeros are globally visible before the caller frees or reuses the buffer.
 * The 8-byte adjustment keeps the stack 16-byte aligned across the call. */
asm_mem_secure_zero:
    subq $8, %rsp
    call asm_mem_zero
    addq $8, %rsp
    mfence
    ret

/* int asm_mem_ct_equal(const void *a, const void *b, size_t n)
 * 1 when the n bytes are equal, else 0. Constant time in n: every byte is
 * read and XOR-folded into an accumulator, with no early exit, so timing
 * says nothing about where two digests or signatures first differ. */
asm_mem_ct_equal:
    xorl %eax, %eax
    testq %rdx, %rdx
    jz .L_cteq_done
.L_cteq_loop:
    movzbl (%rdi), %ecx
    xorb (%rsi), %cl
    orl %ecx, %eax
    incq %rdi
    incq %rsi
    decq %rdx
    jnz .L_cteq_loop
.L_cteq_done:
    testl %eax, %eax
    sete %al
    movzbl %al, %eax
    ret
