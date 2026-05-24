/**
 * Issue #222: threadpool job creation and completion (hosted build, no worker race).
 */
#include "threadpool.h"
#include "common.h"
#include <stdio.h>
#include <string.h>

#define ASSERT(c) do { if (!(c)) { fprintf(stderr, "FAIL: %s\n", #c); return 1; } } while(0)

static int test_create_job_strdup_guard(void) {
    job_node *job = create_job("echo ok");
    ASSERT(job != NULL);
    ASSERT(job->command_str != NULL);
    ASSERT(strcmp(job->command_str, "echo ok") == 0);
    free_job(job);
    return 0;
}

int main(void) {
    printf("test_create_job_strdup_guard... ");
    if (test_create_job_strdup_guard() != 0) return 1;
    printf("OK\n");

    printf("All threadpool issue #222 tests passed.\n");
    return 0;
}
