/*
 * Linux hosted wire I/O via arch-specific socket syscalls (x86-64 / AArch64 ASM).
 */
#include "net_wire_host_syscall.h"

#include <errno.h>

#if defined(__linux__) && (defined(__x86_64__) || defined(__aarch64__)) && \
    defined(FL_NET_ASM_AVAILABLE)

long net_host_socket_asm(int domain, int type, int protocol);
long net_host_bind_asm(int fd, const void *addr, unsigned int addrlen);
long net_host_sendto_asm(int fd, const void *buf, size_t len, int flags, const void *addr,
                         unsigned int addrlen);
long net_host_recvfrom_asm(int fd, void *buf, size_t len, int flags, void *addr,
                           unsigned int *addrlen);
long net_host_close_asm(int fd);
long net_host_setsockopt_asm(int fd, int level, int optname, const void *optval,
                             unsigned int optlen);

static long fix_syscall_long(long r) {
    if (r < 0 && r > -4096) {
        errno = (int)-r;
        return -1;
    }
    return r;
}

int net_host_socket(int domain, int type, int protocol) {
    return (int)fix_syscall_long(net_host_socket_asm(domain, type, protocol));
}

int net_host_bind(int fd, const void *addr, unsigned int addrlen) {
    return (int)fix_syscall_long(net_host_bind_asm(fd, addr, addrlen));
}

ssize_t net_host_sendto(int fd, const void *buf, size_t len, int flags, const void *addr,
                        unsigned int addrlen) {
    return (ssize_t)fix_syscall_long(
        net_host_sendto_asm(fd, buf, len, flags, addr, addrlen));
}

ssize_t net_host_recvfrom(int fd, void *buf, size_t len, int flags, void *addr,
                          unsigned int *addrlen) {
    return (ssize_t)fix_syscall_long(
        net_host_recvfrom_asm(fd, buf, len, flags, addr, addrlen));
}

int net_host_close(int fd) {
    return (int)fix_syscall_long(net_host_close_asm(fd));
}

int net_host_setsockopt(int fd, int level, int optname, const void *optval,
                        unsigned int optlen) {
    return (int)fix_syscall_long(
        net_host_setsockopt_asm(fd, level, optname, optval, optlen));
}

#endif
