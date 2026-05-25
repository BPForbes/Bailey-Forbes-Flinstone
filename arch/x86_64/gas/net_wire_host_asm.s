/* Linux x86-64 socket syscalls for P3 wire host (hosted probes). */
.section .note.GNU-stack,"",@progbits
.text

.equ SYS_socket, 41
.equ SYS_bind, 49
.equ SYS_sendto, 44
.equ SYS_recvfrom, 45
.equ SYS_close, 3
.equ SYS_setsockopt, 54

.globl net_host_socket_asm
.globl net_host_bind_asm
.globl net_host_sendto_asm
.globl net_host_recvfrom_asm
.globl net_host_close_asm
.globl net_host_setsockopt_asm

/* long net_host_socket_asm(int domain, int type, int protocol); */
net_host_socket_asm:
    movq    %rdx, %r10
    movq    $SYS_socket, %rax
    syscall
    ret

/* long net_host_bind_asm(int fd, const void *addr, unsigned int addrlen); */
net_host_bind_asm:
    movq    $SYS_bind, %rax
    syscall
    ret

/* long net_host_sendto_asm(int fd, const void *buf, size_t len, int flags,
 *                          const void *addr, unsigned int addrlen); */
net_host_sendto_asm:
    movq    %rcx, %r10
    movq    $SYS_sendto, %rax
    syscall
    ret

/* long net_host_recvfrom_asm(int fd, void *buf, size_t len, int flags,
 *                            void *addr, unsigned int *addrlen); */
net_host_recvfrom_asm:
    movq    %rcx, %r10
    movq    $SYS_recvfrom, %rax
    syscall
    ret

/* long net_host_close_asm(int fd); */
net_host_close_asm:
    movq    $SYS_close, %rax
    syscall
    ret

/* long net_host_setsockopt_asm(int fd, int level, int optname,
 *                              const void *optval, unsigned int optlen); */
net_host_setsockopt_asm:
    movq    %rcx, %r10
    movq    $SYS_setsockopt, %rax
    syscall
    ret
