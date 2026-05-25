/* Linux AArch64 socket syscalls for P3 wire host (hosted probes). */
.section .note.GNU-stack,"",@progbits
.text

.equ SYS_socket, 198
.equ SYS_bind, 200
.equ SYS_sendto, 206
.equ SYS_recvfrom, 207
.equ SYS_close, 57
.equ SYS_setsockopt, 208

.globl net_host_socket_asm
.globl net_host_bind_asm
.globl net_host_sendto_asm
.globl net_host_recvfrom_asm
.globl net_host_close_asm
.globl net_host_setsockopt_asm

net_host_socket_asm:
    mov     x8, #SYS_socket
    svc     #0
    ret

net_host_bind_asm:
    mov     x8, #SYS_bind
    svc     #0
    ret

net_host_sendto_asm:
    mov     x8, #SYS_sendto
    svc     #0
    ret

net_host_recvfrom_asm:
    mov     x8, #SYS_recvfrom
    svc     #0
    ret

net_host_close_asm:
    mov     x8, #SYS_close
    svc     #0
    ret

net_host_setsockopt_asm:
    mov     x8, #SYS_setsockopt
    svc     #0
    ret
