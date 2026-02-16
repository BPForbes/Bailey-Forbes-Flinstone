#ifndef PRIORITY_QUEUE_H
#define PRIORITY_QUEUE_H

#include <stddef.h>

#define PQ_NUM_PRIORITIES 4   /* 0=highest, 3=lowest */
#define PQ_MAX_ITEMS       128

/**
 * MLQ: per-layer FIFO queues, layer-scan scheduling, secondary tie-breaker.
 *
 * Scheduling: scan layers 0..3 in order; within each layer, FIFO (seq) tie-break.
 * - pq_pop: scans layers, returns first task from lowest non-empty layer
 * - pq_pop_from_layer: returns first (FIFO) from a specific layer - for drain-by-layer
 * - pq_has_layer: check if layer has work
 *
 * pq_update(handle, new_priority): O(1) move task between layers for aging/boost.
 */
typedef void (*task_fn)(void *arg);

typedef int pq_handle_t;  /* -1 = invalid; from pq_push for pq_update */

typedef struct pq_task {
    task_fn fn;
    void *arg;
    int priority;            /* 0..PQ_NUM_PRIORITIES-1 */
    unsigned int seq;        /* FIFO tie-break within layer */
} pq_task_t;

typedef struct priority_queue {
    pq_task_t slots[PQ_MAX_ITEMS];
    int       layer_head[PQ_NUM_PRIORITIES];  /* -1 if empty */
    int       layer_tail[PQ_NUM_PRIORITIES];
    int       slot_next[PQ_MAX_ITEMS];        /* next in same layer, -1 if last */
    int       slot_prev[PQ_MAX_ITEMS];        /* prev in same layer, -1 if first */
    int       size;
    unsigned int next_seq;
} priority_queue_t;

void pq_init(priority_queue_t *pq);

/** Push task; returns handle (>=0) or -1. Use handle for pq_update. */
pq_handle_t pq_push(priority_queue_t *pq, int priority, task_fn fn, void *arg);

/**
 * Pop highest-priority task. Scans layers 0..3, returns first from lowest
 * non-empty layer. FIFO tie-break within layer.
 */
int pq_pop(priority_queue_t *pq, pq_task_t *out);

/**
 * Pop first (FIFO) task from a specific layer. For layer-by-layer scheduling:
 *   for (layer = 0; layer < PQ_NUM_PRIORITIES; layer++)
 *     while (pq_pop_from_layer(pq, layer, &t) == 0) run(t);
 */
int pq_pop_from_layer(priority_queue_t *pq, int layer, pq_task_t *out);

/** True if layer has at least one task. */
int pq_has_layer(const priority_queue_t *pq, int layer);

/** O(1) update priority; moves task between layers for aging/boost. */
int pq_update(priority_queue_t *pq, pq_handle_t h, int new_priority);

int pq_is_empty(const priority_queue_t *pq);
int pq_count(const priority_queue_t *pq);
int pq_count_layer(const priority_queue_t *pq, int layer);

#endif /* PRIORITY_QUEUE_H */
