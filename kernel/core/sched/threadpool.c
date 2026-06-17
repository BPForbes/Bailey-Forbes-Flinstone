#define _POSIX_C_SOURCE 200809L
#include "threadpool.h"
#include "interpreter.h"
#include "common.h"
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static volatile sig_atomic_t s_pool_interrupt_req;
static pthread_t            s_pool_active_worker;
static int                  s_pool_active_worker_set;

#ifndef BATCH_SINGLE_THREAD
static int threadpool_sem_wait_done(sem_t *done_sem) {
    while (sem_wait(done_sem) != 0) {
        if (errno != EINTR) {
            perror("threadpool: sem_wait");
            return -1;
        }
        if (s_pool_interrupt_req)
            return -1;
    }
    return 0;
}
#endif

thread_pool_t g_pool;

static void job_task_cleanup(void *arg) {
    job_node *job = (job_node *)arg;

    pthread_mutex_lock(&g_pool.mutex);
    s_pool_active_worker_set = 0;
    s_pool_active_worker     = (pthread_t)0;
    pthread_mutex_unlock(&g_pool.mutex);
    if (job && job->done_sem)
        sem_post(job->done_sem);
}

static void run_job_task(void *arg) {
    job_node *job = (job_node *)arg;

    pthread_cleanup_push(job_task_cleanup, job);
    /* Deferred cancellation (the default): cancel is delivered only at safe
     * cancellation points inside execute_command_str, never while holding
     * g_pool.mutex or job->mutex. */
    pthread_mutex_lock(&g_pool.mutex);
    s_pool_active_worker     = pthread_self();
    s_pool_active_worker_set = 1;
    pthread_mutex_unlock(&g_pool.mutex);

    if (job->command_str)
        (void)execute_command_str(job->command_str);

    if (!job->done_sem) {
        pthread_mutex_lock(&job->mutex);
        job->done = 1;
        pthread_cond_broadcast(&job->cond);
        pthread_mutex_unlock(&job->mutex);
    }
    pthread_cleanup_pop(1);
}

static void pool_sigint_handler(int sig) {
    (void)sig;
    s_pool_interrupt_req = 1;
    (void)write(STDERR_FILENO, "^C\n", 3);
    /* pthread_cancel is not async-signal-safe; the main thread performs
     * worker cancellation after detecting s_pool_interrupt_req. */
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

void submit_single_command_interruptible(const char *line) {
#ifdef BATCH_SINGLE_THREAD
    submit_single_command(line);
#else
    struct sigaction sa_new, sa_old;
    job_node *job;
    sem_t done_sem;

    memset(&sa_new, 0, sizeof(sa_new));
    sa_new.sa_handler = pool_sigint_handler;
    sigemptyset(&sa_new.sa_mask);
    sa_new.sa_flags = 0;
    s_pool_interrupt_req = 0;
    if (sigaction(SIGINT, &sa_new, &sa_old) != 0) {
        submit_single_command(line);
        return;
    }

    job = create_job(line);
    if (!job) {
        (void)sigaction(SIGINT, &sa_old, NULL);
        return;
    }
    if (sem_init(&done_sem, 0, 0) != 0) {
        free_job(job);
        (void)sigaction(SIGINT, &sa_old, NULL);
        return;
    }
    job->done_sem = &done_sem;
    if (queue_job_priority(job, PRIORITY_IMMEDIATE) != 0) {
        job->done_sem = NULL;
        sem_destroy(&done_sem);
        free_job(job);
        (void)sigaction(SIGINT, &sa_old, NULL);
        return;
    }

    /* Wait for completion; break early on user interrupt (SIGINT). */
    while (sem_wait(&done_sem) != 0) {
        if (errno != EINTR)
            break;
        if (s_pool_interrupt_req) {
            /* Cancel the worker from main-thread context — pthread_cancel is
             * not async-signal-safe and must not be called from a handler. */
            pthread_t worker = (pthread_t)0;
            pthread_mutex_lock(&g_pool.mutex);
            if (s_pool_active_worker_set)
                worker = s_pool_active_worker;
            pthread_mutex_unlock(&g_pool.mutex);
            if (worker)
                (void)pthread_cancel(worker);
            /* Drain the sem_post from the cleanup handler. */
            while (sem_wait(&done_sem) != 0 && errno == EINTR)
                ;
            break;
        }
    }

    (void)sigaction(SIGINT, &sa_old, NULL);
    job->done_sem = NULL;
    sem_destroy(&done_sem);
    free_job(job);
#endif
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
    if (threadpool_sem_wait_done(&done_sem) != 0) {
        job->done_sem = NULL;
        sem_destroy(&done_sem);
        free_job(job);
        return;
    }
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
