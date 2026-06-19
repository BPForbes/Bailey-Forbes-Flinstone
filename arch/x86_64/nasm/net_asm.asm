; P3 networking hot-path primitives (x86-64 NASM).
section .note.GNU-stack progbits noalloc noexec nowrite
section .text

%define FL_NET_TCP_HDR_LEN 20
%define FL_NET_TCP_FLAG_SYN 0x02
%define FL_NET_TCP_FLAG_RST 0x04
%define FL_NET_TCP_FLAG_ACK 0x10

%define FL_NET_ICMPV4_HDR_MIN 8
%define FL_NET_ICMPV4_TYPE_ECHO 8
%define FL_NET_ICMPV4_TYPE_ECHO_REPLY 0
%define FL_NET_ICMP_ECHO_FILL 0x5a

global asm_net_checksum16
global asm_net_htons_be16
global asm_net_ntohs_be16
global asm_net_htonl_be32
global asm_net_ntohl_be32
global asm_net_htonll_be64
global asm_net_ntohll_be64
global asm_net_tcp_build_syn
global asm_net_tcp_build_rst_ack
global asm_net_icmp_echo_request_build
global asm_net_icmp_echo_reply_match
global asm_net_pseudo_header_fill12
global asm_net_pseudo_checksum_tcpudp
global asm_net_dns_query_header_prefix
global asm_net_tcp_read_ports_be
global asm_net_arp_cache_clear
global asm_net_arp_cache_lookup
global asm_net_arp_cache_insert
global asm_net_arp_cache_evict_oldest

%define FL_NET_ARP_CACHE_ENTRY_STRIDE 16
%define FL_NET_ARP_CACHE_OFF_IP 0
%define FL_NET_ARP_CACHE_OFF_MAC 4
%define FL_NET_ARP_CACHE_OFF_AGE 12

asm_net_checksum16:
    xor eax, eax
    test rdi, rdi
    jz .L_csum_empty
    test rsi, rsi
    jz .L_csum_empty
    mov r8, rsi
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

asm_net_ntohs_be16:
    movzx eax, di
    rol ax, 8
    ret

asm_net_htonl_be32:
    mov eax, edi
    bswap eax
    ret

asm_net_ntohl_be32:
    mov eax, edi
    bswap eax
    ret

asm_net_htonll_be64:
    mov rax, rdi
    bswap rax
    ret

asm_net_ntohll_be64:
    mov rax, rdi
    bswap rax
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
    mov byte [rdi + 14], 0x20
    mov byte [rdi + 15], 0
    mov rax, FL_NET_TCP_HDR_LEN
    ret
.L_tcp_zero:
    xor eax, eax
    ret

asm_net_tcp_build_rst_ack:
    cmp rsi, FL_NET_TCP_HDR_LEN
    jb .L_rst_zero
    cmp rcx, FL_NET_TCP_HDR_LEN
    jb .L_rst_zero
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
.L_rst_zero:
    xor eax, eax
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

asm_net_icmp_echo_request_build:
    push rbx
    test rdi, rdi
    jz .L_icmp_zero_pop
    mov rbx, rdi
    mov r9, r8
    add r9, FL_NET_ICMPV4_HDR_MIN
    cmp rsi, r9
    jb .L_icmp_zero_pop
    mov byte [rbx], FL_NET_ICMPV4_TYPE_ECHO
    mov byte [rbx + 1], 0
    mov word [rbx + 2], 0
    movzx eax, dx
    rol ax, 8
    mov [rbx + 4], ax
    movzx eax, cx
    rol ax, 8
    mov [rbx + 6], ax
    test r8, r8
    jz .L_icmp_csum
    lea rdi, [rbx + FL_NET_ICMPV4_HDR_MIN]
    mov rcx, r8
    mov al, FL_NET_ICMP_ECHO_FILL
    rep stosb
.L_icmp_csum:
    mov rdi, rbx
    mov rsi, r9
    call asm_net_checksum16
    mov [rbx + 2], ah
    mov [rbx + 3], al
    mov rax, r9
    pop rbx
    ret
.L_icmp_zero_pop:
    pop rbx
.L_icmp_zero:
    xor eax, eax
    ret

asm_net_icmp_echo_reply_match:
    test rdi, rdi
    jz .L_icmp_match_fail
    cmp rsi, FL_NET_ICMPV4_HDR_MIN
    jb .L_icmp_match_fail
    cmp byte [rdi], FL_NET_ICMPV4_TYPE_ECHO_REPLY
    jne .L_icmp_match_fail
    movzx eax, byte [rdi + 4]
    shl eax, 8
    movzx r8d, byte [rdi + 5]
    or eax, r8d
    cmp ax, dx
    jne .L_icmp_match_fail
    movzx eax, byte [rdi + 6]
    shl eax, 8
    movzx r8d, byte [rdi + 7]
    or eax, r8d
    cmp ax, cx
    jne .L_icmp_match_fail
    mov eax, 1
    ret
.L_icmp_match_fail:
    xor eax, eax
    ret

asm_net_pseudo_header_fill12:
    mov eax, esi
    mov [rdi], al
    shr eax, 8
    mov [rdi + 1], al
    shr eax, 8
    mov [rdi + 2], al
    shr eax, 8
    mov [rdi + 3], al
    mov eax, edx
    mov [rdi + 4], al
    shr eax, 8
    mov [rdi + 5], al
    shr eax, 8
    mov [rdi + 6], al
    shr eax, 8
    mov [rdi + 7], al
    mov byte [rdi + 8], 0
    mov [rdi + 9], cl
    mov rax, r8
    mov [rdi + 10], ah
    mov [rdi + 11], al
    ret

asm_net_pseudo_checksum_tcpudp:
    push rbx
    push r12
    push r13
    push r14
    push r15
    mov r12d, edi
    mov r13d, esi
    mov r14d, edx
    mov rbx, rcx
    mov r15, r8
    sub rsp, 16
    lea rdi, [rsp + 4]
    mov esi, r12d
    mov edx, r13d
    mov ecx, r14d
    mov r8, r15
    call asm_net_pseudo_header_fill12
    xor eax, eax
    lea rdi, [rsp + 4]
    mov rsi, 12
    call .L_net_accum16_be
    test rbx, rbx
    jz .L_pseudo_fold
    test r15, r15
    jz .L_pseudo_fold
    mov rdi, rbx
    mov rsi, r15
    call .L_net_accum16_be
.L_pseudo_fold:
    mov ecx, eax
    shr eax, 16
    add eax, ecx
    mov ecx, eax
    shr eax, 16
    add eax, ecx
    and eax, 0xffff
    not ax
    add rsp, 16
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    ret

.L_net_accum16_be:
    test rsi, rsi
    jz .L_accum16_done
.L_accum16_pair:
    cmp rsi, 1
    jbe .L_accum16_odd
    movzx ecx, word [rdi]
    rol cx, 8
    add eax, ecx
    add rdi, 2
    sub rsi, 2
    jmp .L_accum16_pair
.L_accum16_odd:
    test rsi, rsi
    jz .L_accum16_done
    movzx ecx, byte [rdi]
    shl ecx, 8
    add eax, ecx
.L_accum16_done:
    ret

asm_net_dns_query_header_prefix:
    movzx eax, si
    rol ax, 8
    mov [rdi], ax
    mov byte [rdi + 2], 0x01
    mov byte [rdi + 3], 0
    mov byte [rdi + 4], 0
    mov byte [rdi + 5], 0x01
    ret

asm_net_arp_cache_clear:
    test rdi, rdi
    jz .L_arp_clr_done
    test rdx, rdx
    jz .L_arp_clr_done
    test rcx, rcx
    jz .L_arp_clr_done
    mov dword [rdx], 0
    mov dword [rcx], 0
    test rsi, rsi
    jz .L_arp_clr_done
    mov rax, rdi
    imul r8, rsi, FL_NET_ARP_CACHE_ENTRY_STRIDE
    add r8, rdi
.L_arp_clr_row:
    cmp rax, r8
    jae .L_arp_clr_done
    mov qword [rax], 0
    mov qword [rax + 8], 0
    add rax, FL_NET_ARP_CACHE_ENTRY_STRIDE
    jmp .L_arp_clr_row
.L_arp_clr_done:
    ret

asm_net_arp_cache_lookup:
    xor eax, eax
    test rdi, rdi
    jz .L_arp_lk_fail
    test rcx, rcx
    jz .L_arp_lk_fail
    test r8, r8
    jz .L_arp_lk_fail
.L_arp_lk_loop:
    cmp eax, esi
    jae .L_arp_lk_fail
    mov r11, rax
    shl r11, 4
    add r11, rdi
    mov r10d, [r11 + FL_NET_ARP_CACHE_OFF_IP]
    cmp r10d, edx
    jne .L_arp_lk_next
    mov r10d, [r11 + FL_NET_ARP_CACHE_OFF_MAC]
    mov [rcx], r10d
    movzx r10d, word [r11 + FL_NET_ARP_CACHE_OFF_MAC + 4]
    mov [rcx + 4], r10w
    mov r10d, [r8]
    inc r10d
    mov [r8], r10d
    mov [r11 + FL_NET_ARP_CACHE_OFF_AGE], r10d
    mov eax, 1
    ret
.L_arp_lk_next:
    inc eax
    jmp .L_arp_lk_loop
.L_arp_lk_fail:
    xor eax, eax
    ret

asm_net_arp_cache_evict_oldest:
    push r12
    test rdi, rdi
    jz .L_arp_ev_done
    test rsi, rsi
    jz .L_arp_ev_done
    mov eax, [rsi]
    test eax, eax
    jz .L_arp_ev_done
    cmp eax, 1
    je .L_arp_ev_one
    xor r12d, r12d
    mov r9d, [rdi + FL_NET_ARP_CACHE_OFF_AGE]
    mov ecx, 1
.L_arp_ev_scan:
    cmp ecx, eax
    jae .L_arp_ev_copy
    mov r11, rcx
    shl r11, 4
    add r11, rdi
    mov r10d, [r11 + FL_NET_ARP_CACHE_OFF_AGE]
    cmp r10d, r9d
    jae .L_arp_ev_scan_next
    mov r9d, r10d
    mov r12d, ecx
.L_arp_ev_scan_next:
    inc ecx
    jmp .L_arp_ev_scan
.L_arp_ev_copy:
    dec eax
    cmp r12d, eax
    je .L_arp_ev_store
    mov r11, rax
    shl r11, 4
    add r11, rdi
    mov rcx, r12
    shl rcx, 4
    add rcx, rdi
    mov r10, [r11]
    mov [rcx], r10
    mov r10, [r11 + 8]
    mov [rcx + 8], r10
.L_arp_ev_store:
    mov [rsi], eax
    jmp .L_arp_ev_done
.L_arp_ev_one:
    mov dword [rsi], 0
.L_arp_ev_done:
    pop r12
    ret

asm_net_arp_cache_insert:
    push r12
    test r9, r9
    jz .L_arp_ins_inval
    test rsi, rsi
    jz .L_arp_ins_inval
    test rcx, rcx
    jz .L_arp_ins_inval
    mov eax, [rsi]
    xor r10d, r10d
.L_arp_ins_scan:
    cmp r10d, eax
    jae .L_arp_ins_new
    mov r11, r10
    shl r11, 4
    add r11, rdi
    cmp r8d, [r11 + FL_NET_ARP_CACHE_OFF_IP]
    jne .L_arp_ins_next
    mov r10d, [r9]
    mov [r11 + FL_NET_ARP_CACHE_OFF_MAC], r10d
    movzx r10d, word [r9 + 4]
    mov [r11 + FL_NET_ARP_CACHE_OFF_MAC + 4], r10w
    mov r10d, [rcx]
    inc r10d
    mov [rcx], r10d
    mov [r11 + FL_NET_ARP_CACHE_OFF_AGE], r10d
    xor eax, eax
    pop r12
    ret
.L_arp_ins_next:
    inc r10d
    jmp .L_arp_ins_scan
.L_arp_ins_new:
    cmp eax, edx
    jb .L_arp_ins_append
    push rbx
    push rdx
    push rcx
    push r8
    push r9
    mov rbx, rdi
    call asm_net_arp_cache_evict_oldest
    mov rdi, rbx
    pop r9
    pop r8
    pop rcx
    pop rdx
    pop rbx
    mov eax, [rsi]
.L_arp_ins_append:
    mov r11, rax
    shl r11, 4
    add r11, rdi
    mov [r11 + FL_NET_ARP_CACHE_OFF_IP], r8d
    mov r10d, [r9]
    mov [r11 + FL_NET_ARP_CACHE_OFF_MAC], r10d
    movzx r10d, word [r9 + 4]
    mov [r11 + FL_NET_ARP_CACHE_OFF_MAC + 4], r10w
    mov r10d, [rcx]
    inc r10d
    mov [rcx], r10d
    mov [r11 + FL_NET_ARP_CACHE_OFF_AGE], r10d
    inc dword [rsi]
    xor eax, eax
    pop r12
    ret
.L_arp_ins_inval:
    mov eax, -22
    pop r12
    ret
