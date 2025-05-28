#ifndef THREADPOOL_H
#define THREADPOOL_H

#include "common.h"   // Added to provide NUM_WORKERS and MAX_JOBS
#include <pthread.h>

typedef struct job_node {
    char *command_str;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    int done;
} job_node;

typedef struct thread_pool {
    pthread_t workers[NUM_WORKERS];  /* NUM_WORKERS defined in common.h */
    job_node *job_queue[MAX_JOBS];   /* MAX_JOBS defined in common.h */
    int head, tail;
    int shutting_down;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
} thread_pool_t;

extern thread_pool_t g_pool;

job_node *create_job(const char *line);
void free_job(job_node *job);
void queue_job(job_node *job);
void submit_single_command(const char *line);
void *worker_thread(void *arg);

#endif /* THREADPOOL_H */
