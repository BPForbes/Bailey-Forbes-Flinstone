/* P3 networking hot-path primitives (x86-64 GAS). */
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
    movw    $0x2000, 14(%rdi)
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
