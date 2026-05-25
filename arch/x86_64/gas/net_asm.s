/* P3 networking hot-path primitives (x86-64 GAS). */
.section .note.GNU-stack,"",@progbits
.text
.globl asm_net_checksum16
.globl asm_net_htons_be16

/* uint16_t asm_net_checksum16(const void *data, size_t len); */
asm_net_checksum16:
    xorl    %eax, %eax
    testq   %rsi, %rsi
    jz      .L_csum_empty
    testq   %rdx, %rdx
    jz      .L_csum_empty
    movq    %rdx, %r8
.L_csum_pair:
    cmpq    $1, %r8
    jbe     .L_csum_odd
    movzwl  (%rdi), %ecx
    rolw    $8, %cx
    addl    %ecx, %eax
    addq    $2, %rdi
    subq    $2, %r8
    jmp     .L_csum_pair
.L_csum_odd:
    testq   %r8, %r8
    jz      .L_csum_fold
    movzbl  (%rdi), %ecx
    shll    $8, %ecx
    addl    %ecx, %eax
.L_csum_fold:
    movl    %eax, %ecx
    shrl    $16, %eax
    addl    %ecx, %eax
    movl    %eax, %ecx
    shrl    $16, %eax
    addl    %ecx, %eax
    andl    $0xffff, %eax
    notw    %ax
    ret
.L_csum_empty:
    movw    $0xffff, %ax
    ret

/* uint16_t asm_net_htons_be16(uint16_t host); */
asm_net_htons_be16:
    movzwl  %di, %eax
    rolw    $8, %ax
    ret
