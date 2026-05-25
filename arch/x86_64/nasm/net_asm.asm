; P3 networking hot-path primitives (x86-64 NASM).
section .note.GNU-stack progbits noalloc noexec nowrite
section .text

global asm_net_checksum16
global asm_net_htons_be16

; uint16_t asm_net_checksum16(const void *data, size_t len);
asm_net_checksum16:
    xor eax, eax
    test rsi, rsi
    jz .L_csum_empty
    test rdx, rdx
    jz .L_csum_empty
    mov r8, rdx
.L_csum_pair:
    cmp r8, 1
    jbe .L_csum_odd
    movzx ecx, word [rdi]
    rol cx, 8
    add eax, ecx
    add rdi, 2
    sub r8, 2
    jmp .L_csum_pair
.L_csum_odd:
    test r8, r8
    jz .L_csum_fold
    movzx ecx, byte [rdi]
    shl ecx, 8
    add eax, ecx
.L_csum_fold:
    mov ecx, eax
    shr eax, 16
    add eax, ecx
    mov ecx, eax
    shr eax, 16
    add eax, ecx
    and eax, 0xffff
    not ax
    ret
.L_csum_empty:
    mov ax, 0xffff
    ret

; uint16_t asm_net_htons_be16(uint16_t host);
asm_net_htons_be16:
    movzx eax, di
    rol ax, 8
    ret
