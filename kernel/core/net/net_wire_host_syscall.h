#ifndef NET_WIRE_HOST_SYSCALL_H
#define NET_WIRE_HOST_SYSCALL_H

#include <stddef.h>
#include <sys/types.h>

#if defined(__linux__) && (defined(__x86_64__) || defined(__aarch64__)) && defined(FL_NET_ASM_AVAILABLE)

int net_host_socket(int domain, int type, int protocol);
int net_host_bind(int fd, const void *addr, unsigned int addrlen);
ssize_t net_host_sendto(int fd, const void *buf, size_t len, int flags, const void *addr,
                        unsigned int addrlen);
ssize_t net_host_recvfrom(int fd, void *buf, size_t len, int flags, void *addr,
                          unsigned int *addrlen);
int net_host_close(int fd);
int net_host_setsockopt(int fd, int level, int optname, const void *optval,
                        unsigned int optlen);

#else

#include <sys/socket.h>
#include <unistd.h>

static inline int net_host_socket(int domain, int type, int protocol) {
    return socket(domain, type, protocol);
}
static inline int net_host_bind(int fd, const void *addr, unsigned int addrlen) {
    return bind(fd, (const struct sockaddr *)addr, (socklen_t)addrlen);
}
static inline ssize_t net_host_sendto(int fd, const void *buf, size_t len, int flags,
                                      const void *addr, unsigned int addrlen) {
    return sendto(fd, buf, len, flags, (const struct sockaddr *)addr, (socklen_t)addrlen);
}
static inline ssize_t net_host_recvfrom(int fd, void *buf, size_t len, int flags, void *addr,
                                        unsigned int *addrlen) {
    socklen_t alen = addrlen ? (socklen_t)*addrlen : 0;
    ssize_t n = recvfrom(fd, buf, len, flags, (struct sockaddr *)addr, addrlen ? &alen : NULL);
    if (addrlen)
        *addrlen = (unsigned int)alen;
    return n;
}
static inline int net_host_close(int fd) {
    return close(fd);
}
static inline int net_host_setsockopt(int fd, int level, int optname, const void *optval,
                                      unsigned int optlen) {
    return setsockopt(fd, level, optname, optval, (socklen_t)optlen);
}

#endif

#endif /* NET_WIRE_HOST_SYSCALL_H */
