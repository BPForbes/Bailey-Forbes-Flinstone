#include "priority_queue.h"
#include <string.h>

void pq_init(priority_queue_t *pq) {
    memset(pq, 0, sizeof(*pq));
}

int pq_push(priority_queue_t *pq, int priority, task_fn fn, void *arg) {
    if (priority < 0 || priority >= PQ_NUM_PRIORITIES)
        return -1;
    int count = pq_count(pq);
    if (count >= PQ_MAX_ITEMS)
        return -1;
    int i = 0;
    while (i < PQ_MAX_ITEMS && pq->items[i].fn != NULL)
        i++;
    if (i >= PQ_MAX_ITEMS)
        return -1;
    pq->items[i].fn = fn;
    pq->items[i].arg = arg;
    pq->items[i].priority = priority;
    return 0;
}

/* Pop highest-priority (lowest number) available task */
int pq_pop(priority_queue_t *pq, pq_task_t *out) {
    int best = -1;
    for (int i = 0; i < PQ_MAX_ITEMS; i++) {
        if (pq->items[i].fn == NULL)
            continue;
        if (best < 0 || pq->items[i].priority < pq->items[best].priority)
            best = i;
    }
    if (best < 0)
        return -1;
    *out = pq->items[best];
    pq->items[best].fn = NULL;
    return 0;
}

int pq_is_empty(const priority_queue_t *pq) {
    for (int i = 0; i < PQ_MAX_ITEMS; i++) {
        if (pq->items[i].fn != NULL)
            return 0;
    }
    return 1;
}

int pq_count(const priority_queue_t *pq) {
    int n = 0;
    for (int i = 0; i < PQ_MAX_ITEMS; i++) {
        if (pq->items[i].fn != NULL)
            n++;
    }
    return n;
}
