/*
 * Thin wrappers around ASM-backed pread64/pwrite64 on Linux x86-64 / AArch64 host.
 */
#include "disk_host_asm.h"
#include <errno.h>
#include <unistd.h>

#if defined(__linux__) && (defined(__x86_64__) || defined(__aarch64__)) && !defined(DISK_HOST_USE_LIBC_PREADV)

long disk_host_pread64_asm(int fd, void *buf, size_t count, off_t offset);
long disk_host_pwrite64_asm(int fd, const void *buf, size_t count, off_t offset);

static ssize_t fix_syscall_long(long r) {
    if (r < 0 && r > -4096) {
        errno = (int)-r;
        return -1;
    }
    return (ssize_t)r;
}

ssize_t disk_host_pread_vol(int fd, void *buf, size_t count, off_t offset) {
    return fix_syscall_long(disk_host_pread64_asm(fd, buf, count, offset));
}

ssize_t disk_host_pwrite_vol(int fd, const void *buf, size_t count, off_t offset) {
    return fix_syscall_long(disk_host_pwrite64_asm(fd, buf, count, offset));
}

#else

ssize_t disk_host_pread_vol(int fd, void *buf, size_t count, off_t offset) {
    return pread(fd, buf, count, offset);
}

ssize_t disk_host_pwrite_vol(int fd, const void *buf, size_t count, off_t offset) {
    return pwrite(fd, buf, count, offset);
}

#endif
