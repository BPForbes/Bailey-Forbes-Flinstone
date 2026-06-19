; Linux x86-64 socket syscalls for P3 wire host (hosted probes).
section .note.GNU-stack progbits noalloc noexec nowrite
section .text

%define SYS_socket 41
%define SYS_bind 49
%define SYS_sendto 44
%define SYS_recvfrom 45
%define SYS_close 3
%define SYS_setsockopt 54

global net_host_socket_asm
global net_host_bind_asm
global net_host_sendto_asm
global net_host_recvfrom_asm
global net_host_close_asm
global net_host_setsockopt_asm

net_host_socket_asm:
    mov r10, rdx
    mov rax, SYS_socket
    syscall
    ret

net_host_bind_asm:
    mov rax, SYS_bind
    syscall
    ret

net_host_sendto_asm:
    mov r10, rcx
    mov rax, SYS_sendto
    syscall
    ret

net_host_recvfrom_asm:
    mov r10, rcx
    mov rax, SYS_recvfrom
    syscall
    ret

net_host_close_asm:
    mov rax, SYS_close
    syscall
    ret

net_host_setsockopt_asm:
    mov r10, rcx
    mov rax, SYS_setsockopt
    syscall
    ret
