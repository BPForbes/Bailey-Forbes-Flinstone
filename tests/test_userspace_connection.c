#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "fl/syscall.h"

static void test_pipe_syscalls(void) {
    fl_sys_bootstrap();
    long h = fl_syscall_dispatch(FL_SYS_PIPE_CREATE, 64, 0, 0, 0);
    assert(h >= 0);

    const char *msg = "hello-userspace";
    long w = fl_syscall_dispatch(FL_SYS_PIPE_WRITE, (uintptr_t)h, (uintptr_t)msg, strlen(msg), 0);
    assert(w == (long)strlen(msg));

    char out[64] = {0};
    long r = fl_syscall_dispatch(FL_SYS_PIPE_READ, (uintptr_t)h, (uintptr_t)out, sizeof(out), 0);
    assert(r == (long)strlen(msg));
    assert(memcmp(out, msg, strlen(msg)) == 0);
    assert(fl_syscall_dispatch(FL_SYS_CLOSE, (uintptr_t)h, 0, 0, 0) == 0);
    fl_sys_shutdown();
}

static void test_msgq_syscalls(void) {
    fl_sys_bootstrap();
    long h = fl_syscall_dispatch(FL_SYS_MSGQ_CREATE, 4, 32, 0, 0);
    assert(h >= 0);

    const char *msg = "kernel->userspace";
    long s = fl_syscall_dispatch(FL_SYS_MSGQ_SEND, (uintptr_t)h, (uintptr_t)msg, strlen(msg) + 1, 0);
    assert(s == 0);

    char out[32] = {0};
    long rc = fl_syscall_dispatch(FL_SYS_MSGQ_RECV, (uintptr_t)h, (uintptr_t)out, sizeof(out), 0);
    assert(rc == 0);
    assert(strcmp(out, msg) == 0);
    assert(fl_syscall_dispatch(FL_SYS_CLOSE, (uintptr_t)h, 0, 0, 0) == 0);
    fl_sys_shutdown();
}

int main(void) {
    test_pipe_syscalls();
    test_msgq_syscalls();
    puts("userspace connection tests: OK");
    return 0;
}
