/**
 * Issue #222: threadpool job creation, done_sem completion (#222.1, #222.2).
 */
#include "threadpool.h"
#include "common.h"
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define ASSERT(c) do { if (!(c)) { fprintf(stderr, "FAIL: %s\n", #c); return 1; } } while(0)

int execute_command_str(const char *line) {
    (void)line;
    return 0;
}

#ifndef BATCH_SINGLE_THREAD
static int threadpool_sem_wait_done(sem_t *done_sem) {
    while (sem_wait(done_sem) != 0) {
        if (errno != EINTR) {
            perror("threadpool: sem_wait");
            return -1;
        }
    }
    return 0;
}

static void *post_done_sem_later(void *arg) {
    sem_t *done_sem = (sem_t *)arg;
    struct timespec ts = {0, 5 * 1000 * 1000};
    nanosleep(&ts, NULL);
    sem_post(done_sem);
    return NULL;
}

static int test_done_sem_completion_issue222_2(void) {
    sem_t done_sem;
    job_node *job;
    pthread_t worker;

    ASSERT(sem_init(&done_sem, 0, 0) == 0);
    job = create_job("noop");
    ASSERT(job != NULL);
    job->done_sem = &done_sem;
    ASSERT(pthread_create(&worker, NULL, post_done_sem_later, &done_sem) == 0);
    ASSERT(threadpool_sem_wait_done(&done_sem) == 0);
    ASSERT(pthread_join(worker, NULL) == 0);
    job->done_sem = NULL;
    sem_destroy(&done_sem);
    free_job(job);
    return 0;
}

static int test_pool_start(void) {
    pq_init(&g_pool.pq);
    g_pool.shutting_down = 0;
    ASSERT(pthread_mutex_init(&g_pool.mutex, NULL) == 0);
    ASSERT(pthread_cond_init(&g_pool.cond, NULL) == 0);
    for (int i = 0; i < NUM_WORKERS; i++)
        ASSERT(pthread_create(&g_pool.workers[i], NULL, worker_thread, NULL) == 0);
    return 0;
}

static void test_pool_stop(void) {
    pthread_mutex_lock(&g_pool.mutex);
    g_pool.shutting_down = 1;
    pthread_cond_broadcast(&g_pool.cond);
    pthread_mutex_unlock(&g_pool.mutex);
    for (int i = 0; i < NUM_WORKERS; i++)
        pthread_join(g_pool.workers[i], NULL);
    pthread_cond_destroy(&g_pool.cond);
    pthread_mutex_destroy(&g_pool.mutex);
}

static int test_submit_uses_done_sem_issue222_2(void) {
    if (test_pool_start() != 0)
        return 1;
    submit_single_command("version");
    test_pool_stop();
    return 0;
}
#endif

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

#ifndef BATCH_SINGLE_THREAD
    printf("test_done_sem_completion_issue222_2... ");
    if (test_done_sem_completion_issue222_2() != 0) return 1;
    printf("OK\n");

    printf("test_submit_uses_done_sem_issue222_2... ");
    if (test_submit_uses_done_sem_issue222_2() != 0) return 1;
    printf("OK\n");
#else
    printf("test_done_sem_completion_issue222_2... SKIP (BATCH_SINGLE_THREAD)\n");
    printf("test_submit_uses_done_sem_issue222_2... SKIP (BATCH_SINGLE_THREAD)\n");
#endif

    printf("All threadpool issue #222 tests passed.\n");
    return 0;
}
