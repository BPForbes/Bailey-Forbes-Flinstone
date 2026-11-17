/*
 * FL_SYS_KQ_* (FL-03): the KeyQuorum side of the syscall hook. syscall.c holds only function
 * pointers; this file maps the fl_kq_cp_* checkpoints onto them and decides nothing itself.
 */
#include "kq_checkpoint.h"
#include "fl/kq.h"
#include "fl/syscall.h"

#include <string.h>

static long refusal(fl_kq_cp_result_t r) {
    return -(long)(FL_SYS_KQ_ERR_BASE + (int)r);
}

static long kq_sys_verify(const char *path) {
    fl_kq_cp_result_t r = fl_kq_cp_verify(path);

    return r == FL_KQ_CP_OK ? 0 : refusal(r);
}

/* Changes nothing. fl_kq_pre_mutation is declared by fl/kq.h but not defined on this branch
 * (FL-02 / FL-09), so until it lands the check is the op's validity plus the container's. */
static long kq_sys_check(const char *path, unsigned op) {
    if (op < FL_KQ_OP_CREATE || op > FL_KQ_OP_RECEIVE)
        return refusal(FL_KQ_CP_INVALID);
    return kq_sys_verify(path);
}

static long kq_sys_history(const char *path, uint8_t *out, size_t cap) {
    fl_kq_buf_t lines = {0};
    fl_kq_cp_result_t r = fl_kq_cp_history(path, &lines);
    long n;

    if (r != FL_KQ_CP_OK)
        return refusal(r);
    if (lines.len > cap || lines.len > 0x7fffffffu) {
        n = refusal(FL_KQ_CP_TOO_LARGE);
    } else {
        memcpy(out, lines.data, lines.len);
        n = (long)lines.len;
    }
    fl_kq_buf_free(&lines);
    return n;
}

void fl_kq_syscall_install(void) {
    static const fl_syscall_kq_ops_t ops = {kq_sys_check, kq_sys_verify, kq_sys_history};

    fl_syscall_set_kq_ops(&ops);
}
