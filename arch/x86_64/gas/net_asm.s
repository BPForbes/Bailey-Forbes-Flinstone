/* P3 networking hot-path primitives (x86-64 GAS). */
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
