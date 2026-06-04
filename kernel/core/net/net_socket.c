#include "net_socket.h"

#include "net_endian.h"
#include "net_endpoint.h"
#include "net_wire_host_syscall.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/select.h>
#include <sys/time.h>

#if defined(__linux__) || defined(__APPLE__) || defined(__FreeBSD__)
#define FL_NET_SOCK_HOSTED 1
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

typedef struct {
    int fd;
    fl_net_sock_type_t type;
    int af;
    unsigned in_use;
} fl_net_sock_slot_t;

static fl_net_sock_slot_t s_socks[FL_NET_SOCK_TABLE_MAX];
static unsigned s_sock_inited;
static int s_last_sock_errno;

static fl_result_t sock_fail_from_errno(void) {
    s_last_sock_errno = errno;
    if (errno == EACCES)
        return FL_RESULT_ACCES;
    if (errno == EADDRINUSE || errno == EADDRNOTAVAIL)
        return FL_RESULT_BUSY;
    if (errno == ECONNREFUSED)
        return FL_RESULT_NOENT;
    if (errno == ETIMEDOUT)
        return FL_RESULT_TIMEDOUT;
    return FL_RESULT_ERR;
}

static fl_net_sock_slot_t *sock_lookup(fl_net_sock_handle_t h) {
    if (h <= 0 || (unsigned)h > FL_NET_SOCK_TABLE_MAX)
        return NULL;
    if (!s_socks[h - 1].in_use)
        return NULL;
    return &s_socks[h - 1];
}

fl_result_t fl_net_sock_init(void) {
    if (s_sock_inited)
        return FL_RESULT_OK;
    memset(s_socks, 0, sizeof(s_socks));
    s_sock_inited = 1u;
    return FL_RESULT_OK;
}

void fl_net_sock_shutdown(void) {
    for (unsigned i = 0; i < FL_NET_SOCK_TABLE_MAX; i++) {
        if (s_socks[i].in_use) {
#if defined(FL_NET_SOCK_HOSTED)
            if (s_socks[i].fd >= 0)
                close(s_socks[i].fd);
#endif
            s_socks[i].in_use = 0u;
            s_socks[i].fd = -1;
        }
    }
    s_sock_inited = 0u;
}

fl_result_t fl_net_sock_open(fl_net_sock_type_t type, fl_net_sock_handle_t *out_handle) {
    return fl_net_sock_open_for(NULL, type, out_handle);
}

fl_result_t fl_net_sock_open_for(const fl_net_endpoint_t *endpoint_hint,
                                   fl_net_sock_type_t type,
                                   fl_net_sock_handle_t *out_handle) {
    unsigned i;
    int sock_type;
    int fd;
    int af = AF_INET;

    if (!out_handle)
        return FL_RESULT_INVAL;
    if (!s_sock_inited)
        fl_net_sock_init();

#if !defined(FL_NET_SOCK_HOSTED)
    (void)endpoint_hint;
    (void)type;
    *out_handle = FL_NET_SOCK_INVALID;
    return FL_RESULT_NOSYS;
#else
    if (endpoint_hint && endpoint_hint->family == FL_NET_ADDR_FAMILY_V6)
        af = AF_INET6;
    if (type == FL_NET_SOCK_TYPE_STREAM)
        sock_type = SOCK_STREAM;
    else if (type == FL_NET_SOCK_TYPE_DGRAM)
        sock_type = SOCK_DGRAM;
    else
        return FL_RESULT_INVAL;

    for (i = 0; i < FL_NET_SOCK_TABLE_MAX; i++) {
        if (!s_socks[i].in_use)
            break;
    }
    if (i >= FL_NET_SOCK_TABLE_MAX)
        return FL_RESULT_BUSY;

    fd = net_host_socket(af, sock_type, 0);
    if (fd < 0)
        return sock_fail_from_errno();

#if defined(__APPLE__)
    if (sock_type == SOCK_STREAM) {
        int nosigpipe = 1;
        (void)setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &nosigpipe, sizeof(nosigpipe));
    }
#endif
    if (af == AF_INET6) {
        int v6only = 1;
        (void)setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &v6only, sizeof(v6only));
    }

    s_socks[i].fd = fd;
    s_socks[i].type = type;
    s_socks[i].af = af;
    s_socks[i].in_use = 1u;
    *out_handle = (fl_net_sock_handle_t)(i + 1);
    return FL_RESULT_OK;
#endif
}

fl_result_t fl_net_sock_close(fl_net_sock_handle_t handle) {
    fl_net_sock_slot_t *s = sock_lookup(handle);
    if (!s)
        return FL_RESULT_INVAL;

#if defined(FL_NET_SOCK_HOSTED)
    if (s->fd >= 0)
        close(s->fd);
#endif
    s->fd = -1;
    s->in_use = 0u;
    return FL_RESULT_OK;
}

#if defined(FL_NET_SOCK_HOSTED)
static void sock_sin4(struct sockaddr_in *sa, uint32_t addr_be, uint16_t port_host) {
    memset(sa, 0, sizeof(*sa));
    sa->sin_family = AF_INET;
    sa->sin_addr.s_addr = addr_be;
    /* First-class internal helper; libc htons is fine on hosted but we
     * route through fl_net_htons so the entire tree uses one entry point. */
    sa->sin_port = fl_net_htons(port_host);
}

static void sock_sin6(struct sockaddr_in6 *sa, const uint8_t v6_be[16], uint16_t port_host) {
    memset(sa, 0, sizeof(*sa));
    sa->sin6_family = AF_INET6;
    sa->sin6_port = fl_net_htons(port_host);
    if (v6_be)
        memcpy(&sa->sin6_addr, v6_be, 16);
}

static fl_result_t endpoint_to_sockaddr(const fl_net_endpoint_t *ep,
                                          struct sockaddr_storage *ss,
                                          socklen_t *slen_out) {
    if (!ep || !ss || !slen_out)
        return FL_RESULT_INVAL;
    if (ep->family == FL_NET_ADDR_FAMILY_V4) {
        struct sockaddr_in *sa = (struct sockaddr_in *)ss;
        sock_sin4(sa, ep->addr.v4_be, ep->port_host);
        *slen_out = (socklen_t)sizeof(*sa);
        return FL_RESULT_OK;
    }
    if (ep->family == FL_NET_ADDR_FAMILY_V6) {
        struct sockaddr_in6 *sa = (struct sockaddr_in6 *)ss;
        sock_sin6(sa, ep->addr.v6_be, ep->port_host);
        *slen_out = (socklen_t)sizeof(*sa);
        return FL_RESULT_OK;
    }
    return FL_RESULT_INVAL;
}

static fl_result_t sockaddr_to_endpoint(const struct sockaddr *sa, socklen_t slen,
                                          fl_net_endpoint_t *out) {
    if (!sa || !out)
        return FL_RESULT_INVAL;
    if (sa->sa_family == AF_INET && slen >= (socklen_t)sizeof(struct sockaddr_in)) {
        const struct sockaddr_in *sin = (const struct sockaddr_in *)sa;
        fl_net_endpoint_from_v4(sin->sin_addr.s_addr, fl_net_ntohs(sin->sin_port), out);
        return FL_RESULT_OK;
    }
    if (sa->sa_family == AF_INET6 && slen >= (socklen_t)sizeof(struct sockaddr_in6)) {
        const struct sockaddr_in6 *sin6 = (const struct sockaddr_in6 *)sa;
        fl_net_endpoint_from_v6((const uint8_t *)&sin6->sin6_addr,
                                fl_net_ntohs(sin6->sin6_port), out);
        return FL_RESULT_OK;
    }
    return FL_RESULT_INVAL;
}
#endif

fl_result_t fl_net_sock_bind(fl_net_sock_handle_t handle, uint32_t addr_be, uint16_t port_host) {
#if !defined(FL_NET_SOCK_HOSTED)
    (void)handle;
    (void)addr_be;
    (void)port_host;
    return FL_RESULT_NOSYS;
#else
    fl_net_sock_slot_t *s = sock_lookup(handle);
    struct sockaddr_in sa;

    if (!s)
        return FL_RESULT_INVAL;
    /* SO_REUSEADDR keeps demo / test bind from failing when a prior STREAM
     * listener left the port in TIME_WAIT. UDP doesn't have TIME_WAIT and
     * enabling it on a DGRAM socket lets two demo clients bind the same
     * port simultaneously (and silently race for recv), so gate by type.
     * Best-effort: setsockopt errors are ignored. */
    if (s->type == FL_NET_SOCK_TYPE_STREAM) {
        int yes = 1;
        (void)setsockopt(s->fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    }
    sock_sin4(&sa, addr_be, port_host);
    if (bind(s->fd, (struct sockaddr *)&sa, sizeof(sa)) != 0)
        return sock_fail_from_errno();
    return FL_RESULT_OK;
#endif
}

fl_result_t fl_net_sock_listen(fl_net_sock_handle_t handle, int backlog) {
#if !defined(FL_NET_SOCK_HOSTED)
    (void)handle;
    (void)backlog;
    return FL_RESULT_NOSYS;
#else
    fl_net_sock_slot_t *s = sock_lookup(handle);
    if (!s || s->type != FL_NET_SOCK_TYPE_STREAM)
        return FL_RESULT_INVAL;
    if (backlog <= 0)
        backlog = FL_NET_SOCK_DEFAULT_LISTEN_BACKLOG;
    if (listen(s->fd, backlog) != 0)
        return sock_fail_from_errno();
    return FL_RESULT_OK;
#endif
}

fl_result_t fl_net_sock_accept(fl_net_sock_handle_t listen_handle,
                               fl_net_sock_handle_t *out_client) {
#if !defined(FL_NET_SOCK_HOSTED)
    (void)listen_handle;
    (void)out_client;
    return FL_RESULT_NOSYS;
#else
    fl_net_sock_slot_t *listen = sock_lookup(listen_handle);
    struct sockaddr_storage peer;
    socklen_t peer_len = sizeof(peer);
    int cfd;
    unsigned i;
    fl_net_sock_handle_t client_h;
    int client_af = AF_INET;

    if (!listen || !out_client || listen->type != FL_NET_SOCK_TYPE_STREAM)
        return FL_RESULT_INVAL;

    cfd = accept(listen->fd, (struct sockaddr *)&peer, &peer_len);
    if (cfd < 0) {
        /* Differentiate "nothing pending right now" from a real error
         * so the caller (e.g. fl_net_server_accept_pending) can keep
         * polling on TIMEDOUT and surface BUSY / ERR otherwise. */
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
            return FL_RESULT_TIMEDOUT;
        return sock_fail_from_errno();
    }

    for (i = 0; i < FL_NET_SOCK_TABLE_MAX; i++) {
        if (!s_socks[i].in_use)
            break;
    }
    if (i >= FL_NET_SOCK_TABLE_MAX) {
        close(cfd);
        return FL_RESULT_BUSY;
    }

    if (peer.ss_family == AF_INET6)
        client_af = AF_INET6;

    s_socks[i].fd = cfd;
    s_socks[i].type = FL_NET_SOCK_TYPE_STREAM;
    s_socks[i].af = client_af;
    s_socks[i].in_use = 1u;
    client_h = (fl_net_sock_handle_t)(i + 1);
    *out_client = client_h;
    return FL_RESULT_OK;
#endif
}

fl_result_t fl_net_sock_connect(fl_net_sock_handle_t handle, uint32_t peer_be,
                                uint16_t port_host) {
#if !defined(FL_NET_SOCK_HOSTED)
    (void)handle;
    (void)peer_be;
    (void)port_host;
    return FL_RESULT_NOSYS;
#else
    fl_net_sock_slot_t *s = sock_lookup(handle);
    struct sockaddr_in sa;

    if (!s)
        return FL_RESULT_INVAL;
    sock_sin4(&sa, peer_be, port_host);
    if (connect(s->fd, (struct sockaddr *)&sa, sizeof(sa)) != 0)
        return sock_fail_from_errno();
    return FL_RESULT_OK;
#endif
}

fl_result_t fl_net_sock_send(fl_net_sock_handle_t handle, const void *buf, size_t len,
                             size_t *sent) {
#if !defined(FL_NET_SOCK_HOSTED)
    (void)handle;
    (void)buf;
    (void)len;
    (void)sent;
    return FL_RESULT_NOSYS;
#else
    fl_net_sock_slot_t *s = sock_lookup(handle);
    ssize_t n;
    int flags = 0;

    if (!s || !buf || !sent)
        return FL_RESULT_INVAL;
    if (len > FL_NET_SOCK_IO_CHUNK_MAX)
        len = FL_NET_SOCK_IO_CHUNK_MAX;

    if (s->type == FL_NET_SOCK_TYPE_STREAM) {
#if defined(MSG_NOSIGNAL)
        flags = MSG_NOSIGNAL;
#endif
    }
    n = send(s->fd, buf, len, flags);
    if (n < 0)
        return FL_RESULT_ERR;
    *sent = (size_t)n;
    return FL_RESULT_OK;
#endif
}

fl_result_t fl_net_sock_recv(fl_net_sock_handle_t handle, void *buf, size_t cap, size_t *got,
                             unsigned timeout_ms) {
#if !defined(FL_NET_SOCK_HOSTED)
    (void)handle;
    (void)buf;
    (void)cap;
    (void)got;
    (void)timeout_ms;
    return FL_RESULT_NOSYS;
#else
    fl_net_sock_slot_t *s = sock_lookup(handle);
    ssize_t n;

    if (!s || !buf || !got)
        return FL_RESULT_INVAL;
    if (cap > FL_NET_SOCK_IO_CHUNK_MAX)
        cap = FL_NET_SOCK_IO_CHUNK_MAX;

    if (timeout_ms > 0u) {
        fd_set rfds;
        struct timeval tv;
        int sel;

        FD_ZERO(&rfds);
        FD_SET(s->fd, &rfds);
        tv.tv_sec = (time_t)(timeout_ms / 1000u);
        tv.tv_usec = (suseconds_t)((timeout_ms % 1000u) * 1000u);
        sel = select(s->fd + 1, &rfds, NULL, NULL, &tv);
        if (sel == 0)
            return FL_RESULT_TIMEDOUT;
        if (sel < 0)
            return FL_RESULT_ERR;
    }

    n = recv(s->fd, buf, cap, 0);
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return FL_RESULT_TIMEDOUT;
        return FL_RESULT_ERR;
    }
    if (n == 0)
        return FL_RESULT_EOF;
    *got = (size_t)n;
    return FL_RESULT_OK;
#endif
}

fl_result_t fl_net_sock_set_nonblock(fl_net_sock_handle_t handle, int nonblock) {
#if !defined(FL_NET_SOCK_HOSTED)
    (void)handle;
    (void)nonblock;
    return FL_RESULT_NOSYS;
#else
    fl_net_sock_slot_t *s = sock_lookup(handle);
    int flags;

    if (!s)
        return FL_RESULT_INVAL;
    flags = fcntl(s->fd, F_GETFL, 0);
    if (flags < 0)
        return FL_RESULT_ERR;
    if (nonblock)
        flags |= O_NONBLOCK;
    else
        flags &= ~O_NONBLOCK;
    if (fcntl(s->fd, F_SETFL, flags) != 0)
        return FL_RESULT_ERR;
    return FL_RESULT_OK;
#endif
}

fl_result_t fl_net_sock_connect_from(fl_net_sock_handle_t handle,
                                     uint32_t local_be,
                                     uint32_t peer_be, uint16_t port_host) {
#if !defined(FL_NET_SOCK_HOSTED)
    (void)handle;
    (void)local_be;
    (void)peer_be;
    (void)port_host;
    return FL_RESULT_NOSYS;
#else
    fl_net_sock_slot_t *s = sock_lookup(handle);
    struct sockaddr_in sa;
    if (!s)
        return FL_RESULT_INVAL;
    if (local_be != 0u) {
        if (s->type == FL_NET_SOCK_TYPE_STREAM) {
            int yes = 1;
            (void)setsockopt(s->fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
        }
        sock_sin4(&sa, local_be, 0);
        if (bind(s->fd, (struct sockaddr *)&sa, sizeof(sa)) != 0)
            return sock_fail_from_errno();
    }
    sock_sin4(&sa, peer_be, port_host);
    if (connect(s->fd, (struct sockaddr *)&sa, sizeof(sa)) != 0)
        return sock_fail_from_errno();
    return FL_RESULT_OK;
#endif
}

fl_result_t fl_net_sock_bind_ep(fl_net_sock_handle_t handle, const fl_net_endpoint_t *local) {
#if !defined(FL_NET_SOCK_HOSTED)
    (void)handle;
    (void)local;
    return FL_RESULT_NOSYS;
#else
    fl_net_sock_slot_t *s = sock_lookup(handle);
    struct sockaddr_storage ss;
    socklen_t slen;

    if (!s || !local)
        return FL_RESULT_INVAL;
    if (s->type == FL_NET_SOCK_TYPE_STREAM) {
        int yes = 1;
        (void)setsockopt(s->fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    }
    if (endpoint_to_sockaddr(local, &ss, &slen) != FL_RESULT_OK)
        return FL_RESULT_INVAL;
    if (bind(s->fd, (struct sockaddr *)&ss, slen) != 0)
        return sock_fail_from_errno();
    return FL_RESULT_OK;
#endif
}

fl_result_t fl_net_sock_connect_from_ep(fl_net_sock_handle_t handle,
                                        const fl_net_endpoint_t *local,
                                        const fl_net_endpoint_t *peer) {
#if !defined(FL_NET_SOCK_HOSTED)
    (void)handle;
    (void)local;
    (void)peer;
    return FL_RESULT_NOSYS;
#else
    fl_net_sock_slot_t *s = sock_lookup(handle);
    struct sockaddr_storage ss;
    socklen_t slen;
    fl_net_endpoint_t bind_ep;

    if (!s || !peer)
        return FL_RESULT_INVAL;
    if (local && local->family != 0u) {
        if (s->type == FL_NET_SOCK_TYPE_STREAM) {
            int yes = 1;
            (void)setsockopt(s->fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
        }
        bind_ep = *local;
        bind_ep.port_host = 0u;
        if (endpoint_to_sockaddr(&bind_ep, &ss, &slen) != FL_RESULT_OK)
            return FL_RESULT_INVAL;
        if (bind(s->fd, (struct sockaddr *)&ss, slen) != 0)
            return sock_fail_from_errno();
    }
    if (endpoint_to_sockaddr(peer, &ss, &slen) != FL_RESULT_OK)
        return FL_RESULT_INVAL;
    if (connect(s->fd, (struct sockaddr *)&ss, slen) != 0)
        return sock_fail_from_errno();
    return FL_RESULT_OK;
#endif
}

fl_result_t fl_net_sock_peer_endpoint(fl_net_sock_handle_t handle, fl_net_endpoint_t *out) {
#if !defined(FL_NET_SOCK_HOSTED)
    (void)handle;
    (void)out;
    return FL_RESULT_NOSYS;
#else
    fl_net_sock_slot_t *s = sock_lookup(handle);
    struct sockaddr_storage ss;
    socklen_t slen = (socklen_t)sizeof(ss);

    if (!s || !out)
        return FL_RESULT_INVAL;
    if (getpeername(s->fd, (struct sockaddr *)&ss, &slen) != 0)
        return FL_RESULT_ERR;
    return sockaddr_to_endpoint((struct sockaddr *)&ss, slen, out);
#endif
}

fl_result_t fl_net_sock_local_endpoint(fl_net_sock_handle_t handle, fl_net_endpoint_t *out) {
#if !defined(FL_NET_SOCK_HOSTED)
    (void)handle;
    (void)out;
    return FL_RESULT_NOSYS;
#else
    fl_net_sock_slot_t *s = sock_lookup(handle);
    struct sockaddr_storage ss;
    socklen_t slen = (socklen_t)sizeof(ss);

    if (!s || !out)
        return FL_RESULT_INVAL;
    if (getsockname(s->fd, (struct sockaddr *)&ss, &slen) != 0)
        return FL_RESULT_ERR;
    return sockaddr_to_endpoint((struct sockaddr *)&ss, slen, out);
#endif
}

fl_result_t fl_net_sock_peer_ipv4(fl_net_sock_handle_t handle, uint32_t *out_be) {
#if !defined(FL_NET_SOCK_HOSTED)
    (void)handle;
    (void)out_be;
    return FL_RESULT_NOSYS;
#else
    fl_net_sock_slot_t *s = sock_lookup(handle);
    struct sockaddr_in sa;
    socklen_t slen = sizeof(sa);
    if (!s || !out_be)
        return FL_RESULT_INVAL;
    if (getpeername(s->fd, (struct sockaddr *)&sa, &slen) != 0)
        return FL_RESULT_ERR;
    *out_be = sa.sin_addr.s_addr;
    return FL_RESULT_OK;
#endif
}

fl_result_t fl_net_sock_local_ipv4(fl_net_sock_handle_t handle, uint32_t *out_be) {
#if !defined(FL_NET_SOCK_HOSTED)
    (void)handle;
    (void)out_be;
    return FL_RESULT_NOSYS;
#else
    fl_net_sock_slot_t *s = sock_lookup(handle);
    struct sockaddr_in sa;
    socklen_t slen = sizeof(sa);
    if (!s || !out_be)
        return FL_RESULT_INVAL;
    if (getsockname(s->fd, (struct sockaddr *)&sa, &slen) != 0)
        return FL_RESULT_ERR;
    *out_be = sa.sin_addr.s_addr;
    return FL_RESULT_OK;
#endif
}

int fl_net_sock_host_fd(fl_net_sock_handle_t handle) {
#if !defined(FL_NET_SOCK_HOSTED)
    (void)handle;
    return -1;
#else
    fl_net_sock_slot_t *s = sock_lookup(handle);

    if (!s || s->fd < 0)
        return -1;
    return s->fd;
#endif
}

int fl_net_sock_last_errno(void) {
    return s_last_sock_errno;
}

void fl_net_sock_clear_errno(void) {
    s_last_sock_errno = 0;
}
