#include "threadpool.h"
#include "interpreter.h"
#include "common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

thread_pool_t g_pool;

job_node *create_job(const char *line) {
    job_node *job = calloc(1, sizeof(job_node));
    job->command_str = strdup(line);
    pthread_mutex_init(&job->mutex, NULL);
    pthread_cond_init(&job->cond, NULL);
    job->done = 0;
    return job;
}

void free_job(job_node *job) {
    free(job->command_str);
    pthread_mutex_destroy(&job->mutex);
    pthread_cond_destroy(&job->cond);
    free(job);
}

void queue_job(job_node *job) {
    pthread_mutex_lock(&g_pool.mutex);
    int next_tail = (g_pool.tail + 1) % MAX_JOBS;
    if (next_tail == g_pool.head)
        fprintf(stderr, "Job queue overflow!\n");
    else {
        g_pool.job_queue[g_pool.tail] = job;
        g_pool.tail = next_tail;
        pthread_cond_signal(&g_pool.cond);
    }
    pthread_mutex_unlock(&g_pool.mutex);
}

void submit_single_command(const char *line) {
#ifdef BATCH_SINGLE_THREAD
    /* Direct execution: avoids allocator/pthread issues with ASM allocator */
    (void)execute_command_str(line);
#else
    job_node *job = create_job(line);
    queue_job(job);
    pthread_mutex_lock(&job->mutex);
    while (!job->done)
        pthread_cond_wait(&job->cond, &job->mutex);
    pthread_mutex_unlock(&job->mutex);
    free_job(job);
#endif
}

void *worker_thread(void *arg) {
    (void)arg;
    while (1) {
        pthread_mutex_lock(&g_pool.mutex);
        while (g_pool.head == g_pool.tail && !g_pool.shutting_down)
            pthread_cond_wait(&g_pool.cond, &g_pool.mutex);
        if (g_pool.shutting_down) {
            pthread_mutex_unlock(&g_pool.mutex);
            break;
        }
        job_node *job = g_pool.job_queue[g_pool.head];
        g_pool.head = (g_pool.head + 1) % MAX_JOBS;
        pthread_mutex_unlock(&g_pool.mutex);
        if (job) {
            int rc = execute_command_str(job->command_str);
            (void)rc;
            pthread_mutex_lock(&job->mutex);
            job->done = 1;
            pthread_cond_broadcast(&job->cond);
            pthread_mutex_unlock(&job->mutex);
        }
    }
    return NULL;
}
