#ifndef PRIORITY_QUEUE_H
#define PRIORITY_QUEUE_H

#include <stddef.h>

#define PQ_NUM_PRIORITIES 4   /* 0=highest, 3=lowest */
#define PQ_MAX_ITEMS       128

/**
 * MLQ-style priority queue: binary heap with O(log N) push/pop/update.
 *
 * INVARIANTS:
 * - FIFO tie-break: equal priority => lower seq (pushed first) pops first
 * - pq_update(handle, new_priority): O(log N) change-priority for aging/boost
 *
 * Handles: pq_push returns a handle (>=0) or -1 on error.
 * Use pq_update(pq, handle, new_priority) for aging, I/O boost, etc.
 */
typedef void (*task_fn)(void *arg);

typedef int pq_handle_t;  /* -1 = invalid; from pq_push for pq_update */

typedef struct pq_task {
    task_fn fn;
    void *arg;
    int priority;            /* 0..PQ_NUM_PRIORITIES-1 */
    unsigned int seq;        /* FIFO tie-break: lower = pushed first */
} pq_task_t;

typedef struct priority_queue {
    pq_task_t slots[PQ_MAX_ITEMS];
    int       heap[PQ_MAX_ITEMS];      /* heap of slot indices */
    int       slot_to_heap[PQ_MAX_ITEMS]; /* heap pos per slot, -1 if free */
    int       size;
    unsigned int next_seq;
} priority_queue_t;

void pq_init(priority_queue_t *pq);

/** Push task; returns handle (>=0) or -1. Use handle for pq_update. */
pq_handle_t pq_push(priority_queue_t *pq, int priority, task_fn fn, void *arg);

/** Pop highest-priority (lowest number) task; FIFO tie-break for equal priority. */
int pq_pop(priority_queue_t *pq, pq_task_t *out);

/** O(log N) update priority of item (for aging, I/O boost, etc.). */
int pq_update(priority_queue_t *pq, pq_handle_t h, int new_priority);

int pq_is_empty(const priority_queue_t *pq);
int pq_count(const priority_queue_t *pq);

#endif /* PRIORITY_QUEUE_H */
