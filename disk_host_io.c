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
    unsigned char *p = (unsigned char *)buf;
    size_t got = 0;
    while (got < count) {
        ssize_t r = fix_syscall_long(
            disk_host_pread64_asm(fd, p + got, count - got, offset + (off_t)got));
        if (r < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (r == 0)
            return got ? (ssize_t)got : 0;
        got += (size_t)r;
    }
    return (ssize_t)got;
}

ssize_t disk_host_pwrite_vol(int fd, const void *buf, size_t count, off_t offset) {
    const unsigned char *p = (const unsigned char *)buf;
    size_t put = 0;
    while (put < count) {
        ssize_t r = fix_syscall_long(
            disk_host_pwrite64_asm(fd, p + put, count - put, offset + (off_t)put));
        if (r < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (r == 0) {
            errno = EIO;
            return -1;
        }
        put += (size_t)r;
    }
    return (ssize_t)put;
}

#else

ssize_t disk_host_pread_vol(int fd, void *buf, size_t count, off_t offset) {
    unsigned char *p = (unsigned char *)buf;
    size_t got = 0;
    while (got < count) {
        ssize_t r = pread(fd, p + got, count - got, offset + (off_t)got);
        if (r < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (r == 0)
            return got ? (ssize_t)got : 0;
        got += (size_t)r;
    }
    return (ssize_t)got;
}

ssize_t disk_host_pwrite_vol(int fd, const void *buf, size_t count, off_t offset) {
    const unsigned char *p = (const unsigned char *)buf;
    size_t put = 0;
    while (put < count) {
        ssize_t r = pwrite(fd, p + put, count - put, offset + (off_t)put);
        if (r < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (r == 0) {
            errno = EIO;
            return -1;
        }
        put += (size_t)r;
    }
    return (ssize_t)put;
}

#endif
