/**
 * Scheduler-grade MLQ: O(1) push/pop/update/remove, safe handles, nonempty bitmap.
 * ASM-backed: mem_asm for copy/zero (x86-64 and ARM).
 */
#include "fl/sched.h"
#include "priority_queue.h"
#include "mem_asm.h"
#include <string.h>

#define PQ_HANDLE_SLOT(h)  ((int)((unsigned)(h) & 0xFF))
#define PQ_HANDLE_GEN(h)   ((uint16_t)((unsigned)(h) >> 16))
#define PQ_MAKE_HANDLE(gen, slot) ((pq_handle_t)(((unsigned)(gen) << 16) | ((unsigned)(slot) & 0xFF)))

static int handle_valid(const priority_queue_t *pq, pq_handle_t h) {
    if (!pq || h < 0) return 0;
    int slot = PQ_HANDLE_SLOT(h);
    if (slot >= PQ_MAX_ITEMS) return 0;
    if (pq->slots[slot].fn == NULL) return 0;
    return pq->gen[slot] == PQ_HANDLE_GEN(h);
}

static void set_layer_nonempty(priority_queue_t *pq, int layer, int nonempty) {
    if (nonempty)
        pq->nonempty_mask |= (1ULL << layer);
    else
        pq->nonempty_mask &= ~(1ULL << layer);
}

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
    if (pq->layer_head[layer] < 0)
        set_layer_nonempty(pq, layer, 0);
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
    set_layer_nonempty(pq, layer, 1);
}

static void free_slot(priority_queue_t *pq, int slot) {
    asm_mem_zero(&pq->slots[slot], sizeof(pq_task_t));
    pq->free_stack[pq->free_top++] = slot;
}

void pq_init(priority_queue_t *pq) {
    if (pq)
        asm_mem_zero(pq, sizeof(*pq));
    if (pq) {
        pq->free_top = PQ_MAX_ITEMS;
        for (int i = 0; i < PQ_MAX_ITEMS; i++)
            pq->free_stack[i] = i;
        for (int i = 0; i < PQ_NUM_PRIORITIES; i++) {
            pq->layer_head[i] = -1;
            pq->layer_tail[i] = -1;
        }
        pq->nonempty_mask = 0;
    }
}

pq_handle_t pq_push(priority_queue_t *pq, int priority, task_fn fn, void *arg) {
    if (!pq || priority < 0 || priority >= PQ_NUM_PRIORITIES || pq->free_top <= 0)
        return -1;
    int slot = pq->free_stack[--pq->free_top];
    pq->slots[slot].fn = fn;
    pq->slots[slot].arg = arg;
    pq->slots[slot].seq = pq->next_seq++;
    pq->slots[slot].quantum = 0;
    pq->slots[slot].runtime = 0;
    pq->gen[slot] = (uint16_t)(pq->next_gen++);
    append_to_layer(pq, slot, priority);
    pq->size++;
    return PQ_MAKE_HANDLE(pq->gen[slot], slot);
}

/* Pop from a layer's head; caller ensures layer is non-empty */
static int pop_layer_head(priority_queue_t *pq, int layer, pq_task_t *out) {
    int slot = pq->layer_head[layer];
    if (slot < 0 || !out)
        return -1;
    asm_mem_copy(out, &pq->slots[slot], sizeof(pq_task_t));
    unlink_slot(pq, slot);
    free_slot(pq, slot);
    pq->size--;
    return 0;
}

int pq_pop(priority_queue_t *pq, pq_task_t *out) {
    if (!pq || !out || pq->size == 0)
        return -1;
    if (pq->nonempty_mask == 0)
        return -1;
    int layer = __builtin_ctzll(pq->nonempty_mask);
    return pop_layer_head(pq, layer, out);
}

int pq_pop_from_layer(priority_queue_t *pq, int layer, pq_task_t *out) {
    if (!pq || layer < 0 || layer >= PQ_NUM_PRIORITIES || !out)
        return -1;
    if ((pq->nonempty_mask & (1ULL << layer)) == 0)
        return -1;
    return pop_layer_head(pq, layer, out);
}

int pq_has_layer(const priority_queue_t *pq, int layer) {
    if (!pq || layer < 0 || layer >= PQ_NUM_PRIORITIES)
        return 0;
    return (pq->nonempty_mask & (1ULL << layer)) != 0;
}

int pq_update(priority_queue_t *pq, pq_handle_t h, int new_priority) {
    if (!pq || new_priority < 0 || new_priority >= PQ_NUM_PRIORITIES)
        return -1;
    if (!handle_valid(pq, h))
        return -1;
    int slot = PQ_HANDLE_SLOT(h);
    int old = pq->slots[slot].priority;
    if (old == new_priority)
        return 0;
    unlink_slot(pq, slot);
    append_to_layer(pq, slot, new_priority);
    return 0;
}

int pq_remove(priority_queue_t *pq, pq_handle_t h) {
    if (!pq || !handle_valid(pq, h))
        return -1;
    int slot = PQ_HANDLE_SLOT(h);
    unlink_slot(pq, slot);
    free_slot(pq, slot);
    pq->size--;
    return 0;
}

void pq_set_quantum(priority_queue_t *pq, pq_handle_t h, uint32_t quantum) {
    if (!pq || !handle_valid(pq, h))
        return;
    int slot = PQ_HANDLE_SLOT(h);
    pq->slots[slot].quantum = quantum;
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
