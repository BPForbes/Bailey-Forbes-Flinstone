; Linux x86-64 pread64/pwrite64 syscalls for host disk cluster I/O.
section .note.GNU-stack progbits alloc noexec
section .text

%define SYS_pread64 17
%define SYS_pwrite64 18

global disk_host_pread64_asm
global disk_host_pwrite64_asm

; long disk_host_pread64_asm(int fd, void *buf, size_t count, off_t offset);
disk_host_pread64_asm:
    mov r10, rcx
    mov rax, SYS_pread64
    syscall
    ret

; long disk_host_pwrite64_asm(int fd, const void *buf, size_t count, off_t offset);
disk_host_pwrite64_asm:
    mov r10, rcx
    mov rax, SYS_pwrite64
    syscall
    ret
