#ifndef FL_SCHED_H
#define FL_SCHED_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Task priorities */
typedef enum {
    PRIORITY_IDLE = 0,
    PRIORITY_LOW = 1,
    PRIORITY_NORMAL = 2,
    PRIORITY_HIGH = 3,
    PRIORITY_REALTIME = 4
} task_priority_t;

/* Task structure */
struct task;
typedef struct task task_t;

/* Task creation */
task_t *task_create(void (*entry)(void *), void *arg, task_priority_t priority);
void task_destroy(task_t *task);

/* Task control */
void task_yield(void);
void task_sleep(uint64_t ms);
task_priority_t task_get_priority(task_t *task);
void task_set_priority(task_t *task, task_priority_t priority);

/* Scheduler */
void sched_init(void);
void sched_start(void);
void sched_tick(void);

/* Thread pool */
void threadpool_init(size_t num_threads);
void threadpool_submit(void (*func)(void *), void *arg, task_priority_t priority);
void threadpool_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* FL_SCHED_H */
