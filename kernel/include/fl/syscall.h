#ifndef FL_SYSCALL_H
#define FL_SYSCALL_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    /* FL_SYS_WRITE: Write to stdout (fd argument is ignored)
     * Arguments:
     *   a0: pointer to buffer (const char*)
     *   a1: length (size_t)
     *   a2, a3: unused/ignored
     * Returns: ssize_t number of bytes written, or -1 on error
     * Note: Always writes to STDOUT_FILENO; callers must not expect
     *       file-descriptor-controlled write behavior.
     * Implementation: kernel/core/sys/syscall.c */
    FL_SYS_WRITE = 1,

    /* FL_SYS_READ: Read from stdin (fd argument is ignored)
     * Arguments:
     *   a0: pointer to buffer (char*)
     *   a1: length (size_t)
     *   a2, a3: unused/ignored
     * Returns: ssize_t number of bytes read, or -1 on error
     * Note: Always reads from STDIN_FILENO; callers must not expect
     *       file-descriptor-controlled read behavior.
     * Implementation: kernel/core/sys/syscall.c */
    FL_SYS_READ = 2,

    FL_SYS_PIPE_CREATE = 3,
    FL_SYS_PIPE_READ = 4,
    FL_SYS_PIPE_WRITE = 5,
    FL_SYS_MSGQ_CREATE = 6,
    FL_SYS_MSGQ_SEND = 7,
    FL_SYS_MSGQ_RECV = 8,
    FL_SYS_CLOSE = 9,

    /* KeyQuorum monitor queries (FL-03). Append only; the work is done by the KeyQuorum
     * driver through the hook below, never by the kernel. Every call returns -1 for a bad
     * call (NULL argument, zero cap, no hook registered) and -(FL_SYS_KQ_ERR_BASE + result)
     * for a refusal by KeyQuorum, so the two never collide (fl_kq_cp_result_t starts at 1).
     * Paths are NUL-terminated host pointers (the VM bridge translates guest offsets). */

    /* a0: path, a1: fl_kq_op_t -> 0 ok, else -(100 + fl_kq_cp_result_t). Changes nothing. */
    FL_SYS_KQ_CHECK = 10,
    /* a0: path -> 0 ok, else -(100 + fl_kq_cp_result_t). */
    FL_SYS_KQ_VERIFY = 11,
    /* a0: path, a1: out buffer, a2: cap -> bytes written, else -(100 + result). */
    FL_SYS_KQ_HISTORY = 12,
} fl_syscall_no_t;

#define FL_SYS_KQ_ERR_BASE 100

/* Registered by the KeyQuorum build (kernel/core/vfs/kq_syscall.c) like
 * fl_net_server_set_kq_hook; NULL, or a NULL member, makes the call return -1. */
typedef struct {
    long (*check)(const char *path, unsigned op);
    long (*verify)(const char *path);
    long (*history)(const char *path, uint8_t *out, size_t cap);
} fl_syscall_kq_ops_t;

void fl_syscall_set_kq_ops(const fl_syscall_kq_ops_t *ops);

void fl_sys_bootstrap(void);
void fl_sys_shutdown(void);
long fl_syscall_dispatch(fl_syscall_no_t no, uintptr_t a0, uintptr_t a1, uintptr_t a2, uintptr_t a3);

#ifdef __cplusplus
}
#endif

#endif /* FL_SYSCALL_H */
