#include "task_manager.h"
#include "threadpool.h"
#include "interpreter.h"
#include <stdlib.h>
#include <string.h>

struct task_manager {
    /* Routes to thread pool's PQ; no local queue */
};

task_manager_t *task_manager_create(void) {
    return calloc(1, sizeof(task_manager_t));
}

void task_manager_destroy(task_manager_t *tm) {
    free(tm);
}

int task_manager_submit(task_manager_t *tm, int priority, const char *command) {
    (void)tm;
    if (priority < 0) priority = 0;
    if (priority >= PQ_NUM_PRIORITIES) priority = PQ_NUM_PRIORITIES - 1;
    /* Submit via thread pool's PQ - workers pull by priority */
    submit_single_command_priority(command, priority);
    return 0;
}

void task_manager_shutdown(task_manager_t *tm) {
    (void)tm;
}
