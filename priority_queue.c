/**
 * MLQ-style priority queue: binary heap, O(log N) push/pop/update.
 */
#include "priority_queue.h"
#include "mem_asm.h"
#include <string.h>

/* Compare two slots: lower priority wins; if equal, lower seq (FIFO) wins */
static int pq_less(const priority_queue_t *pq, int slot_a, int slot_b) {
    const pq_task_t *a = &pq->slots[slot_a];
    const pq_task_t *b = &pq->slots[slot_b];
    if (a->priority != b->priority)
        return a->priority < b->priority;
    return a->seq < b->seq;
}

static void pq_swap(priority_queue_t *pq, int i, int j) {
    int si = pq->heap[i];
    int sj = pq->heap[j];
    pq->heap[i] = sj;
    pq->heap[j] = si;
    pq->slot_to_heap[si] = j;
    pq->slot_to_heap[sj] = i;
}

static void pq_sift_up(priority_queue_t *pq, int idx) {
    while (idx > 0) {
        int parent = (idx - 1) / 2;
        if (!pq_less(pq, pq->heap[idx], pq->heap[parent]))
            break;
        pq_swap(pq, idx, parent);
        idx = parent;
    }
}

static void pq_sift_down(priority_queue_t *pq, int idx) {
    int n = pq->size;
    while (1) {
        int best = idx;
        int left = 2 * idx + 1;
        int right = 2 * idx + 2;
        if (left < n && pq_less(pq, pq->heap[left], pq->heap[best]))
            best = left;
        if (right < n && pq_less(pq, pq->heap[right], pq->heap[best]))
            best = right;
        if (best == idx)
            break;
        pq_swap(pq, idx, best);
        idx = best;
    }
}

void pq_init(priority_queue_t *pq) {
    if (pq)
        asm_mem_zero(pq, sizeof(*pq));
    if (pq) {
        for (int i = 0; i < PQ_MAX_ITEMS; i++)
            pq->slot_to_heap[i] = -1;
    }
}

pq_handle_t pq_push(priority_queue_t *pq, int priority, task_fn fn, void *arg) {
    if (!pq || priority < 0 || priority >= PQ_NUM_PRIORITIES || pq->size >= PQ_MAX_ITEMS)
        return -1;
    int slot = -1;
    for (int i = 0; i < PQ_MAX_ITEMS; i++) {
        if (pq->slot_to_heap[i] == -1 && pq->slots[i].fn == NULL) {
            slot = i;
            break;
        }
    }
    if (slot < 0)
        return -1;
    pq->slots[slot].fn = fn;
    pq->slots[slot].arg = arg;
    pq->slots[slot].priority = priority;
    pq->slots[slot].seq = pq->next_seq++;
    pq->heap[pq->size] = slot;
    pq->slot_to_heap[slot] = pq->size;
    pq->size++;
    pq_sift_up(pq, pq->size - 1);
    return (pq_handle_t)slot;
}

int pq_pop(priority_queue_t *pq, pq_task_t *out) {
    if (!pq || pq->size == 0 || !out)
        return -1;
    int slot = pq->heap[0];
    *out = pq->slots[slot];
    pq->slots[slot].fn = NULL;
    pq->slot_to_heap[slot] = -1;
    pq->size--;
    if (pq->size > 0) {
        pq->heap[0] = pq->heap[pq->size];
        pq->slot_to_heap[pq->heap[0]] = 0;
        pq_sift_down(pq, 0);
    }
    return 0;
}

int pq_update(priority_queue_t *pq, pq_handle_t h, int new_priority) {
    if (!pq || h < 0 || h >= PQ_MAX_ITEMS || new_priority < 0 || new_priority >= PQ_NUM_PRIORITIES)
        return -1;
    int idx = pq->slot_to_heap[h];
    if (idx < 0)
        return -1;
    int old_p = pq->slots[h].priority;
    pq->slots[h].priority = new_priority;
    if (new_priority < old_p)
        pq_sift_up(pq, idx);
    else if (new_priority > old_p)
        pq_sift_down(pq, idx);
    return 0;
}

int pq_is_empty(const priority_queue_t *pq) {
    return !pq || pq->size == 0;
}

int pq_count(const priority_queue_t *pq) {
    return pq ? pq->size : 0;
}
