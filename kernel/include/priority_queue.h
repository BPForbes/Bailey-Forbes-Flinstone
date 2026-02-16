#ifndef PRIORITY_QUEUE_H
#define PRIORITY_QUEUE_H

#include <stddef.h>
#include <stdint.h>

#define PQ_NUM_PRIORITIES 4   /* 0=highest, 3=lowest */
#define PQ_MAX_ITEMS       128

/**
 * Scheduler-grade MLQ: O(1) push/pop/update, safe handles, remove-by-handle.
 *
 * - Freelist: O(1) slot allocation
 * - nonempty_mask: O(1) layer selection via ctz
 * - Generation IDs: handles survive slot reuse; stale handle = safe no-op
 * - pq_remove: cancel/block/exit
 * - Scheduler policy: quantum + runtime in pq_task_t for demotion/aging
 */
typedef void (*task_fn)(void *arg);

typedef int pq_handle_t;  /* -1 = invalid; (gen<<16)|slot from pq_push */

typedef struct pq_task {
    task_fn fn;
    void *arg;
    int priority;            /* 0..PQ_NUM_PRIORITIES-1 */
    unsigned int seq;        /* FIFO tie-break within layer */
    uint32_t quantum;        /* 0 = no limit; demote when runtime >= quantum */
    uint32_t runtime;       /* scheduler-maintained; increment on tick */
} pq_task_t;

typedef struct priority_queue {
    pq_task_t slots[PQ_MAX_ITEMS];
    int       layer_head[PQ_NUM_PRIORITIES];  /* -1 if empty */
    int       layer_tail[PQ_NUM_PRIORITIES];
    int       slot_next[PQ_MAX_ITEMS];        /* next in same layer, -1 if last */
    int       slot_prev[PQ_MAX_ITEMS];        /* prev in same layer, -1 if first */
    int       free_stack[PQ_MAX_ITEMS];       /* O(1) slot allocation */
    int       free_top;
    uint64_t  nonempty_mask;                 /* bit N = layer N has work */
    uint16_t  gen[PQ_MAX_ITEMS];             /* generation per slot; handle = (gen<<16)|slot */
    uint16_t  next_gen;
    int       size;
    unsigned int next_seq;
} priority_queue_t;

void pq_init(priority_queue_t *pq);

/** Push task; returns handle (gen<<16)|slot or -1. O(1). */
pq_handle_t pq_push(priority_queue_t *pq, int priority, task_fn fn, void *arg);

/**
 * Pop highest-priority task. O(1) via ctz(nonempty_mask). FIFO within layer.
 */
int pq_pop(priority_queue_t *pq, pq_task_t *out);

/**
 * Pop first (FIFO) task from a specific layer.
 */
int pq_pop_from_layer(priority_queue_t *pq, int layer, pq_task_t *out);

/** True if layer has at least one task. */
int pq_has_layer(const priority_queue_t *pq, int layer);

/** O(1) update priority; moves task between layers. Validates handle gen. */
int pq_update(priority_queue_t *pq, pq_handle_t h, int new_priority);

/** O(1) remove task by handle; for cancel/block/exit. Returns 0 on success. */
int pq_remove(priority_queue_t *pq, pq_handle_t h);

/** Set quantum for demotion policy: demote when runtime >= quantum. */
void pq_set_quantum(priority_queue_t *pq, pq_handle_t h, uint32_t quantum);

int pq_is_empty(const priority_queue_t *pq);
int pq_count(const priority_queue_t *pq);
int pq_count_layer(const priority_queue_t *pq, int layer);

#endif /* PRIORITY_QUEUE_H */
