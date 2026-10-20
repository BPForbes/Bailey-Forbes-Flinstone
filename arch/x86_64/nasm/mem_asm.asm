; ASM memory primitives for disk buffer operations (x86-64 NASM)
; Same API as GAS version - SysV ABI: rdi, rsi, rdx, rcx, r8, r9
section .note.GNU-stack progbits noalloc noexec nowrite
section .text
global asm_mem_copy
global asm_mem_zero
global asm_block_fill
global asm_mem_secure_zero
global asm_mem_ct_equal

; void asm_mem_copy(void *dst, const void *src, size_t n)
asm_mem_copy:
    mov r8, rdx              ; n
    mov r9, rdx
    shr r9, 3                ; n/8
    test r9, r9
    jz .copy_bytes
.copy_qwords:
    mov r10, [rsi]
    mov [rdi], r10
    add rsi, 8
    add rdi, 8
    dec r9
    jnz .copy_qwords
.copy_bytes:
    and r8, 7
    test r8, r8
    jz .copy_done
.copy_byte_loop:
    mov r10b, [rsi]
    mov [rdi], r10b
    inc rsi
    inc rdi
    dec r8
    jnz .copy_byte_loop
.copy_done:
    ret

; void asm_mem_zero(void *ptr, size_t n)
asm_mem_zero:
    mov r8, rsi
    xor r10, r10
    mov r9, rsi
    shr r9, 3
    test r9, r9
    jz .zero_bytes
.zero_qwords:
    mov [rdi], r10
    add rdi, 8
    dec r9
    jnz .zero_qwords
.zero_bytes:
    and r8, 7
    test r8, r8
    jz .zero_done
.zero_byte_loop:
    mov byte [rdi], 0
    inc rdi
    dec r8
    jnz .zero_byte_loop
.zero_done:
    ret

; void asm_block_fill(void *ptr, unsigned char byte, size_t n)
asm_block_fill:
    movzx esi, sil           ; byte
    mov r8, rdx
    shr r8, 3
    test r8, r8
    jz .fill_bytes
    mov r10, rsi
    shl r10, 8
    or r10, rsi
    mov r11, r10
    shl r10, 16
    or r10, r11
    mov r11, r10
    shl r10, 32
    or r10, r11
.fill_qwords:
    mov [rdi], r10
    add rdi, 8
    dec r8
    jnz .fill_qwords
.fill_bytes:
    mov r8, rdx
    and r8, 7
    test r8, r8
    jz .fill_done
.fill_byte_loop:
    mov [rdi], sil
    inc rdi
    dec r8
    jnz .fill_byte_loop
.fill_done:
    ret

; void asm_mem_secure_zero(void *ptr, size_t n)
; Wipes a secret: extends asm_mem_zero (the parent does the stores) with mfence.
; The 8-byte adjustment keeps the stack 16-byte aligned across the call.
asm_mem_secure_zero:
    sub rsp, 8
    call asm_mem_zero
    add rsp, 8
    mfence
    ret

; int asm_mem_ct_equal(const void *a, const void *b, size_t n)
; 1 when equal, else 0. Constant time in n: no early exit.
asm_mem_ct_equal:
    xor eax, eax
    test rdx, rdx
    jz .cteq_done
.cteq_loop:
    movzx ecx, byte [rdi]
    xor cl, byte [rsi]
    or eax, ecx
    inc rdi
    inc rsi
    dec rdx
    jnz .cteq_loop
.cteq_done:
    test eax, eax
    sete al
    movzx eax, al
    ret
