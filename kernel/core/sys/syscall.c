#include "fl/syscall.h"
#include "fl/ipc.h"
#include "vrt.h"

#include <stdint.h>
#include <unistd.h>

/**
 * Perform early system initialization required before normal operation.
 *
 * This function initializes core kernel subsystems and runtime resources
 * that must be ready before other system services are used. Call once
 * during boot sequence.
 */
void fl_sys_bootstrap(void) {
    vrt_init();
}

/**
 * Shut down the virtual resource table (VRT) subsystem and release associated resources.
 *
 * This performs any teardown required for VRT-managed resources so the system can
 * transition to a stopped or cleaned-up state.
 */
void fl_sys_shutdown(void) {
    vrt_shutdown();
}

/**
 * Dispatches a system call by number and executes the corresponding kernel operation.
 *
 * Supported syscalls perform standard I/O, create/read/write pipes, create/send/receive message
 * queues, and close VRT resources. Arguments a0–a3 are interpreted according to the syscall.
 *
 * @param no Syscall number specifying the operation to perform.
 * @param a0 First syscall argument; meaning depends on `no` (e.g. for FL_SYS_WRITE: pointer to buffer;
 *           FL_SYS_READ: pointer to buffer; FL_SYS_PIPE_CREATE: pipe size; FL_SYS_PIPE_* and
 *           FL_SYS_MSGQ_*: VRT handle passed as an integer).
 * @param a1 Second syscall argument; meaning depends on `no` (e.g. for FL_SYS_WRITE/READ: byte count;
 *           FL_SYS_MSGQ_CREATE: message size; for read/write/send: pointer to data or byte count as applicable).
 * @param a2 Third syscall argument; meaning depends on `no` (commonly a byte count or flags for receive).
 * @param a3 Fourth syscall argument; meaning depends on `no` (used as timeout/flags for message-queue receive).
 *
 * @returns `-1` on invalid inputs, failed operations, or unsupported syscall numbers; otherwise
 *          the syscall-specific result: number of bytes read/written, a non-negative VRT handle
 *          for created resources, or `0` for a successful close.
 */
long fl_syscall_dispatch(fl_syscall_no_t no, uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3) {
    switch (no) {
        case FL_SYS_WRITE: {
            const char *buf = (const char *)(uintptr_t)a0;
            size_t n = (size_t)a1;
            if (!buf) return -1;
            ssize_t wr = write(STDOUT_FILENO, buf, n);
            return (wr < 0) ? -1 : (long)wr;
        }
        case FL_SYS_READ: {
            char *buf = (char *)(uintptr_t)a0;
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
            vrt_handle_t h = (vrt_handle_t)(uintptr_t)a0;
            if (vrt_get(h, &entry) != 0 || entry.type != VRT_TYPE_PIPE || !entry.resource)
                return -1;
            return (long)pipe_read((pipe_t *)entry.resource, (void *)(uintptr_t)a1, (size_t)a2);
        }
        case FL_SYS_PIPE_WRITE: {
            vrt_entry_t entry;
            vrt_handle_t h = (vrt_handle_t)(uintptr_t)a0;
            if (vrt_get(h, &entry) != 0 || entry.type != VRT_TYPE_PIPE || !entry.resource)
                return -1;
            return (long)pipe_write((pipe_t *)entry.resource, (const void *)(uintptr_t)a1, (size_t)a2);
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
            vrt_handle_t h = (vrt_handle_t)(uintptr_t)a0;
            if (vrt_get(h, &entry) != 0 || entry.type != VRT_TYPE_MSGQ || !entry.resource)
                return -1;
            return (long)msgq_send((msgq_t *)entry.resource, (const void *)(uintptr_t)a1, (size_t)a2);
        }
        case FL_SYS_MSGQ_RECV: {
            vrt_entry_t entry;
            vrt_handle_t h = (vrt_handle_t)(uintptr_t)a0;
            if (vrt_get(h, &entry) != 0 || entry.type != VRT_TYPE_MSGQ || !entry.resource)
                return -1;
            return (long)msgq_receive((msgq_t *)entry.resource, (void *)(uintptr_t)a1, (size_t)a2, a3);
        }
        case FL_SYS_CLOSE: {
            vrt_entry_t entry;
            vrt_handle_t h = (vrt_handle_t)(uintptr_t)a0;
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
