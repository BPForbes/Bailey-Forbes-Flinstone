/* P3 networking hot-path primitives (AArch64 GAS). */
.section .note.GNU-stack,"",@progbits
.text
.globl asm_net_checksum16
.globl asm_net_htons_be16
.globl asm_net_tcp_build_syn
.globl asm_net_tcp_build_rst_ack
.globl asm_net_tcp_read_ports_be

.equ FL_NET_TCP_HDR_LEN, 20
.equ FL_NET_TCP_FLAG_SYN, 0x02
.equ FL_NET_TCP_FLAG_RST, 0x04
.equ FL_NET_TCP_FLAG_ACK, 0x10

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

asm_net_htons_be16:
    rev16   w0, w0
    and     w0, w0, #0xffff
    ret

/* x0=buf x1=cap w2=sport w3=dport w4=seq */
asm_net_tcp_build_syn:
    cmp     x1, #FL_NET_TCP_HDR_LEN
    b.lo    .L_tcp_zero
    rev16   w5, w2
    strh    w5, [x0]
    rev16   w5, w3
    strh    w5, [x0, #2]
    rev     w5, w4
    str     w5, [x0, #4]
    str     xzr, [x0, #8]
    mov     w5, #0x50
    strb    w5, [x0, #12]
    mov     w5, #FL_NET_TCP_FLAG_SYN
    strb    w5, [x0, #13]
    mov     w5, #0x2000
    strh    w5, [x0, #14]
    mov     x0, #FL_NET_TCP_HDR_LEN
    ret
.L_tcp_zero:
    mov     x0, #0
    ret

/* x0=syn x1=syn_len x2=reply x3=cap */
asm_net_tcp_build_rst_ack:
    cmp     x1, #FL_NET_TCP_HDR_LEN
    b.lo    .L_tcp_zero
    cmp     x3, #FL_NET_TCP_HDR_LEN
    b.lo    .L_tcp_zero
    ldrh    w5, [x0, #2]
    rev16   w5, w5
    strh    w5, [x2]
    ldrh    w5, [x0]
    rev16   w5, w5
    strh    w5, [x2, #2]
    ldr     w5, [x0, #4]
    rev     w5, w5
    ldrb    w6, [x0, #13]
    tst     w6, #FL_NET_TCP_FLAG_SYN
    beq     .L_rst_seq_store
    add     w5, w5, #1
.L_rst_seq_store:
    rev     w5, w5
    str     w5, [x2, #8]
    str     xzr, [x2, #4]
    mov     w5, #0x50
    strb    w5, [x2, #12]
    mov     w5, #(FL_NET_TCP_FLAG_RST | FL_NET_TCP_FLAG_ACK)
    strb    w5, [x2, #13]
    strh    xzr, [x2, #14]
    mov     x0, #FL_NET_TCP_HDR_LEN
    ret

/* x0=tcp x1=len x2=sport_out x3=dport_out */
asm_net_tcp_read_ports_be:
    cmp     x1, #4
    b.lo    .L_ports_err
    ldrh    w4, [x0]
    rev16   w4, w4
    strh    w4, [x2]
    ldrh    w4, [x0, #2]
    rev16   w4, w4
    strh    w4, [x3]
    mov     w0, #0
    ret
.L_ports_err:
    mov     w0, #-1
    ret
