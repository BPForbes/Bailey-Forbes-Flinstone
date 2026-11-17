#include "fl/syscall.h"
#include "fl/ipc.h"
#include "vrt.h"

#include <unistd.h>

/* No reference to KeyQuorum here: the KeyQuorum build registers its ops. */
static fl_syscall_kq_ops_t s_kq;
static int s_kq_set;

void fl_syscall_set_kq_ops(const fl_syscall_kq_ops_t *ops) {
    if (ops) {
        s_kq = *ops;
        s_kq_set = 1;
    } else {
        s_kq_set = 0;
    }
}

void fl_sys_bootstrap(void) {
    vrt_init();
}

void fl_sys_shutdown(void) {
    vrt_shutdown();
}

long fl_syscall_dispatch(fl_syscall_no_t no, uintptr_t a0, uintptr_t a1, uintptr_t a2, uintptr_t a3) {
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
            vrt_entry_t entry;
            vrt_handle_t h = (vrt_handle_t)a0;
            if (vrt_get(h, &entry) != 0 || entry.type != VRT_TYPE_PIPE || !entry.resource)
                return -1;
            return (long)pipe_read((pipe_t *)entry.resource, (void *)a1, (size_t)a2);
        }
        case FL_SYS_PIPE_WRITE: {
            vrt_entry_t entry;
            vrt_handle_t h = (vrt_handle_t)a0;
            if (vrt_get(h, &entry) != 0 || entry.type != VRT_TYPE_PIPE || !entry.resource)
                return -1;
            return (long)pipe_write((pipe_t *)entry.resource, (const void *)a1, (size_t)a2);
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
            vrt_entry_t entry;
            vrt_handle_t h = (vrt_handle_t)a0;
            if (vrt_get(h, &entry) != 0 || entry.type != VRT_TYPE_MSGQ || !entry.resource)
                return -1;
            return (long)msgq_send((msgq_t *)entry.resource, (const void *)a1, (size_t)a2);
        }
        case FL_SYS_MSGQ_RECV: {
            vrt_entry_t entry;
            vrt_handle_t h = (vrt_handle_t)a0;
            if (vrt_get(h, &entry) != 0 || entry.type != VRT_TYPE_MSGQ || !entry.resource)
                return -1;
            return (long)msgq_receive((msgq_t *)entry.resource, (void *)a1, (size_t)a2, (uint64_t)a3);
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
        case FL_SYS_KQ_CHECK:
            return (s_kq_set && s_kq.check && a0) ? s_kq.check((const char *)a0, (unsigned)a1) : -1;
        case FL_SYS_KQ_VERIFY:
            return (s_kq_set && s_kq.verify && a0) ? s_kq.verify((const char *)a0) : -1;
        case FL_SYS_KQ_HISTORY:
            return (s_kq_set && s_kq.history && a0 && a1 && a2)
                       ? s_kq.history((const char *)a0, (uint8_t *)a1, (size_t)a2)
                       : -1;
        default:
            return -1;
    }
}
