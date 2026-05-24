#include "threadpool.h"
#include "interpreter.h"
#include "common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

thread_pool_t g_pool;

static void run_job_task(void *arg) {
    job_node *job = (job_node *)arg;

    if (job->command_str)
        (void)execute_command_str(job->command_str);
    if (job->done_sem) {
        sem_post(job->done_sem);
        return;
    }
    pthread_mutex_lock(&job->mutex);
    job->done = 1;
    pthread_cond_broadcast(&job->cond);
    pthread_mutex_unlock(&job->mutex);
}

job_node *create_job(const char *line) {
    job_node *job = calloc(1, sizeof(*job));

    if (!job)
        return NULL;
    job->command_str = strdup(line);
    if (!job->command_str) {
        free(job);
        return NULL;
    }
    pthread_mutex_init(&job->mutex, NULL);
    pthread_cond_init(&job->cond, NULL);
    job->done = 0;
    job->done_sem = NULL;
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

int queue_job_priority(job_node *job, int priority) {
    job->priority = priority;
    job->enqueue_time = time(NULL);
    job->pq_handle = -1;
    pthread_mutex_lock(&g_pool.mutex);
    if (pq_count(&g_pool.pq) >= PQ_MAX_ITEMS) {
        fprintf(stderr, "Job queue overflow!\n");
        pthread_mutex_unlock(&g_pool.mutex);
        return -1;
    }
    pq_handle_t h = pq_push(&g_pool.pq, priority, run_job_task, job);
    if (h >= 0) {
        job->pq_handle = h;
        if (priority == PRIORITY_BACKGROUND)
            pq_set_quantum(&g_pool.pq, h, 100);  /* optional demotion on quantum expiry */
    }
    pthread_cond_signal(&g_pool.cond);
    pthread_mutex_unlock(&g_pool.mutex);
    return (h >= 0) ? 0 : -1;
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
    sem_t done_sem;

    if (!job)
        return;
    if (sem_init(&done_sem, 0, 0) != 0) {
        free_job(job);
        return;
    }
    job->done_sem = &done_sem;
    if (queue_job_priority(job, priority) != 0) {
        job->done_sem = NULL;
        sem_destroy(&done_sem);
        free_job(job);
        return;
    }
    if (sem_wait(&done_sem) != 0)
        perror("threadpool: sem_wait");
    job->done_sem = NULL;
    sem_destroy(&done_sem);
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
        /* Layer-scan: run one from each non-empty layer per round (secondary tie-breaker) */
        pq_task_t task;
        for (int layer = 0; layer < PQ_NUM_PRIORITIES; layer++) {
            if (pq_pop_from_layer(&g_pool.pq, layer, &task) != 0)
                continue;
            pthread_mutex_unlock(&g_pool.mutex);
            if (task.fn && task.arg)
                task.fn(task.arg);
            pthread_mutex_lock(&g_pool.mutex);
        }
        pthread_mutex_unlock(&g_pool.mutex);
    }
    return NULL;
}
