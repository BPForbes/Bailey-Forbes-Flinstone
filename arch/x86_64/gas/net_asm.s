/* P3 networking hot-path primitives (x86-64 GAS). */
.section .note.GNU-stack,"",@progbits
.text
.globl asm_net_checksum16
.globl asm_net_htons_be16
.globl asm_net_ntohs_be16
.globl asm_net_htonl_be32
.globl asm_net_ntohl_be32
.globl asm_net_htonll_be64
.globl asm_net_ntohll_be64
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

/* uint16_t asm_net_checksum16(const void *data, size_t len); */
asm_net_checksum16:
    xorl    %eax, %eax
    testq   %rdi, %rdi
    jz      .L_csum_empty
    testq   %rsi, %rsi
    jz      .L_csum_empty
    movq    %rsi, %r8
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

/* uint16_t asm_net_ntohs_be16(uint16_t net); — alias on bswap hardware. */
asm_net_ntohs_be16:
    movzwl  %di, %eax
    rolw    $8, %ax
    ret

/* uint32_t asm_net_htonl_be32(uint32_t host); */
asm_net_htonl_be32:
    movl    %edi, %eax
    bswapl  %eax
    ret

/* uint32_t asm_net_ntohl_be32(uint32_t net); — alias of htonl on bswap hw. */
asm_net_ntohl_be32:
    movl    %edi, %eax
    bswapl  %eax
    ret

/* uint64_t asm_net_htonll_be64(uint64_t host); */
asm_net_htonll_be64:
    movq    %rdi, %rax
    bswapq  %rax
    ret

/* uint64_t asm_net_ntohll_be64(uint64_t net); — alias of htonll on bswap hw. */
asm_net_ntohll_be64:
    movq    %rdi, %rax
    bswapq  %rax
    ret

/* size_t asm_net_tcp_build_syn(uint8_t *buf, size_t cap, uint16_t sport,
 *                               uint16_t dport, uint32_t seq); */
asm_net_tcp_build_syn:
    cmpq    $FL_NET_TCP_HDR_LEN, %rsi
    jb      .L_tcp_zero
    movzwl  %dx, %eax
    rolw    $8, %ax
    movw    %ax, (%rdi)
    movzwl  %cx, %eax
    rolw    $8, %ax
    movw    %ax, 2(%rdi)
    movl    %r8d, %eax
    bswap   %eax
    movl    %eax, 4(%rdi)
    movl    $0, 8(%rdi)
    movb    $0x50, 12(%rdi)
    movb    $FL_NET_TCP_FLAG_SYN, 13(%rdi)
    movb    $0x20, 14(%rdi)
    movb    $0x00, 15(%rdi)
    movq    $FL_NET_TCP_HDR_LEN, %rax
    ret
.L_tcp_zero:
    xorl    %eax, %eax
    ret

/* size_t asm_net_tcp_build_rst_ack(const uint8_t *syn, size_t syn_len,
 *                                  uint8_t *reply, size_t cap); */
asm_net_tcp_build_rst_ack:
    cmpq    $FL_NET_TCP_HDR_LEN, %rsi
    jb      .L_tcp_zero
    cmpq    $FL_NET_TCP_HDR_LEN, %rcx
    jb      .L_tcp_zero
    movzbl  2(%rdi), %eax
    shll    $8, %eax
    movzbl  3(%rdi), %r8d
    orl     %r8d, %eax
    movw    %ax, (%rdx)
    movzbl  (%rdi), %eax
    shll    $8, %eax
    movzbl  1(%rdi), %r8d
    orl     %r8d, %eax
    movw    %ax, 2(%rdx)
    movl    4(%rdi), %eax
    bswap   %eax
    testb   $FL_NET_TCP_FLAG_SYN, 13(%rdi)
    jz      .L_rst_seq_store
    incl    %eax
.L_rst_seq_store:
    bswap   %eax
    movl    %eax, 8(%rdx)
    movl    $0, 4(%rdx)
    movb    $0x50, 12(%rdx)
    movb    $(FL_NET_TCP_FLAG_RST | FL_NET_TCP_FLAG_ACK), 13(%rdx)
    movw    $0, 14(%rdx)
    movq    $FL_NET_TCP_HDR_LEN, %rax
    ret

/* int asm_net_tcp_read_ports_be(const uint8_t *tcp, size_t len,
 *                               uint16_t *sport, uint16_t *dport); */
asm_net_tcp_read_ports_be:
    cmpq    $4, %rsi
    jb      .L_ports_err
    movzbl  (%rdi), %eax
    shll    $8, %eax
    movzbl  1(%rdi), %r8d
    orl     %r8d, %eax
    movw    %ax, (%rdx)
    movzbl  2(%rdi), %eax
    shll    $8, %eax
    movzbl  3(%rdi), %r8d
    orl     %r8d, %eax
    movw    %ax, (%rcx)
    xorl    %eax, %eax
    ret
.L_ports_err:
    movl    $-1, %eax
    ret

/* size_t asm_net_icmp_echo_request_build(uint8_t *buf, size_t cap, uint16_t id,
 *                                        uint16_t seq, size_t payload_len); */
asm_net_icmp_echo_request_build:
    pushq   %rbx
    testq   %rdi, %rdi
    jz      .L_icmp_zero_pop
    movq    %rdi, %rbx
    movq    %r8, %r9
    addq    $FL_NET_ICMPV4_HDR_MIN, %r9
    cmpq    %r9, %rsi
    jb      .L_icmp_zero_pop
    movb    $FL_NET_ICMPV4_TYPE_ECHO, (%rbx)
    movb    $0, 1(%rbx)
    movw    $0, 2(%rbx)
    movzwl  %dx, %eax
    rolw    $8, %ax
    movw    %ax, 4(%rbx)
    movzwl  %cx, %eax
    rolw    $8, %ax
    movw    %ax, 6(%rbx)
    testq   %r8, %r8
    jz      .L_icmp_csum
    lea     FL_NET_ICMPV4_HDR_MIN(%rbx), %rdi
    movq    %r8, %rcx
    movb    $FL_NET_ICMP_ECHO_FILL, %al
    rep stosb
.L_icmp_csum:
    movq    %rbx, %rdi
    movq    %r9, %rsi
    call    asm_net_checksum16
    movb    %ah, 2(%rbx)
    movb    %al, 3(%rbx)
    movq    %r9, %rax
    popq    %rbx
    ret
.L_icmp_zero_pop:
    popq    %rbx
.L_icmp_zero:
    xorl    %eax, %eax
    ret

/* int asm_net_icmp_echo_reply_match(const uint8_t *buf, size_t len,
 *                                   uint16_t id, uint16_t seq); */
asm_net_icmp_echo_reply_match:
    testq   %rdi, %rdi
    jz      .L_icmp_match_fail
    cmpq    $FL_NET_ICMPV4_HDR_MIN, %rsi
    jb      .L_icmp_match_fail
    cmpb    $FL_NET_ICMPV4_TYPE_ECHO_REPLY, (%rdi)
    jne     .L_icmp_match_fail
    movzbl  4(%rdi), %eax
    shll    $8, %eax
    movzbl  5(%rdi), %r8d
    orl     %r8d, %eax
    cmpw    %dx, %ax
    jne     .L_icmp_match_fail
    movzbl  6(%rdi), %eax
    shll    $8, %eax
    movzbl  7(%rdi), %r8d
    orl     %r8d, %eax
    cmpw    %cx, %ax
    jne     .L_icmp_match_fail
    movl    $1, %eax
    ret
.L_icmp_match_fail:
    xorl    %eax, %eax
    ret

/* void asm_net_pseudo_header_fill12(uint8_t *pseudo, uint32_t src_be, uint32_t dst_be,
 *                                   uint8_t proto, size_t seg_len); */
asm_net_pseudo_header_fill12:
    movl    %esi, %eax
    movb    %al, (%rdi)
    shrl    $8, %eax
    movb    %al, 1(%rdi)
    shrl    $8, %eax
    movb    %al, 2(%rdi)
    shrl    $8, %eax
    movb    %al, 3(%rdi)
    movl    %edx, %eax
    movb    %al, 4(%rdi)
    shrl    $8, %eax
    movb    %al, 5(%rdi)
    shrl    $8, %eax
    movb    %al, 6(%rdi)
    shrl    $8, %eax
    movb    %al, 7(%rdi)
    movb    $0, 8(%rdi)
    movb    %cl, 9(%rdi)
    movq    %r8, %rax
    movb    %ah, 10(%rdi)
    movb    %al, 11(%rdi)
    ret

/* uint16_t asm_net_pseudo_checksum_tcpudp(uint32_t src_be, uint32_t dst_be, uint8_t proto,
 *                                        const void *seg, size_t seg_len); */
asm_net_pseudo_checksum_tcpudp:
    pushq   %rbx
    pushq   %r12
    pushq   %r13
    pushq   %r14
    pushq   %r15
    movl    %edi, %r12d
    movl    %esi, %r13d
    movl    %edx, %r14d
    movq    %rcx, %rbx
    movq    %r8, %r15
    subq    $16, %rsp
    leaq    4(%rsp), %rdi
    movl    %r12d, %esi
    movl    %r13d, %edx
    movl    %r14d, %ecx
    movq    %r15, %r8
    call    asm_net_pseudo_header_fill12
    xorl    %eax, %eax
    leaq    4(%rsp), %rdi
    movq    $12, %rsi
    call    .L_net_accum16_be
    testq   %rbx, %rbx
    jz      .L_pseudo_fold
    testq   %r15, %r15
    jz      .L_pseudo_fold
    movq    %rbx, %rdi
    movq    %r15, %rsi
    call    .L_net_accum16_be
.L_pseudo_fold:
    movl    %eax, %ecx
    shrl    $16, %eax
    addl    %ecx, %eax
    movl    %eax, %ecx
    shrl    $16, %eax
    addl    %ecx, %eax
    andl    $0xffff, %eax
    notw    %ax
    addq    $16, %rsp
    popq    %r15
    popq    %r14
    popq    %r13
    popq    %r12
    popq    %rbx
    ret

/* Add BE16 pairs at rdi/rsi into eax (caller zeroes eax first). */
.L_net_accum16_be:
    testq   %rsi, %rsi
    jz      .L_accum16_done
.L_accum16_pair:
    cmpq    $1, %rsi
    jbe     .L_accum16_odd
    movzwl  (%rdi), %ecx
    rolw    $8, %cx
    addl    %ecx, %eax
    addq    $2, %rdi
    subq    $2, %rsi
    jmp     .L_accum16_pair
.L_accum16_odd:
    testq   %rsi, %rsi
    jz      .L_accum16_done
    movzbl  (%rdi), %ecx
    shll    $8, %ecx
    addl    %ecx, %eax
.L_accum16_done:
    ret

/* void asm_net_dns_query_header_prefix(uint8_t *query, uint16_t txid); */
asm_net_dns_query_header_prefix:
    movzwl  %si, %eax
    rolw    $8, %ax
    movw    %ax, (%rdi)
    movb    $0x01, 2(%rdi)
    movb    $0, 3(%rdi)
    movb    $0, 4(%rdi)
    movb    $0x01, 5(%rdi)
    ret

/* void asm_net_arp_cache_clear(void *table, size_t max_entries, unsigned *count,
 *                              unsigned *tick); */
asm_net_arp_cache_clear:
    testq   %rdi, %rdi
    jz      .L_arp_clr_done
    testq   %rdx, %rdx
    jz      .L_arp_clr_done
    testq   %rcx, %rcx
    jz      .L_arp_clr_done
    movl    $0, (%rdx)
    movl    $0, (%rcx)
    testq   %rsi, %rsi
    jz      .L_arp_clr_done
    movq    %rdi, %rax
    imulq   $FL_NET_ARP_CACHE_ENTRY_STRIDE, %rsi, %r8
    addq    %rdi, %r8
.L_arp_clr_row:
    cmpq    %r8, %rax
    jae     .L_arp_clr_done
    movq    $0, (%rax)
    movq    $0, 8(%rax)
    addq    $FL_NET_ARP_CACHE_ENTRY_STRIDE, %rax
    jmp     .L_arp_clr_row
.L_arp_clr_done:
    ret

/* int asm_net_arp_cache_lookup(const void *table, unsigned count, uint32_t ip_be,
 *                               uint8_t *mac_out, unsigned *tick); */
asm_net_arp_cache_lookup:
    xorl    %eax, %eax
    testq   %rdi, %rdi
    jz      .L_arp_lk_fail
    testq   %rcx, %rcx
    jz      .L_arp_lk_fail
    testq   %r8, %r8
    jz      .L_arp_lk_fail
.L_arp_lk_loop:
    cmpl    %esi, %eax
    jae     .L_arp_lk_fail
    movq    %rax, %r11
    shlq    $4, %r11
    addq    %rdi, %r11
    movl    FL_NET_ARP_CACHE_OFF_IP(%r11), %r10d
    cmpl    %edx, %r10d
    jne     .L_arp_lk_next
    movl    FL_NET_ARP_CACHE_OFF_MAC(%r11), %r10d
    movl    %r10d, (%rcx)
    movzwl  FL_NET_ARP_CACHE_OFF_MAC + 4(%r11), %r10d
    movw    %r10w, 4(%rcx)
    movzbl  FL_NET_ARP_CACHE_OFF_MAC + 5(%r11), %r10d
    movb    %r10b, 5(%rcx)
    movl    (%r8), %r10d
    incl    %r10d
    movl    %r10d, (%r8)
    movl    %r10d, FL_NET_ARP_CACHE_OFF_AGE(%r11)
    movl    $1, %eax
    ret
.L_arp_lk_next:
    incl    %eax
    jmp     .L_arp_lk_loop
.L_arp_lk_fail:
    xorl    %eax, %eax
    ret

/* void asm_net_arp_cache_evict_oldest(void *table, unsigned *count); */
asm_net_arp_cache_evict_oldest:
    pushq   %r12
    testq   %rdi, %rdi
    jz      .L_arp_ev_done
    testq   %rsi, %rsi
    jz      .L_arp_ev_done
    movl    (%rsi), %eax
    testl   %eax, %eax
    jz      .L_arp_ev_done
    cmpl    $1, %eax
    je      .L_arp_ev_one
    xorl    %r12d, %r12d
    movl    FL_NET_ARP_CACHE_OFF_AGE(%rdi), %r9d
    movl    $1, %ecx
.L_arp_ev_scan:
    cmpl    %ecx, %eax
    jae     .L_arp_ev_copy
    movq    %rcx, %r11
    shlq    $4, %r11
    addq    %rdi, %r11
    movl    FL_NET_ARP_CACHE_OFF_AGE(%r11), %r10d
    cmpl    %r10d, %r9d
    jae     .L_arp_ev_scan_next
    movl    %r10d, %r9d
    movl    %ecx, %r12d
.L_arp_ev_scan_next:
    incl    %ecx
    jmp     .L_arp_ev_scan
.L_arp_ev_copy:
    decl    %eax
    cmpl    %r12d, %eax
    je      .L_arp_ev_store
    movq    %rax, %r11
    shlq    $4, %r11
    addq    %rdi, %r11
    movq    %r12, %rcx
    shlq    $4, %rcx
    addq    %rdi, %rcx
    movq    (%r11), %r10
    movq    %r10, (%rcx)
    movq    8(%r11), %r10
    movq    %r10, 8(%rcx)
.L_arp_ev_store:
    movl    %eax, (%rsi)
    jmp     .L_arp_ev_done
.L_arp_ev_one:
    movl    $0, (%rsi)
.L_arp_ev_done:
    popq    %r12
    ret

/* int asm_net_arp_cache_insert(void *table, unsigned *count, unsigned max_entries,
 *                               unsigned *tick, uint32_t ip_be, const uint8_t *mac); */
asm_net_arp_cache_insert:
    pushq   %r12
    testq   %r9, %r9
    jz      .L_arp_ins_inval
    testq   %rsi, %rsi
    jz      .L_arp_ins_inval
    testq   %rcx, %rcx
    jz      .L_arp_ins_inval
    movl    (%rsi), %eax
    xorl    %r10d, %r10d
.L_arp_ins_scan:
    cmpl    %r10d, %eax
    jae     .L_arp_ins_new
    movq    %r10, %r11
    shlq    $4, %r11
    addq    %rdi, %r11
    cmpl    %r8d, FL_NET_ARP_CACHE_OFF_IP(%r11)
    jne     .L_arp_ins_next
    movl    (%r9), %r10d
    movl    %r10d, FL_NET_ARP_CACHE_OFF_MAC(%r11)
    movzwl  4(%r9), %r10d
    movw    %r10w, FL_NET_ARP_CACHE_OFF_MAC + 4(%r11)
    movzbl  5(%r9), %r10d
    movb    %r10b, FL_NET_ARP_CACHE_OFF_MAC + 5(%r11)
    movl    (%rcx), %r10d
    incl    %r10d
    movl    %r10d, (%rcx)
    movl    %r10d, FL_NET_ARP_CACHE_OFF_AGE(%r11)
    xorl    %eax, %eax
    popq    %r12
    ret
.L_arp_ins_next:
    incl    %r10d
    jmp     .L_arp_ins_scan
.L_arp_ins_new:
    cmpl    %eax, %edx
    jb      .L_arp_ins_append
    pushq   %rbx
    pushq   %rdx
    pushq   %rcx
    pushq   %r8
    pushq   %r9
    movq    %rdi, %rbx
    call    asm_net_arp_cache_evict_oldest
    movq    %rbx, %rdi
    popq    %r9
    popq    %r8
    popq    %rcx
    popq    %rdx
    popq    %rbx
    movl    (%rsi), %eax
.L_arp_ins_append:
    movq    %rax, %r11
    shlq    $4, %r11
    addq    %rdi, %r11
    movl    %r8d, FL_NET_ARP_CACHE_OFF_IP(%r11)
    movl    (%r9), %r10d
    movl    %r10d, FL_NET_ARP_CACHE_OFF_MAC(%r11)
    movzwl  4(%r9), %r10d
    movw    %r10w, FL_NET_ARP_CACHE_OFF_MAC + 4(%r11)
    movzbl  5(%r9), %r10d
    movb    %r10b, FL_NET_ARP_CACHE_OFF_MAC + 5(%r11)
    movl    (%rcx), %r10d
    incl    %r10d
    movl    %r10d, (%rcx)
    movl    %r10d, FL_NET_ARP_CACHE_OFF_AGE(%r11)
    incl    (%rsi)
    xorl    %eax, %eax
    popq    %r12
    ret
.L_arp_ins_inval:
    movl    $-22, %eax
    popq    %r12
    ret
