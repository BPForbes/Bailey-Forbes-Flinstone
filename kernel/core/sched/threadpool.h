#ifndef THREADPOOL_H
#define THREADPOOL_H

#include "common.h"
#include "priority_queue.h"
#include <pthread.h>
#include <time.h>

#define PRIORITY_IMMEDIATE  0   /* user commands */
#define PRIORITY_DEFERRED   1   /* batch writes */
#define PRIORITY_BACKGROUND 2  /* maintenance */

typedef struct job_node {
    char *command_str;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    int done;
    int priority;           /* for PQ */
    time_t enqueue_time;   /* for aging (future) */
    pq_handle_t pq_handle; /* for pq_remove (cancel) / pq_update (aging) */
} job_node;

typedef struct thread_pool {
    pthread_t workers[NUM_WORKERS];
    priority_queue_t pq;    /* PQ replaces FIFO queue */
    int shutting_down;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
} thread_pool_t;

extern thread_pool_t g_pool;

job_node *create_job(const char *line);
void free_job(job_node *job);
void queue_job(job_node *job);
void queue_job_priority(job_node *job, int priority);
void submit_single_command(const char *line);
void submit_single_command_priority(const char *line, int priority);
void *worker_thread(void *arg);

#endif /* THREADPOOL_H */
