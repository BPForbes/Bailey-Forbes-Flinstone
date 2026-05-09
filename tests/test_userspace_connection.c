#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "fl/ipc.h"
#include "fl/syscall.h"

static void test_pipe_syscalls(void) {
    fl_sys_bootstrap();
    long h = fl_syscall_dispatch(FL_SYS_PIPE_CREATE, 64, 0, 0, 0);
    assert(h >= 0);
    assert(fl_syscall_dispatch(FL_SYS_PIPE_WRITE, (uintptr_t)h, 0, 1, 0) == -1);
    assert(fl_syscall_dispatch(FL_SYS_PIPE_READ, (uintptr_t)h, 0, 1, 0) == -1);

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
    char oversized[33] = {0};
    assert(fl_syscall_dispatch(FL_SYS_MSGQ_SEND, (uintptr_t)h, (uintptr_t)oversized, sizeof(oversized), 0) == -1);

    long s = fl_syscall_dispatch(FL_SYS_MSGQ_SEND, (uintptr_t)h, (uintptr_t)msg, strlen(msg) + 1, 0);
    assert(s == 0);

    char out[32] = {0};
    long rc = fl_syscall_dispatch(FL_SYS_MSGQ_RECV, (uintptr_t)h, (uintptr_t)out, sizeof(out), 0);
    assert(rc == 0);
    assert(strcmp(out, msg) == 0);

    struct timespec t0;
    struct timespec t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    assert(fl_syscall_dispatch(FL_SYS_MSGQ_RECV, (uintptr_t)h, (uintptr_t)out, sizeof(out), 5) == -1);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    {
        long long sec = (long long)(t1.tv_sec - t0.tv_sec);
        long long nsec = (long long)(t1.tv_nsec - t0.tv_nsec);
        if (nsec < 0) {
            sec--;
            nsec += 1000000000LL;
        }
        uint64_t elapsed_ns = (uint64_t)(sec * 1000000000LL + nsec);
        assert(elapsed_ns >= UINT64_C(5) * UINT64_C(1000000));
        if (elapsed_ns > UINT64_C(2000) * UINT64_C(1000000)) {
            fprintf(stderr, "Warning: msgq_recv timeout took %llu ms (expected ~5ms, tolerance 2000ms)\n",
                    (unsigned long long)(elapsed_ns / 1000000));
        }
    }

    assert(fl_syscall_dispatch(FL_SYS_CLOSE, (uintptr_t)h, 0, 0, 0) == 0);
    fl_sys_shutdown();
}

static void test_syscall_rejects_wrong_handle_type(void) {
    fl_sys_bootstrap();
    long pipe_h = fl_syscall_dispatch(FL_SYS_PIPE_CREATE, 64, 0, 0, 0);
    assert(pipe_h >= 0);
    char buf[8] = {0};
    assert(fl_syscall_dispatch(FL_SYS_MSGQ_SEND, (uintptr_t)pipe_h, (uintptr_t)buf, 1, 0) == -1);

    long msgq_h = fl_syscall_dispatch(FL_SYS_MSGQ_CREATE, 2, 16, 0, 0);
    assert(msgq_h >= 0);
    assert(fl_syscall_dispatch(FL_SYS_PIPE_READ, (uintptr_t)msgq_h, (uintptr_t)buf, sizeof(buf), 0) == -1);

    assert(fl_syscall_dispatch(FL_SYS_CLOSE, (uintptr_t)msgq_h, 0, 0, 0) == 0);
    assert(fl_syscall_dispatch(FL_SYS_CLOSE, (uintptr_t)pipe_h, 0, 0, 0) == 0);
    fl_sys_shutdown();
}

static void test_msgq_empty_receive_times_out(void) {
    msgq_t *q = msgq_create(1, 8);
    char out[8] = {0};
    struct timespec start;
    struct timespec end;
    const uint64_t timeout_ms = 5;

    assert(q);
    clock_gettime(CLOCK_MONOTONIC, &start);
    assert(msgq_receive(q, out, sizeof(out), timeout_ms) == -1);
    clock_gettime(CLOCK_MONOTONIC, &end);
    {
        long long sec = (long long)(end.tv_sec - start.tv_sec);
        long long nsec = (long long)(end.tv_nsec - start.tv_nsec);
        if (nsec < 0) {
            sec--;
            nsec += 1000000000LL;
        }
        uint64_t elapsed_ns = (uint64_t)(sec * 1000000000LL + nsec);
        assert(elapsed_ns >= timeout_ms * UINT64_C(1000000));
        if (elapsed_ns > UINT64_C(2000) * UINT64_C(1000000)) {
            fprintf(stderr, "Warning: msgq_receive timeout took %llu ms (expected ~%llu ms, tolerance 2000ms)\n",
                    (unsigned long long)(elapsed_ns / 1000000),
                    (unsigned long long)timeout_ms);
        }
    }
    msgq_destroy(q);
}

int main(void) {
    test_pipe_syscalls();
    test_msgq_syscalls();
    test_syscall_rejects_wrong_handle_type();
    test_msgq_empty_receive_times_out();
    puts("userspace connection tests: OK");
    return 0;
}
