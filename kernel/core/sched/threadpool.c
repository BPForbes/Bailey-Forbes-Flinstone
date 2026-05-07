#include "threadpool.h"
#include "interpreter.h"
#include "common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

thread_pool_t g_pool;

#ifdef BATCH_SINGLE_THREAD

/**
 * Execute the job's command string and mark the job as completed.
 *
 * Calls `execute_command_str` with the job's `command_str` and sets
 * the job's `done` flag to 1 to indicate completion.
 *
 * @param arg Pointer to a `job_node` whose `command_str` will be executed; must be non-NULL.
 */
static void run_job_task(void *arg) {
    job_node *job = (job_node *)arg;
    (void)execute_command_str(job->command_str);
    job->done = 1;
}

/**
 * Execute at most one queued task from each priority layer.
 *
 * Attempts to pop a single task from each priority layer (0 .. PQ_NUM_PRIORITIES - 1)
 * and invokes the popped task's function with its argument when both the function
 * pointer and argument are non-NULL.
 */
static void drain_pool_once(void) {
    pq_task_t task;
    for (int layer = 0; layer < PQ_NUM_PRIORITIES; layer++) {
        if (pq_pop_from_layer(&g_pool.pq, layer, &task) != 0)
            continue;
        if (task.fn && task.arg)
            task.fn(task.arg);
    }
}

/**
 * Create a new job for executing the given command string.
 *
 * Allocates and initializes a job_node, duplicates `line` into the job's
 * internal command string, and sets default metadata (notably priority and
 * enqueue time). The returned job must be freed with the corresponding
 * free_job function.
 *
 * @param line Command string to execute; the function makes an internal copy,
 *             so the caller retains ownership of the original buffer.
 * @returns Pointer to an initialized `job_node`, or `NULL` if allocation fails.
 */
job_node *create_job(const char *line) {
    job_node *job = calloc(1, sizeof(*job));
    if (!job) return NULL;
    job->command_str = strdup(line);
    job->done = 0;
    job->priority = PRIORITY_IMMEDIATE;
    job->enqueue_time = time(NULL);
    job->pq_handle = -1;
    return job;
}

/**
 * Free a job and its associated resources.
 *
 * Frees the job's duplicated command string and the job structure itself.
 * Safe to call with a NULL pointer; no action will be taken in that case.
 *
 * @param job Job to free, or NULL.
 */
void free_job(job_node *job) {
    if (!job) return;
    free(job->command_str);
    free(job);
}

/**
 * Enqueue the given job using the immediate priority level.
 *
 * @param job Job node to enqueue (created by create_job); ownership remains with the caller
 *            until the job completes and is freed by the caller or queue processing.
 */
void queue_job(job_node *job) {
    queue_job_priority(job, PRIORITY_IMMEDIATE);
}

/**
 * Enqueue a job into the global priority queue with the specified priority.
 *
 * Updates the job's metadata (priority, enqueue time, pq handle) and attempts
 * to push it into the global queue. If the queue is full, an error is printed
 * and the function returns without enqueuing. Jobs submitted at
 * `PRIORITY_BACKGROUND` will have their quantum set to 100. In single-threaded
 * builds, queued work is executed immediately until the queue is empty or the
 * pool is shutting down.
 *
 * @param job Job node to enqueue; its metadata will be updated and it may be
 *            executed by worker logic.
 * @param priority Priority level to assign to the job; influences queue order
 *                 and background quantum behavior.
 */
void queue_job_priority(job_node *job, int priority) {
    job->priority = priority;
    job->enqueue_time = time(NULL);
    job->pq_handle = -1;
    if (pq_count(&g_pool.pq) >= PQ_MAX_ITEMS) {
        fprintf(stderr, "Job queue overflow!\n");
        return;
    }
    pq_handle_t h = pq_push(&g_pool.pq, priority, run_job_task, job);
    if (h >= 0) {
        job->pq_handle = h;
        if (priority == PRIORITY_BACKGROUND)
            pq_set_quantum(&g_pool.pq, h, 100);
    }
    /* Single-threaded: run queued work immediately. */
    while (!pq_is_empty(&g_pool.pq) && !g_pool.shutting_down)
        drain_pool_once();
}

/**
 * Submit a single command line for immediate execution and wait for it to complete.
 *
 * This function schedules the provided command string with immediate execution semantics
 * and blocks until the command has finished running.
 *
 * @param line Null-terminated command string to execute. Ownership is not transferred
 *             (the caller retains responsibility for the memory).
 */
void submit_single_command(const char *line) {
    submit_single_command_priority(line, PRIORITY_IMMEDIATE);
}

/**
 * Execute a single command string immediately without queuing.
 *
 * This function runs the command described by `line` synchronously and does not
 * enqueue it into the global job pool or spawn worker threads.
 *
 * @param line Null-terminated command string to execute.
 * @param priority This parameter is ignored; the command is executed immediately regardless of priority.
 */
void submit_single_command_priority(const char *line, int priority) {
    (void)priority;
    (void)execute_command_str(line);
}

/**
 * Worker thread entry used in single-threaded builds; it performs no work and returns immediately.
 *
 * @returns NULL
 */
void *worker_thread(void *arg) {
    (void)arg;
    return NULL;
}

#else /**
 * Execute a queued job and notify waiters of its completion.
 *
 * Executes the command string stored in the provided job node, then sets the
 * job's completion flag and broadcasts the job's condition variable while
 * holding the job mutex so any threads waiting for this job are awakened.
 *
 * @param arg Pointer to the job_node representing the job to run.
 */

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

/**
 * Enqueue a job into the global priority queue with the specified priority.
 *
 * Updates the job's metadata (priority, enqueue time, pq handle), attempts to
 * push the job into the global queue, applies a reduced quantum for background
 * priority entries, and notifies worker threads. If the queue is full the job
 * is not enqueued and an overflow message is printed.
 *
 * @param job Pointer to the job to enqueue (must be non-NULL).
 * @param priority Priority level to assign to the job; higher-priority jobs are
 *                 considered before lower-priority ones.
 */
void queue_job_priority(job_node *job, int priority) {
    job->priority = priority;
    job->enqueue_time = time(NULL);
    job->pq_handle = -1;
    pthread_mutex_lock(&g_pool.mutex);
    if (pq_count(&g_pool.pq) >= PQ_MAX_ITEMS) {
        fprintf(stderr, "Job queue overflow!\n");
        pthread_mutex_unlock(&g_pool.mutex);
        return;
    }
    pq_handle_t h = pq_push(&g_pool.pq, priority, run_job_task, job);
    if (h >= 0) {
        job->pq_handle = h;
        if (priority == PRIORITY_BACKGROUND)
            pq_set_quantum(&g_pool.pq, h, 100);
    }
    pthread_cond_signal(&g_pool.cond);
    pthread_mutex_unlock(&g_pool.mutex);
}

/**
 * Submit a single command line for immediate execution and wait for it to complete.
 *
 * This function schedules the provided command string with immediate execution semantics
 * and blocks until the command has finished running.
 *
 * @param line Null-terminated command string to execute. Ownership is not transferred
 *             (the caller retains responsibility for the memory).
 */
void submit_single_command(const char *line) {
    submit_single_command_priority(line, PRIORITY_IMMEDIATE);
}

/**
 * Submit a single command for execution with the specified priority and wait for it to complete.
 *
 * Creates a job for the given command string, enqueues it at the provided priority, blocks until
 * the job has finished executing, and then frees the job resources.
 *
 * @param line Null-terminated command string to execute.
 * @param priority Priority level determining queue placement for the job.
 */
void submit_single_command_priority(const char *line, int priority) {
    job_node *job = create_job(line);
    if (!job) return;
    queue_job_priority(job, priority);
    pthread_mutex_lock(&job->mutex);
    while (!job->done)
        pthread_cond_wait(&job->cond, &job->mutex);
    pthread_mutex_unlock(&job->mutex);
    free_job(job);
}

/**
 * Worker thread main loop that consumes tasks from the global priority queue and executes them until shutdown.
 *
 * The thread waits on the global pool condition when the queue is empty, pops available tasks by priority,
 * executes each task's function with its argument (if present), and exits when the global shutdown flag is set.
 *
 * @returns NULL when the thread exits.
 */
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

#endif /* BATCH_SINGLE_THREAD */
