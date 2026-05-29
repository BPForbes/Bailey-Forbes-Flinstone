#include "net_socket.h"

#include "net_endian.h"
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
    unsigned in_use;
} fl_net_sock_slot_t;

static fl_net_sock_slot_t s_socks[FL_NET_SOCK_TABLE_MAX];
static unsigned s_sock_inited;

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
    unsigned i;
    int sock_type;
    int fd;

    if (!out_handle)
        return FL_RESULT_INVAL;
    if (!s_sock_inited)
        fl_net_sock_init();

#if !defined(FL_NET_SOCK_HOSTED)
    (void)type;
    *out_handle = FL_NET_SOCK_INVALID;
    return FL_RESULT_NOSYS;
#else
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

    fd = net_host_socket(AF_INET, sock_type, 0);
    if (fd < 0)
        return FL_RESULT_ERR;

#if defined(__APPLE__)
    if (sock_type == SOCK_STREAM) {
        int nosigpipe = 1;
        (void)setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &nosigpipe, sizeof(nosigpipe));
    }
#endif

    s_socks[i].fd = fd;
    s_socks[i].type = type;
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
    /* SO_REUSEADDR keeps demo / test bind from failing when a prior listener
     * left the port in TIME_WAIT. Best-effort: ignore setsockopt errors. */
    {
        int yes = 1;
        (void)setsockopt(s->fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    }
    sock_sin4(&sa, addr_be, port_host);
    if (bind(s->fd, (struct sockaddr *)&sa, sizeof(sa)) != 0)
        return FL_RESULT_ERR;
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
        return FL_RESULT_ERR;
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
    struct sockaddr_in peer;
    socklen_t peer_len = sizeof(peer);
    int cfd;
    unsigned i;
    fl_net_sock_handle_t client_h;

    if (!listen || !out_client || listen->type != FL_NET_SOCK_TYPE_STREAM)
        return FL_RESULT_INVAL;

    cfd = accept(listen->fd, (struct sockaddr *)&peer, &peer_len);
    if (cfd < 0)
        return FL_RESULT_ERR;

    for (i = 0; i < FL_NET_SOCK_TABLE_MAX; i++) {
        if (!s_socks[i].in_use)
            break;
    }
    if (i >= FL_NET_SOCK_TABLE_MAX) {
        close(cfd);
        return FL_RESULT_BUSY;
    }

    s_socks[i].fd = cfd;
    s_socks[i].type = FL_NET_SOCK_TYPE_STREAM;
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
        return FL_RESULT_ERR;
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
        int yes = 1;
        (void)setsockopt(s->fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
        sock_sin4(&sa, local_be, 0);
        if (bind(s->fd, (struct sockaddr *)&sa, sizeof(sa)) != 0)
            return FL_RESULT_ERR;
    }
    sock_sin4(&sa, peer_be, port_host);
    if (connect(s->fd, (struct sockaddr *)&sa, sizeof(sa)) != 0)
        return FL_RESULT_ERR;
    return FL_RESULT_OK;
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
