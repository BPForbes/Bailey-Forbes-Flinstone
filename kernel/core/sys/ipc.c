#include "fl/ipc.h"
#include "mem_asm.h"
#include <stdlib.h>

#ifndef __KERNEL__
#include <pthread.h>
#include <time.h>
#endif

struct pipe {
    uint8_t *buf;
    size_t cap;
    size_t head;
    size_t tail;
    size_t len;
#ifndef __KERNEL__
    pthread_mutex_t mu;
    pthread_cond_t can_read;
    pthread_cond_t can_write;
#endif
};

struct msgq {
    uint8_t *buf;
    size_t max_messages;
    size_t message_size;
    size_t head;
    size_t tail;
    size_t len;
#ifndef __KERNEL__
    pthread_mutex_t mu;
    pthread_cond_t can_read;
    pthread_cond_t can_write;
#endif
};

static size_t min_size(size_t a, size_t b) { return (a < b) ? a : b; }

pipe_t *pipe_create(size_t size) {
    if (size == 0) return NULL;
    pipe_t *p = (pipe_t *)malloc(sizeof(*p));
    if (!p) return NULL;
    asm_mem_zero(p, sizeof(*p));
    p->buf = (uint8_t *)malloc(size);
    if (!p->buf) {
        free(p);
        return NULL;
    }
    p->cap = size;
#ifndef __KERNEL__
    pthread_mutex_init(&p->mu, NULL);
    pthread_cond_init(&p->can_read, NULL);
    pthread_cond_init(&p->can_write, NULL);
#endif
    return p;
}

void pipe_destroy(pipe_t *p) {
    if (!p) return;
#ifndef __KERNEL__
    pthread_mutex_destroy(&p->mu);
    pthread_cond_destroy(&p->can_read);
    pthread_cond_destroy(&p->can_write);
#endif
    free(p->buf);
    free(p);
}

ssize_t pipe_write(pipe_t *p, const void *buf, size_t count) {
    if (!p || !buf || count == 0) return 0;
#ifndef __KERNEL__
    pthread_mutex_lock(&p->mu);
#endif
    size_t written = 0;
    const uint8_t *src = (const uint8_t *)buf;
    while (written < count && p->len < p->cap) {
        p->buf[p->tail] = src[written++];
        p->tail = (p->tail + 1) % p->cap;
        p->len++;
    }
#ifndef __KERNEL__
    if (written > 0) pthread_cond_signal(&p->can_read);
    pthread_mutex_unlock(&p->mu);
#endif
    return (ssize_t)written;
}

ssize_t pipe_read(pipe_t *p, void *buf, size_t count) {
    if (!p || !buf || count == 0) return 0;
#ifndef __KERNEL__
    pthread_mutex_lock(&p->mu);
#endif
    size_t readn = 0;
    uint8_t *dst = (uint8_t *)buf;
    while (readn < count && p->len > 0) {
        dst[readn++] = p->buf[p->head];
        p->head = (p->head + 1) % p->cap;
        p->len--;
    }
#ifndef __KERNEL__
    if (readn > 0) pthread_cond_signal(&p->can_write);
    pthread_mutex_unlock(&p->mu);
#endif
    return (ssize_t)readn;
}

msgq_t *msgq_create(size_t max_messages, size_t message_size) {
    if (max_messages == 0 || message_size == 0) return NULL;
    msgq_t *q = (msgq_t *)malloc(sizeof(*q));
    if (!q) return NULL;
    asm_mem_zero(q, sizeof(*q));
    q->buf = (uint8_t *)malloc(max_messages * message_size);
    if (!q->buf) {
        free(q);
        return NULL;
    }
    q->max_messages = max_messages;
    q->message_size = message_size;
#ifndef __KERNEL__
    pthread_mutex_init(&q->mu, NULL);
    pthread_cond_init(&q->can_read, NULL);
    pthread_cond_init(&q->can_write, NULL);
#endif
    return q;
}

void msgq_destroy(msgq_t *q) {
    if (!q) return;
#ifndef __KERNEL__
    pthread_mutex_destroy(&q->mu);
    pthread_cond_destroy(&q->can_read);
    pthread_cond_destroy(&q->can_write);
#endif
    free(q->buf);
    free(q);
}

int msgq_send(msgq_t *q, const void *msg, size_t size) {
    if (!q || !msg || size == 0) return -1;
#ifndef __KERNEL__
    pthread_mutex_lock(&q->mu);
#endif
    if (q->len >= q->max_messages) {
#ifndef __KERNEL__
        pthread_mutex_unlock(&q->mu);
#endif
        return -1;
    }
    uint8_t *slot = q->buf + (q->tail * q->message_size);
    asm_mem_zero(slot, q->message_size);
    size_t n = min_size(size, q->message_size);
    asm_mem_copy(slot, msg, n);
    q->tail = (q->tail + 1) % q->max_messages;
    q->len++;
#ifndef __KERNEL__
    pthread_cond_signal(&q->can_read);
    pthread_mutex_unlock(&q->mu);
#endif
    return 0;
}

int msgq_receive(msgq_t *q, void *msg, size_t size, uint64_t timeout_ms) {
    (void)timeout_ms;
    if (!q || !msg || size == 0) return -1;
#ifndef __KERNEL__
    pthread_mutex_lock(&q->mu);
#endif
    if (q->len == 0) {
#ifndef __KERNEL__
        pthread_mutex_unlock(&q->mu);
#endif
        return -1;
    }
    const uint8_t *slot = q->buf + (q->head * q->message_size);
    size_t n = min_size(size, q->message_size);
    asm_mem_copy(msg, slot, n);
    q->head = (q->head + 1) % q->max_messages;
    q->len--;
#ifndef __KERNEL__
    pthread_cond_signal(&q->can_write);
    pthread_mutex_unlock(&q->mu);
#endif
    return 0;
}
