; P3 networking hot-path primitives (x86-64 NASM).
section .note.GNU-stack progbits noalloc noexec nowrite
section .text

%define FL_NET_TCP_HDR_LEN 20
%define FL_NET_TCP_FLAG_SYN 0x02
%define FL_NET_TCP_FLAG_RST 0x04
%define FL_NET_TCP_FLAG_ACK 0x10

global asm_net_checksum16
global asm_net_htons_be16
global asm_net_tcp_build_syn
global asm_net_tcp_build_rst_ack
global asm_net_tcp_read_ports_be

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

asm_net_htons_be16:
    movzx eax, di
    rol ax, 8
    ret

asm_net_tcp_build_syn:
    cmp rsi, FL_NET_TCP_HDR_LEN
    jb .L_tcp_zero
    movzx eax, dx
    rol ax, 8
    mov [rdi], ax
    movzx eax, cx
    rol ax, 8
    mov [rdi + 2], ax
    mov eax, r8d
    bswap eax
    mov [rdi + 4], eax
    mov dword [rdi + 8], 0
    mov byte [rdi + 12], 0x50
    mov byte [rdi + 13], FL_NET_TCP_FLAG_SYN
    mov word [rdi + 14], 0x2000
    mov rax, FL_NET_TCP_HDR_LEN
    ret
.L_tcp_zero:
    xor eax, eax
    ret

asm_net_tcp_build_rst_ack:
    cmp rsi, FL_NET_TCP_HDR_LEN
    jb .L_tcp_zero
    cmp rcx, FL_NET_TCP_HDR_LEN
    jb .L_tcp_zero
    movzx eax, byte [rdi + 2]
    shl eax, 8
    movzx r8d, byte [rdi + 3]
    or eax, r8d
    mov [rdx], ax
    movzx eax, byte [rdi]
    shl eax, 8
    movzx r8d, byte [rdi + 1]
    or eax, r8d
    mov [rdx + 2], ax
    mov eax, [rdi + 4]
    bswap eax
    test byte [rdi + 13], FL_NET_TCP_FLAG_SYN
    jz .L_rst_seq_store
    inc eax
.L_rst_seq_store:
    bswap eax
    mov [rdx + 8], eax
    mov dword [rdx + 4], 0
    mov byte [rdx + 12], 0x50
    mov byte [rdx + 13], FL_NET_TCP_FLAG_RST | FL_NET_TCP_FLAG_ACK
    mov word [rdx + 14], 0
    mov rax, FL_NET_TCP_HDR_LEN
    ret

asm_net_tcp_read_ports_be:
    cmp rsi, 4
    jb .L_ports_err
    movzx eax, byte [rdi]
    shl eax, 8
    movzx r8d, byte [rdi + 1]
    or eax, r8d
    mov [rdx], ax
    movzx eax, byte [rdi + 2]
    shl eax, 8
    movzx r8d, byte [rdi + 3]
    or eax, r8d
    mov [rcx], ax
    xor eax, eax
    ret
.L_ports_err:
    mov eax, -1
    ret
