/**
 * MLQ: per-layer FIFO queues, layer-scan scheduling.
 * Secondary tie-breaker: scan layers 0..3; within layer, FIFO (seq).
 */
#include "fl/sched.h"
#include "fl/arch.h"
#include "priority_queue.h"
#include <string.h>

static void unlink_slot(priority_queue_t *pq, int slot) {
    int p = pq->slot_prev[slot];
    int n = pq->slot_next[slot];
    int layer = pq->slots[slot].priority;
    if (p >= 0)
        pq->slot_next[p] = n;
    else
        pq->layer_head[layer] = n;
    if (n >= 0)
        pq->slot_prev[n] = p;
    else
        pq->layer_tail[layer] = p;
}

static void append_to_layer(priority_queue_t *pq, int slot, int layer) {
    pq->slots[slot].priority = layer;
    int t = pq->layer_tail[layer];
    pq->slot_prev[slot] = t;
    pq->slot_next[slot] = -1;
    if (t >= 0)
        pq->slot_next[t] = slot;
    else
        pq->layer_head[layer] = slot;
    pq->layer_tail[layer] = slot;
}

void pq_init(priority_queue_t *pq) {
    if (pq)
        arch_memzero(pq, sizeof(*pq));
    if (pq) {
        for (int i = 0; i < PQ_NUM_PRIORITIES; i++) {
            pq->layer_head[i] = -1;
            pq->layer_tail[i] = -1;
        }
    }
}

pq_handle_t pq_push(priority_queue_t *pq, int priority, task_fn fn, void *arg) {
    if (!pq || priority < 0 || priority >= PQ_NUM_PRIORITIES || pq->size >= PQ_MAX_ITEMS)
        return -1;
    int slot = -1;
    for (int i = 0; i < PQ_MAX_ITEMS; i++) {
        if (pq->slots[i].fn == NULL) {
            slot = i;
            break;
        }
    }
    if (slot < 0)
        return -1;
    pq->slots[slot].fn = fn;
    pq->slots[slot].arg = arg;
    pq->slots[slot].seq = pq->next_seq++;
    append_to_layer(pq, slot, priority);
    pq->size++;
    return (pq_handle_t)slot;
}

/* Pop from a layer's head (caller ensures layer is non-empty) */
static int pop_layer_head(priority_queue_t *pq, int layer, pq_task_t *out) {
    int slot = pq->layer_head[layer];
    if (slot < 0 || !out)
        return -1;
    *out = pq->slots[slot];
    unlink_slot(pq, slot);
    pq->slots[slot].fn = NULL;
    pq->size--;
    return 0;
}

int pq_pop(priority_queue_t *pq, pq_task_t *out) {
    if (!pq || !out || pq->size == 0)
        return -1;
    /* Scan layers 0..3, pop first from lowest non-empty layer */
    for (int layer = 0; layer < PQ_NUM_PRIORITIES; layer++) {
        if (pq->layer_head[layer] >= 0)
            return pop_layer_head(pq, layer, out);
    }
    return -1;
}

int pq_pop_from_layer(priority_queue_t *pq, int layer, pq_task_t *out) {
    if (!pq || layer < 0 || layer >= PQ_NUM_PRIORITIES || !out)
        return -1;
    if (pq->layer_head[layer] < 0)
        return -1;
    return pop_layer_head(pq, layer, out);
}

int pq_has_layer(const priority_queue_t *pq, int layer) {
    if (!pq || layer < 0 || layer >= PQ_NUM_PRIORITIES)
        return 0;
    return pq->layer_head[layer] >= 0;
}

int pq_update(priority_queue_t *pq, pq_handle_t h, int new_priority) {
    if (!pq || h < 0 || h >= PQ_MAX_ITEMS || new_priority < 0 || new_priority >= PQ_NUM_PRIORITIES)
        return -1;
    if (pq->slots[h].fn == NULL)
        return -1;  /* not in queue */
    int old = pq->slots[h].priority;
    if (old == new_priority)
        return 0;
    unlink_slot(pq, h);
    append_to_layer(pq, h, new_priority);
    return 0;
}

int pq_is_empty(const priority_queue_t *pq) {
    return !pq || pq->size == 0;
}

int pq_count(const priority_queue_t *pq) {
    return pq ? pq->size : 0;
}

int pq_count_layer(const priority_queue_t *pq, int layer) {
    if (!pq || layer < 0 || layer >= PQ_NUM_PRIORITIES)
        return 0;
    int n = 0;
    for (int s = pq->layer_head[layer]; s >= 0; s = pq->slot_next[s])
        n++;
    return n;
}
