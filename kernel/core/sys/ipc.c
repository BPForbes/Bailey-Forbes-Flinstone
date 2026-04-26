#include "fl/ipc.h"
#include "mem_asm.h"
#include <errno.h>
#include <stdlib.h>

#ifdef __KERNEL__
#include "core/sys/spinlock.h"
typedef volatile int fl_ipc_lock_t;
#define FL_IPC_LOCK_INIT SPINLOCK_INIT
static inline void fl_ipc_lock(fl_ipc_lock_t *lock) { spinlock_acquire(lock); }
static inline void fl_ipc_unlock(fl_ipc_lock_t *lock) { spinlock_release(lock); }
#else
#include <pthread.h>
#include <time.h>
#endif

struct pipe {
    uint8_t *buf;
    size_t cap;
    size_t head;
    size_t tail;
    size_t len;
#ifdef __KERNEL__
    fl_ipc_lock_t lock;
#else
    pthread_mutex_t mu;
    pthread_cond_t can_read;
    pthread_cond_t can_write;
    pthread_cond_t drain;
    int closing;
    int waiters;
#endif
};

struct msgq {
    uint8_t *buf;
    size_t max_messages;
    size_t message_size;
    size_t head;
    size_t tail;
    size_t len;
#ifdef __KERNEL__
    fl_ipc_lock_t lock;
#else
    pthread_mutex_t mu;
    pthread_cond_t can_read;
    pthread_cond_t can_write;
    pthread_cond_t drain;
    int closing;
    int waiters;
#endif
};

static size_t min_size(size_t a, size_t b) { return (a < b) ? a : b; }

#ifndef __KERNEL__
static void make_abs_timeout(uint64_t timeout_ms, struct timespec *ts) {
    clock_gettime(CLOCK_REALTIME, ts);
    ts->tv_sec += (time_t)(timeout_ms / 1000);
    ts->tv_nsec += (long)((timeout_ms % 1000) * 1000000ULL);
    if (ts->tv_nsec >= 1000000000L) {
        ts->tv_sec++;
        ts->tv_nsec -= 1000000000L;
    }
}
#endif

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
#ifdef __KERNEL__
    p->lock = FL_IPC_LOCK_INIT;
#else
    pthread_mutex_init(&p->mu, NULL);
    pthread_cond_init(&p->can_read, NULL);
    pthread_cond_init(&p->can_write, NULL);
    pthread_cond_init(&p->drain, NULL);
#endif
    return p;
}

void pipe_destroy(pipe_t *p) {
    if (!p) {
        return;
    }
#ifndef __KERNEL__
    pthread_mutex_lock(&p->mu);
    p->closing = 1;
    pthread_cond_broadcast(&p->can_read);
    pthread_cond_broadcast(&p->can_write);
    while (p->waiters > 0) {
        pthread_cond_wait(&p->drain, &p->mu);
    }
    pthread_mutex_unlock(&p->mu);
    pthread_mutex_destroy(&p->mu);
    pthread_cond_destroy(&p->can_read);
    pthread_cond_destroy(&p->can_write);
    pthread_cond_destroy(&p->drain);
#endif
    free(p->buf);
    free(p);
}

ssize_t pipe_write(pipe_t *p, const void *buf, size_t count) {
    if (!p || !buf || count == 0) {
        return -1;
    }
#ifdef __KERNEL__
    fl_ipc_lock(&p->lock);
#else
    pthread_mutex_lock(&p->mu);
    if (p->closing) {
        pthread_mutex_unlock(&p->mu);
        return -1;
    }
#endif
    size_t written = 0;
    const uint8_t *src = (const uint8_t *)buf;
    while (written < count && p->len < p->cap) {
        p->buf[p->tail] = src[written++];
        p->tail = (p->tail + 1) % p->cap;
        p->len++;
    }
#ifdef __KERNEL__
    fl_ipc_unlock(&p->lock);
#else
    if (written > 0) pthread_cond_broadcast(&p->can_read);
    pthread_mutex_unlock(&p->mu);
#endif
    return (ssize_t)written;
}

ssize_t pipe_read(pipe_t *p, void *buf, size_t count) {
    if (!p || !buf || count == 0) {
        return -1;
    }
#ifdef __KERNEL__
    fl_ipc_lock(&p->lock);
    if (p->len == 0) {
        fl_ipc_unlock(&p->lock);
        return -1;
    }
#else
    pthread_mutex_lock(&p->mu);
    while (p->len == 0 && !p->closing) {
        p->waiters++;
        pthread_cond_wait(&p->can_read, &p->mu);
        p->waiters--;
        pthread_cond_signal(&p->drain);
    }
    if (p->closing && p->len == 0) {
        pthread_mutex_unlock(&p->mu);
        return -1;
    }
#endif
    size_t readn = 0;
    uint8_t *dst = (uint8_t *)buf;
    while (readn < count && p->len > 0) {
        dst[readn++] = p->buf[p->head];
        p->head = (p->head + 1) % p->cap;
        p->len--;
    }
#ifdef __KERNEL__
    fl_ipc_unlock(&p->lock);
#else
    if (readn > 0) pthread_cond_broadcast(&p->can_write);
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
#ifdef __KERNEL__
    q->lock = FL_IPC_LOCK_INIT;
#else
    pthread_mutex_init(&q->mu, NULL);
    pthread_cond_init(&q->can_read, NULL);
    pthread_cond_init(&q->can_write, NULL);
    pthread_cond_init(&q->drain, NULL);
#endif
    return q;
}

void msgq_destroy(msgq_t *q) {
    if (!q) {
        return;
    }
#ifndef __KERNEL__
    pthread_mutex_lock(&q->mu);
    q->closing = 1;
    pthread_cond_broadcast(&q->can_read);
    pthread_cond_broadcast(&q->can_write);
    while (q->waiters > 0) {
        pthread_cond_wait(&q->drain, &q->mu);
    }
    pthread_mutex_unlock(&q->mu);
    pthread_mutex_destroy(&q->mu);
    pthread_cond_destroy(&q->can_read);
    pthread_cond_destroy(&q->can_write);
    pthread_cond_destroy(&q->drain);
#endif
    free(q->buf);
    free(q);
}

int msgq_send(msgq_t *q, const void *msg, size_t size) {
    if (!q || !msg || size == 0) {
        return -1;
    }
    if (size > q->message_size) {
        return -1;
    }
#ifdef __KERNEL__
    fl_ipc_lock(&q->lock);
#else
    pthread_mutex_lock(&q->mu);
    if (q->closing) {
        pthread_mutex_unlock(&q->mu);
        return -1;
    }
#endif
    if (q->len >= q->max_messages) {
#ifdef __KERNEL__
        fl_ipc_unlock(&q->lock);
#else
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
#ifdef __KERNEL__
    fl_ipc_unlock(&q->lock);
#else
    pthread_cond_broadcast(&q->can_read);
    pthread_mutex_unlock(&q->mu);
#endif
    return 0;
}

int msgq_receive(msgq_t *q, void *msg, size_t size, uint64_t timeout_ms) {
    if (!q || !msg || size == 0) {
        return -1;
    }
#ifdef __KERNEL__
    (void)timeout_ms;
    fl_ipc_lock(&q->lock);
#else
    pthread_mutex_lock(&q->mu);
    if (q->closing) {
        pthread_mutex_unlock(&q->mu);
        return -1;
    }
    if (q->len == 0) {
        if (timeout_ms == 0) {
            pthread_mutex_unlock(&q->mu);
            return -1;
        }
        struct timespec deadline;
        make_abs_timeout(timeout_ms, &deadline);
        while (q->len == 0 && !q->closing) {
            q->waiters++;
            int rc = pthread_cond_timedwait(&q->can_read, &q->mu, &deadline);
            q->waiters--;
            pthread_cond_signal(&q->drain);
            if (rc == ETIMEDOUT) {
                pthread_mutex_unlock(&q->mu);
                return -1;
            }
            if (rc != 0) {
                pthread_mutex_unlock(&q->mu);
                return -1;
            }
        }
    }
    if (q->closing && q->len == 0) {
        pthread_mutex_unlock(&q->mu);
        return -1;
    }
#endif
    if (q->len == 0) {
#ifdef __KERNEL__
        fl_ipc_unlock(&q->lock);
#else
        pthread_mutex_unlock(&q->mu);
#endif
        return -1;
    }
    const uint8_t *slot = q->buf + (q->head * q->message_size);
    size_t n = min_size(size, q->message_size);
    asm_mem_copy(msg, slot, n);
    q->head = (q->head + 1) % q->max_messages;
    q->len--;
#ifdef __KERNEL__
    fl_ipc_unlock(&q->lock);
#else
    pthread_cond_broadcast(&q->can_write);
    pthread_mutex_unlock(&q->mu);
#endif
    return 0;
}
