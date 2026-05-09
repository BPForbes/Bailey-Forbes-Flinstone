#ifndef DISK_HOST_ASM_H
#define DISK_HOST_ASM_H

#include <sys/types.h>

/*
 * Host volume positioning I/O (Linux syscalls in arch/.../gas/disk_host_io.s).
 * Falls back to libc pread/pwrite in disk_host_io.c when not using ASM syscalls.
 */
ssize_t disk_host_pread_vol(int fd, void *buf, size_t count, off_t offset);
ssize_t disk_host_pwrite_vol(int fd, const void *buf, size_t count, off_t offset);

#endif /* DISK_HOST_ASM_H */
