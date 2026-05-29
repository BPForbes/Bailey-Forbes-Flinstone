/* P3 networking hot-path primitives (AArch64 GAS). */
.section .note.GNU-stack,"",@progbits
.text
.globl asm_net_checksum16
.globl asm_net_htons_be16
.globl asm_net_ntohs_be16
.globl asm_net_htonl_be32
.globl asm_net_ntohl_be32
.globl asm_net_tcp_build_syn
.globl asm_net_tcp_build_rst_ack
.globl asm_net_icmp_echo_request_build
.globl asm_net_icmp_echo_reply_match
.globl asm_net_pseudo_header_fill12
.globl asm_net_pseudo_checksum_tcpudp
.globl asm_net_dns_query_header_prefix
.globl asm_net_tcp_read_ports_be
.globl asm_net_arp_cache_clear
.globl asm_net_arp_cache_lookup
.globl asm_net_arp_cache_insert
.globl asm_net_arp_cache_evict_oldest

.equ FL_NET_ARP_CACHE_ENTRY_STRIDE, 16
.equ FL_NET_ARP_CACHE_OFF_IP, 0
.equ FL_NET_ARP_CACHE_OFF_MAC, 4
.equ FL_NET_ARP_CACHE_OFF_AGE, 12

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
    add     w5, w5, w5, lsr #16
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

/* uint16_t asm_net_ntohs_be16(uint16_t net); — alias on rev-style hw. */
asm_net_ntohs_be16:
    rev16   w0, w0
    and     w0, w0, #0xffff
    ret

/* uint32_t asm_net_htonl_be32(uint32_t host); */
asm_net_htonl_be32:
    rev     w0, w0
    ret

/* uint32_t asm_net_ntohl_be32(uint32_t net); — alias of htonl on rev hw. */
asm_net_ntohl_be32:
    rev     w0, w0
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
    rev16   w5, w5
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
    add     w1, w1, w1, lsr #16
    and     w0, w1, #0xffff
    mvn     w0, w0
    and     w0, w0, #0xffff
    ldp     x23, x24, [sp, #48]
    ldp     x21, x22, [sp, #32]
    ldp     x19, x20, [sp, #16]
    ldp     x29, x30, [sp], #64
    ret

.L_net_accum16_be:
    mov     w4, w0
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
    add     w0, w4, w2
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

/* x0=table x1=max x2=count x3=tick */
asm_net_arp_cache_clear:
    cbz     x0, .L_arp_clr_done
    cbz     x2, .L_arp_clr_done
    cbz     x3, .L_arp_clr_done
    str     wzr, [x2]
    str     wzr, [x3]
    cbz     x1, .L_arp_clr_done
    mov     x4, x0
    add     x5, x0, x1, lsl #4
.L_arp_clr_row:
    cmp     x4, x5
    b.hs    .L_arp_clr_done
    str     xzr, [x4]
    str     xzr, [x4, #8]
    add     x4, x4, #FL_NET_ARP_CACHE_ENTRY_STRIDE
    b       .L_arp_clr_row
.L_arp_clr_done:
    ret

/* x0=table w1=count w2=ip x3=mac_out x4=tick -> 0/1 */
asm_net_arp_cache_lookup:
    mov     w6, #0
    cbz     x0, .L_arp_lk_fail
    cbz     x3, .L_arp_lk_fail
    cbz     x4, .L_arp_lk_fail
.L_arp_lk_loop:
    cmp     w6, w1
    b.hs    .L_arp_lk_fail
    add     x5, x0, x6, lsl #4
    ldr     w7, [x5, #FL_NET_ARP_CACHE_OFF_IP]
    cmp     w7, w2
    b.ne    .L_arp_lk_next
    ldr     w7, [x5, #FL_NET_ARP_CACHE_OFF_MAC]
    str     w7, [x3]
    ldrh    w7, [x5, #FL_NET_ARP_CACHE_OFF_MAC + 4]
    strh    w7, [x3, #4]
    ldrb    w7, [x5, #FL_NET_ARP_CACHE_OFF_MAC + 5]
    strb    w7, [x3, #5]
    ldr     w7, [x4]
    add     w7, w7, #1
    str     w7, [x4]
    str     w7, [x5, #FL_NET_ARP_CACHE_OFF_AGE]
    mov     w0, #1
    ret
.L_arp_lk_next:
    add     w6, w6, #1
    b       .L_arp_lk_loop
.L_arp_lk_fail:
    mov     w0, #0
    ret

/* x0=table x1=count */
asm_net_arp_cache_evict_oldest:
    cbz     x1, .L_arp_ev_done
    ldr     w2, [x1]
    cbz     w2, .L_arp_ev_done
    cmp     w2, #1
    b.eq    .L_arp_ev_one
    mov     w8, #0
    ldr     w9, [x0, #FL_NET_ARP_CACHE_OFF_AGE]
    mov     w10, #1
.L_arp_ev_scan:
    cmp     w10, w2
    b.hs    .L_arp_ev_copy
    add     x11, x0, x10, lsl #4
    ldr     w12, [x11, #FL_NET_ARP_CACHE_OFF_AGE]
    cmp     w12, w9
    b.hs    .L_arp_ev_next
    mov     w9, w12
    mov     w8, w10
.L_arp_ev_next:
    add     w10, w10, #1
    b       .L_arp_ev_scan
.L_arp_ev_copy:
    sub     w2, w2, #1
    cmp     w8, w2
    b.eq    .L_arp_ev_store
    add     x11, x0, w2, uxtw #4
    add     x12, x0, w8, uxtw #4
    ldr     x13, [x11]
    str     x13, [x12]
    ldr     x13, [x11, #8]
    str     x13, [x12, #8]
.L_arp_ev_store:
    str     w2, [x1]
    ret
.L_arp_ev_one:
    str     wzr, [x1]
.L_arp_ev_done:
    ret

/* x0=table x1=count w2=max x3=tick w4=ip x5=mac */
asm_net_arp_cache_insert:
    cbz     x5, .L_arp_ins_inval
    cbz     x1, .L_arp_ins_inval
    cbz     x3, .L_arp_ins_inval
    ldr     w6, [x1]
    mov     w7, #0
.L_arp_ins_scan:
    cmp     w7, w6
    b.hs    .L_arp_ins_new
    add     x8, x0, x7, lsl #4
    ldr     w9, [x8, #FL_NET_ARP_CACHE_OFF_IP]
    cmp     w9, w4
    b.ne    .L_arp_ins_next
    ldr     w9, [x5]
    str     w9, [x8, #FL_NET_ARP_CACHE_OFF_MAC]
    ldrh    w9, [x5, #4]
    strh    w9, [x8, #FL_NET_ARP_CACHE_OFF_MAC + 4]
    ldrb    w9, [x5, #5]
    strb    w9, [x8, #FL_NET_ARP_CACHE_OFF_MAC + 5]
    ldr     w9, [x3]
    add     w9, w9, #1
    str     w9, [x3]
    str     w9, [x8, #FL_NET_ARP_CACHE_OFF_AGE]
    mov     w0, #0
    ret
.L_arp_ins_next:
    add     w7, w7, #1
    b       .L_arp_ins_scan
.L_arp_ins_new:
    cmp     w6, w2
    b.lo    .L_arp_ins_append
    stp     x29, x30, [sp, #-64]!
    stp     x19, x20, [sp, #16]
    stp     x21, x22, [sp, #32]
    stp     x23, x24, [sp, #48]
    mov     x19, x0
    mov     x20, x1
    mov     x21, x3
    mov     x22, x4
    mov     x23, x5
    bl      asm_net_arp_cache_evict_oldest
    mov     x0, x19
    mov     x1, x20
    mov     x3, x21
    mov     x4, x22
    mov     x5, x23
    ldp     x23, x24, [sp, #48]
    ldp     x21, x22, [sp, #32]
    ldp     x19, x20, [sp, #16]
    ldp     x29, x30, [sp], #64
    ldr     w6, [x1]
.L_arp_ins_append:
    add     x8, x0, w6, uxtw #4
    str     w4, [x8, #FL_NET_ARP_CACHE_OFF_IP]
    ldr     w9, [x5]
    str     w9, [x8, #FL_NET_ARP_CACHE_OFF_MAC]
    ldrh    w9, [x5, #4]
    strh    w9, [x8, #FL_NET_ARP_CACHE_OFF_MAC + 4]
    ldrb    w9, [x5, #5]
    strb    w9, [x8, #FL_NET_ARP_CACHE_OFF_MAC + 5]
    ldr     w9, [x3]
    add     w9, w9, #1
    str     w9, [x3]
    str     w9, [x8, #FL_NET_ARP_CACHE_OFF_AGE]
    add     w6, w6, #1
    str     w6, [x1]
    mov     w0, #0
    ret
.L_arp_ins_inval:
    mov     w0, #-22
    ret
