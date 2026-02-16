#include "threadpool.h"
#include "interpreter.h"
#include "common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

thread_pool_t g_pool;

static void run_job_task(void *arg) {
    job_node *job = (job_node *)arg;
    (void)execute_command_str(job->command_str);
    pthread_mutex_lock(&job->mutex);
    job->done = 1;
    pthread_cond_broadcast(&job->cond);
    pthread_mutex_unlock(&job->mutex);
}

job_node *create_job(const char *line) {
    job_node *job = calloc(1, sizeof(*job));
    if (!job) return NULL;
    job->command_str = strdup(line);
    pthread_mutex_init(&job->mutex, NULL);
    pthread_cond_init(&job->cond, NULL);
    job->done = 0;
    job->priority = PRIORITY_IMMEDIATE;
    job->enqueue_time = time(NULL);
    return job;
}

void free_job(job_node *job) {
    if (!job) return;
    free(job->command_str);
    pthread_mutex_destroy(&job->mutex);
    pthread_cond_destroy(&job->cond);
    free(job);
}

void queue_job(job_node *job) {
    queue_job_priority(job, PRIORITY_IMMEDIATE);
}

void queue_job_priority(job_node *job, int priority) {
    job->priority = priority;
    job->enqueue_time = time(NULL);
    pthread_mutex_lock(&g_pool.mutex);
    if (pq_count(&g_pool.pq) >= PQ_MAX_ITEMS) {
        fprintf(stderr, "Job queue overflow!\n");
        pthread_mutex_unlock(&g_pool.mutex);
        return;
    }
    pq_push(&g_pool.pq, priority, run_job_task, job);
    pthread_cond_signal(&g_pool.cond);
    pthread_mutex_unlock(&g_pool.mutex);
}

void submit_single_command(const char *line) {
    submit_single_command_priority(line, PRIORITY_IMMEDIATE);
}

void submit_single_command_priority(const char *line, int priority) {
#ifdef BATCH_SINGLE_THREAD
    (void)priority;
    (void)execute_command_str(line);
#else
    job_node *job = create_job(line);
    if (!job) return;
    queue_job_priority(job, priority);
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
        while (pq_is_empty(&g_pool.pq) && !g_pool.shutting_down)
            pthread_cond_wait(&g_pool.cond, &g_pool.mutex);
        if (g_pool.shutting_down) {
            pthread_mutex_unlock(&g_pool.mutex);
            break;
        }
        pq_task_t task;
        int r = pq_pop(&g_pool.pq, &task);
        pthread_mutex_unlock(&g_pool.mutex);
        if (r == 0 && task.fn && task.arg)
            task.fn(task.arg);
    }
    return NULL;
}
