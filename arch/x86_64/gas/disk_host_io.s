/* Linux x86-64 pread64/pwrite64 syscalls for host disk cluster I/O. */
.section .note.GNU-stack,"",@progbits
.text

.equ SYS_pread64, 17
.equ SYS_pwrite64, 18

.globl disk_host_pread64_asm
.globl disk_host_pwrite64_asm

/* long disk_host_pread64_asm(int fd, void *buf, size_t count, off_t offset); */
disk_host_pread64_asm:
    movq %rcx, %r10
    movq $SYS_pread64, %rax
    syscall
    ret

/* long disk_host_pwrite64_asm(int fd, const void *buf, size_t count, off_t offset); */
disk_host_pwrite64_asm:
    movq %rcx, %r10
    movq $SYS_pwrite64, %rax
    syscall
    ret
