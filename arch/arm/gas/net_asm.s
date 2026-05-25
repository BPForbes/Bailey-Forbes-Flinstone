/* P3 networking hot-path primitives (AArch64 GAS). */
.section .note.GNU-stack,"",@progbits
.text
.globl asm_net_checksum16
.globl asm_net_htons_be16
.globl asm_net_tcp_build_syn
.globl asm_net_tcp_build_rst_ack
.globl asm_net_icmp_echo_request_build
.globl asm_net_icmp_echo_reply_match
.globl asm_net_pseudo_header_fill12
.globl asm_net_pseudo_checksum_tcpudp
.globl asm_net_dns_query_header_prefix
.globl asm_net_tcp_read_ports_be

.equ FL_NET_TCP_HDR_LEN, 20
.equ FL_NET_TCP_FLAG_SYN, 0x02
.equ FL_NET_TCP_FLAG_RST, 0x04
.equ FL_NET_TCP_FLAG_ACK, 0x10

.equ FL_NET_ICMPV4_HDR_MIN, 8
.equ FL_NET_ICMPV4_TYPE_ECHO, 8
.equ FL_NET_ICMPV4_TYPE_ECHO_REPLY, 0
.equ FL_NET_ICMP_ECHO_FILL, 0x5a

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
    strh    wzr, [x2, #14]
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

/* x0=buf x1=cap w2=id w3=seq x4=payload_len */
asm_net_icmp_echo_request_build:
    stp     x19, x30, [sp, #-16]!
    cbz     x0, .L_icmp_zero_pop
    mov     x19, x0
    add     x5, x4, #FL_NET_ICMPV4_HDR_MIN
    cmp     x1, x5
    b.lo    .L_icmp_zero_pop
    mov     w6, #FL_NET_ICMPV4_TYPE_ECHO
    strb    w6, [x19]
    strb    wzr, [x19, #1]
    strh    wzr, [x19, #2]
    rev16   w6, w2
    strh    w6, [x19, #4]
    rev16   w6, w3
    strh    w6, [x19, #6]
    cbz     x4, .L_icmp_csum
    add     x0, x19, #FL_NET_ICMPV4_HDR_MIN
    mov     w1, #FL_NET_ICMP_ECHO_FILL
    mov     x2, x4
1:  strb    w1, [x0], #1
    subs    x2, x2, #1
    b.ne    1b
.L_icmp_csum:
    mov     x0, x19
    mov     x1, x5
    bl      asm_net_checksum16
    and     w6, w0, #0xff
    strb    w6, [x19, #3]
    ubfx    w6, w0, #8, #8
    strb    w6, [x19, #2]
    mov     x0, x5
    ldp     x19, x30, [sp], #16
    ret
.L_icmp_zero_pop:
    ldp     x19, x30, [sp], #16
.L_icmp_zero:
    mov     x0, #0
    ret

/* x0=buf x1=len w2=id w3=seq */
asm_net_icmp_echo_reply_match:
    cbz     x0, .L_icmp_match_fail
    cmp     x1, #FL_NET_ICMPV4_HDR_MIN
    b.lo    .L_icmp_match_fail
    ldrb    w4, [x0]
    cbnz    w4, .L_icmp_match_fail
    ldrh    w4, [x0, #4]
    rev16   w4, w4
    cmp     w4, w2
    b.ne    .L_icmp_match_fail
    ldrh    w4, [x0, #6]
    rev16   w4, w4
    cmp     w4, w3
    b.ne    .L_icmp_match_fail
    mov     w0, #1
    ret
.L_icmp_match_fail:
    mov     w0, #0
    ret

/* x0=pseudo w1=src w2=dst w3=proto x4=seg_len */
asm_net_pseudo_header_fill12:
    strb    w1, [x0]
    ubfx    w5, w1, #8, #8
    strb    w5, [x0, #1]
    ubfx    w5, w1, #16, #8
    strb    w5, [x0, #2]
    ubfx    w5, w1, #24, #8
    strb    w5, [x0, #3]
    strb    w2, [x0, #4]
    ubfx    w5, w2, #8, #8
    strb    w5, [x0, #5]
    ubfx    w5, w2, #16, #8
    strb    w5, [x0, #6]
    ubfx    w5, w2, #24, #8
    strb    w5, [x0, #7]
    strb    wzr, [x0, #8]
    strb    w3, [x0, #9]
    ubfx    w5, w4, #8, #8
    strb    w5, [x0, #10]
    and     w5, w4, #0xff
    strb    w5, [x0, #11]
    ret

/* w0=src w1=dst w2=proto x3=seg x4=seg_len */
asm_net_pseudo_checksum_tcpudp:
    stp     x29, x30, [sp, #-64]!
    mov     x29, sp
    stp     x19, x20, [sp, #16]
    stp     x21, x22, [sp, #32]
    stp     x23, x24, [sp, #48]
    mov     w19, w0
    mov     w20, w1
    mov     w21, w2
    mov     x22, x3
    mov     x23, x4
    add     x0, sp, #4
    mov     w1, w19
    mov     w2, w20
    mov     w3, w21
    mov     x4, x23
    bl      asm_net_pseudo_header_fill12
    mov     w0, #0
    add     x6, sp, #4
    mov     x0, x6
    mov     x1, #12
    bl      .L_net_accum16_be
    cbz     x22, .L_pseudo_fold
    cbz     x23, .L_pseudo_fold
    mov     x0, x22
    mov     x1, x23
    bl      .L_net_accum16_be
.L_pseudo_fold:
    add     w1, w0, w0, lsr #16
    and     w0, w1, #0xffff
    mvn     w0, w0
    and     w0, w0, #0xffff
    ldp     x23, x24, [sp, #48]
    ldp     x21, x22, [sp, #32]
    ldp     x19, x20, [sp, #16]
    ldp     x29, x30, [sp], #64
    ret

.L_net_accum16_be:
    mov     w2, #0
    cbz     x1, .L_accum16_done
.L_accum16_pair:
    cmp     x1, #1
    b.ls    .L_accum16_odd
    ldrh    w3, [x0], #2
    rev16   w3, w3
    add     w2, w2, w3
    sub     x1, x1, #2
    b       .L_accum16_pair
.L_accum16_odd:
    cbz     x1, .L_accum16_done
    ldrb    w3, [x0]
    add     w2, w2, w3, lsl #8
.L_accum16_done:
    add     w0, w0, w2
    ret

/* x0=query w1=txid */
asm_net_dns_query_header_prefix:
    rev16   w1, w1
    strh    w1, [x0]
    mov     w2, #0x01
    strb    w2, [x0, #2]
    strb    wzr, [x0, #3]
    strb    wzr, [x0, #4]
    strb    w2, [x0, #5]
    ret
