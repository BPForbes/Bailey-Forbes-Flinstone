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
} fl_syscall_no_t;

/**
 * Opcode bundled for fl_syscall_dispatch: distinct from uint64_t bridge args so
 * syscall number and a0 cannot be accidentally transposed at call sites.
 */
typedef struct fl_syscall_invocation {
    fl_syscall_no_t number;
} fl_syscall_invocation_t;

static inline fl_syscall_invocation_t fl_syscall_invoke(fl_syscall_no_t number) {
    fl_syscall_invocation_t inv;
    inv.number = number;
    return inv;
}

void fl_sys_bootstrap(void);
void fl_sys_shutdown(void);
long fl_syscall_dispatch(fl_syscall_invocation_t invocation,
                         uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3);

#ifdef __cplusplus
}
#endif

#endif /* FL_SYSCALL_H */
