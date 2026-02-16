#ifndef PRIORITY_QUEUE_H
#define PRIORITY_QUEUE_H

#define PQ_NUM_PRIORITIES 4
#define PQ_MAX_ITEMS 128

typedef void (*task_fn)(void *arg);

typedef struct pq_task {
    task_fn fn;
    void *arg;
    int priority;
    unsigned int seq;
} pq_task_t;

typedef struct priority_queue {
    pq_task_t items[PQ_MAX_ITEMS];
} priority_queue_t;

void pq_init(priority_queue_t *pq);
int pq_push(priority_queue_t *pq, int priority, task_fn fn, void *arg);
int pq_pop(priority_queue_t *pq, pq_task_t *out);
int pq_is_empty(const priority_queue_t *pq);
int pq_count(const priority_queue_t *pq);

#endif /* PRIORITY_QUEUE_H */
