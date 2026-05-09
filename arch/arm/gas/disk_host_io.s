/* Linux AArch64 pread64/pwrite64 syscalls for host disk cluster I/O. */
.section .note.GNU-stack,"",@progbits
.text

.equ SYS_pread64, 67
.equ SYS_pwrite64, 68

.globl disk_host_pread64_asm
.globl disk_host_pwrite64_asm

/* long disk_host_pread64_asm(int fd, void *buf, size_t count, off_t offset); */
disk_host_pread64_asm:
    mov x8, #SYS_pread64
    svc #0
    ret

/* long disk_host_pwrite64_asm(int fd, const void *buf, size_t count, off_t offset); */
disk_host_pwrite64_asm:
    mov x8, #SYS_pwrite64
    svc #0
    ret
