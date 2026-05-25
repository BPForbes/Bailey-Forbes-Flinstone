/* P3 networking hot-path primitives (AArch64 GAS). */
.section .note.GNU-stack,"",@progbits
.text
.globl asm_net_checksum16
.globl asm_net_htons_be16

/* uint16_t asm_net_checksum16(const void *data, size_t len); */
asm_net_checksum16:
    mov     w3, #0xffff
    cbz     x0, .L_csum_empty
    cbz     x1, .L_csum_empty
    mov     x4, x1
    mov     w2, #0
.L_csum_pair:
    cmp     x4, #1
    b.ls    .L_csum_odd
    ldrh    w5, [x0], #2
    rev16   w5, w5
    add     w2, w2, w5
    sub     x4, x4, #2
    b       .L_csum_pair
.L_csum_odd:
    cbz     x4, .L_csum_fold
    ldrb    w5, [x0]
    add     w2, w2, w5, lsl #8
.L_csum_fold:
    add     w5, w2, w2, lsr #16
    and     w2, w5, #0xffff
    mvn     w2, w2
    and     w0, w2, #0xffff
    ret
.L_csum_empty:
    mov     w0, w3
    ret

/* uint16_t asm_net_htons_be16(uint16_t host); */
asm_net_htons_be16:
    rev16   w0, w0
    and     w0, w0, #0xffff
    ret
