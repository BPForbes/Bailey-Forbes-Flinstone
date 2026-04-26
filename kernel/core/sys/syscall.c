#include "fl/syscall.h"
#include "fl/ipc.h"
#include "vrt.h"

#include <unistd.h>

void fl_sys_bootstrap(void) {
    vrt_init();
}

void fl_sys_shutdown(void) {
    vrt_shutdown();
}

long fl_syscall_dispatch(fl_syscall_no_t no, uintptr_t a0, uintptr_t a1, uintptr_t a2, uintptr_t a3) {
    (void)a3;
    switch (no) {
        case FL_SYS_WRITE: {
            const char *buf = (const char *)a0;
            size_t n = (size_t)a1;
            if (!buf) return -1;
            ssize_t wr = write(STDOUT_FILENO, buf, n);
            return (wr < 0) ? -1 : (long)wr;
        }
        case FL_SYS_READ: {
            char *buf = (char *)a0;
            size_t n = (size_t)a1;
            if (!buf) return -1;
            ssize_t rd = read(STDIN_FILENO, buf, n);
            return (rd < 0) ? -1 : (long)rd;
        }
        case FL_SYS_PIPE_CREATE: {
            size_t sz = (size_t)a0;
            pipe_t *p = pipe_create(sz);
            if (!p) return -1;
            vrt_handle_t h = vrt_alloc(VRT_TYPE_PIPE, p, "pipe", sz);
            if (h == VRT_HANDLE_INVALID) {
                pipe_destroy(p);
                return -1;
            }
            return (long)h;
        }
        case FL_SYS_PIPE_READ: {
            pipe_t *p = (pipe_t *)vrt_resource((vrt_handle_t)a0);
            return p ? (long)pipe_read(p, (void *)a1, (size_t)a2) : -1;
        }
        case FL_SYS_PIPE_WRITE: {
            pipe_t *p = (pipe_t *)vrt_resource((vrt_handle_t)a0);
            return p ? (long)pipe_write(p, (const void *)a1, (size_t)a2) : -1;
        }
        case FL_SYS_MSGQ_CREATE: {
            size_t max_msgs = (size_t)a0;
            size_t msg_sz = (size_t)a1;
            msgq_t *q = msgq_create(max_msgs, msg_sz);
            if (!q) return -1;
            vrt_handle_t h = vrt_alloc(VRT_TYPE_MSGQ, q, "msgq", msg_sz);
            if (h == VRT_HANDLE_INVALID) {
                msgq_destroy(q);
                return -1;
            }
            return (long)h;
        }
        case FL_SYS_MSGQ_SEND: {
            msgq_t *q = (msgq_t *)vrt_resource((vrt_handle_t)a0);
            return q ? (long)msgq_send(q, (const void *)a1, (size_t)a2) : -1;
        }
        case FL_SYS_MSGQ_RECV: {
            msgq_t *q = (msgq_t *)vrt_resource((vrt_handle_t)a0);
            return q ? (long)msgq_receive(q, (void *)a1, (size_t)a2, 0) : -1;
        }
        case FL_SYS_CLOSE: {
            vrt_entry_t entry;
            vrt_handle_t h = (vrt_handle_t)a0;
            if (vrt_get(h, &entry) != 0) return -1;
            if (entry.type == VRT_TYPE_PIPE) {
                pipe_destroy((pipe_t *)entry.resource);
            } else if (entry.type == VRT_TYPE_MSGQ) {
                msgq_destroy((msgq_t *)entry.resource);
            } else {
                return -1;
            }
            vrt_free(h);
            return 0;
        }
        default:
            return -1;
    }
}
