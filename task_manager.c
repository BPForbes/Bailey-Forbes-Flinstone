#include "task_manager.h"
#include "interpreter.h"
#include <stdlib.h>
#include <string.h>

struct task_manager {
    priority_queue_t pq;
    /* Uses existing thread pool - jobs are routed by priority */
};

static void run_command_task(void *arg) {
    char *cmd = (char *)arg;
    execute_command_str(cmd);
    free(cmd);
}

task_manager_t *task_manager_create(void) {
    task_manager_t *tm = calloc(1, sizeof(*tm));
    if (tm) pq_init(&tm->pq);
    return tm;
}

void task_manager_destroy(task_manager_t *tm) {
    free(tm);
}

int task_manager_submit(task_manager_t *tm, int priority, const char *command) {
    if (priority < 0) priority = 0;
    if (priority >= PQ_NUM_PRIORITIES) priority = PQ_NUM_PRIORITIES - 1;
    char *dup = strdup(command);
    if (!dup) return -1;
    int r = pq_push(&tm->pq, priority, run_command_task, dup);
    if (r != 0) {
        free(dup);
        return -1;
    }
    /* For now, immediately execute via thread pool (submit_single_command) */
    submit_single_command(command);
    return 0;
}

void task_manager_shutdown(task_manager_t *tm) {
    (void)tm;
}
