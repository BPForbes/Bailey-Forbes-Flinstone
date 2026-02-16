#ifndef TASK_MANAGER_H
#define TASK_MANAGER_H

#include "priority_queue.h"
#include "threadpool.h"

/* Task manager: multi-priority queue + thread pool */
typedef struct task_manager task_manager_t;

task_manager_t *task_manager_create(void);
void task_manager_destroy(task_manager_t *tm);
int task_manager_submit(task_manager_t *tm, int priority, const char *command);
void task_manager_shutdown(task_manager_t *tm);

#endif /* TASK_MANAGER_H */
