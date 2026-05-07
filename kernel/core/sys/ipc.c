#include "fl/ipc.h"
#include "mem_asm.h"
#include <errno.h>
#include <stdlib.h>

#if defined(__KERNEL__) || defined(EMSCRIPTEN_SINGLE_THREAD)
#include "core/sys/spinlock.h"
typedef volatile int fl_ipc_lock_t;
#define FL_IPC_LOCK_INIT SPINLOCK_INIT
/**
 * Acquire the given IPC spinlock.
 *
 * Blocks (spins) until the provided lock is successfully acquired.
 *
 * @param lock Pointer to the spinlock to acquire.
 */
static inline void fl_ipc_lock(fl_ipc_lock_t *lock) { spinlock_acquire(lock); }
/**
 * Release a previously acquired IPC spinlock.
 *
 * @param lock Pointer to the spinlock to release; must have been previously acquired. 
 */
static inline void fl_ipc_unlock(fl_ipc_lock_t *lock) { spinlock_release(lock); }
#define IPC_KERNELLIKE_SYNC 1
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
#ifdef IPC_KERNELLIKE_SYNC
    fl_ipc_lock_t lock;
#else
    pthread_mutex_t mu;
    pthread_cond_t can_read;
    pthread_cond_t can_write;  /* TODO: add wait in pipe_write for blocking writes */
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
#ifdef IPC_KERNELLIKE_SYNC
    fl_ipc_lock_t lock;
#else
    pthread_mutex_t mu;
    pthread_cond_t can_read;
    pthread_cond_t can_write;  /* TODO: add wait in msgq_send for blocking sends */
    pthread_cond_t drain;
    int closing;
    int waiters;
#endif
};

/**
 * Select the smaller of two size values.
 * @param a First size value to compare.
 * @param b Second size value to compare.
 * @returns `a` if `a < b`, otherwise `b`.
 */
static size_t min_size(size_t a, size_t b) { return (a < b) ? a : b; }

#if !defined(IPC_KERNELLIKE_SYNC)
/**
 * Compute an absolute CLOCK_MONOTONIC deadline timeout_ms milliseconds from now.
 *
 * Populates `ts` with the current CLOCK_MONOTONIC time plus `timeout_ms` milliseconds.
 * The resulting `ts->tv_nsec` is normalized to be less than 1,000,000,000.
 *
 * @param timeout_ms Milliseconds to add to the current monotonic time.
 * @param ts Pointer to a timespec structure that will be set to the absolute deadline.
 */
static void make_abs_timeout(uint64_t timeout_ms, struct timespec *ts) {
    clock_gettime(CLOCK_MONOTONIC, ts);
    ts->tv_sec += (time_t)(timeout_ms / 1000);
    ts->tv_nsec += (long)((timeout_ms % 1000) * 1000000ULL);
    if (ts->tv_nsec >= 1000000000L) {
        ts->tv_sec++;
        ts->tv_nsec -= 1000000000L;
    }
}
#endif

/**
 * Create a bounded byte pipe with the specified capacity.
 *
 * Allocates and initializes a pipe_t and its internal byte buffer. On success
 * the buffer is zeroed and synchronization primitives are initialized for the
 * configured synchronization mode (kernel-like spinlock or pthread mutex/conds).
 *
 * @param size Capacity in bytes for the pipe's circular buffer; must be > 0.
 * @returns Pointer to the newly allocated `pipe_t` on success, or `NULL` if
 *          `size` is zero or if any allocation or synchronization primitive
 *          initialization fails.
 */
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
#ifdef IPC_KERNELLIKE_SYNC
    p->lock = FL_IPC_LOCK_INIT;
#else
    if (pthread_mutex_init(&p->mu, NULL) != 0) {
        free(p->buf);
        free(p);
        return NULL;
    }
    if (pthread_cond_init(&p->can_read, NULL) != 0) {
        pthread_mutex_destroy(&p->mu);
        free(p->buf);
        free(p);
        return NULL;
    }
    if (pthread_cond_init(&p->can_write, NULL) != 0) {
        pthread_cond_destroy(&p->can_read);
        pthread_mutex_destroy(&p->mu);
        free(p->buf);
        free(p);
        return NULL;
    }
    if (pthread_cond_init(&p->drain, NULL) != 0) {
        pthread_cond_destroy(&p->can_write);
        pthread_cond_destroy(&p->can_read);
        pthread_mutex_destroy(&p->mu);
        free(p->buf);
        free(p);
        return NULL;
    }
#endif
    return p;
}

/**
 * Close and free a pipe, waking blocked operations and releasing its resources.
 *
 * If `p` is NULL the function does nothing. In pthread mode the function sets
 * the pipe's `closing` flag, broadcasts `can_read` and `can_write` to wake any
 * blocked readers/writers, waits for active waiters to drain, then destroys
 * the mutex and condition variables. Finally the pipe's buffer and the pipe
 * object itself are freed.
 *
 * @param p Pipe to destroy; ownership is released and the pointer must not be used after call.
 */
void pipe_destroy(pipe_t *p) {
    if (!p) {
        return;
    }
#ifndef IPC_KERNELLIKE_SYNC
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
    pthread_cond_destroy(&p->can_write);  /* TODO: used in pipe_write when blocking */
    pthread_cond_destroy(&p->drain);
#endif
    free(p->buf);
    free(p);
}

/**
 * Write up to `count` bytes from `buf` into the pipe's circular buffer.
 *
 * Attempts to append bytes to the pipe until either `count` bytes have been written
 * or the pipe's capacity is reached. Does not block if the buffer is full; fewer
 * than `count` bytes may be written in that case.
 *
 * @param p    Destination pipe; must be non-NULL.
 * @param buf  Source buffer; must be non-NULL.
 * @param count Number of bytes to write; must be greater than zero.
 *
 * @returns Number of bytes actually written (0 up to `count`), or `-1` on error.
 *          Errors occur when `p` or `buf` is NULL, `count` is zero, or the pipe
 *          is in the process of closing.
 */
ssize_t pipe_write(pipe_t *p, const void *buf, size_t count) {
    if (!p || !buf || count == 0) {
        return -1;
    }
#ifdef IPC_KERNELLIKE_SYNC
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
#ifdef IPC_KERNELLIKE_SYNC
    fl_ipc_unlock(&p->lock);
#else
    if (written > 0) pthread_cond_broadcast(&p->can_read);
    pthread_mutex_unlock(&p->mu);
#endif
    return (ssize_t)written;
}

/**
 * Read up to `count` bytes from the pipe into `buf`.
 *
 * Blocks until at least one byte is available or the pipe is closed (in pthread mode);
 * in kernel-like mode the call returns immediately if no data is available.
 *
 * @param p Pointer to the pipe to read from.
 * @param buf Destination buffer to receive data.
 * @param count Maximum number of bytes to read.
 * @returns The number of bytes actually read (0 or more) on success, or `-1` on error
 *          (invalid arguments or if the pipe is closed and no data is available).
 */
ssize_t pipe_read(pipe_t *p, void *buf, size_t count) {
    if (!p || !buf || count == 0) {
        return -1;
    }
#ifdef IPC_KERNELLIKE_SYNC
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
#ifdef IPC_KERNELLIKE_SYNC
    fl_ipc_unlock(&p->lock);
#else
    if (readn > 0) pthread_cond_broadcast(&p->can_write);
    pthread_mutex_unlock(&p->mu);
#endif
    return (ssize_t)readn;
}

/**
 * Create a new fixed-size message queue.
 *
 * Allocates and initializes a msgq_t containing a circular buffer capable of
 * holding up to `max_messages` messages each of `message_size` bytes, and
 * prepares synchronization primitives appropriate for the build configuration.
 *
 * @param max_messages Maximum number of messages the queue can hold; must be > 0.
 * @param message_size Size in bytes of each message; must be > 0.
 * @returns Pointer to an initialized msgq_t on success.
 *          NULL if `max_messages` or `message_size` is zero, if memory allocation
 *          fails, or if initialization of required synchronization primitives fails.
 */
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
#ifdef IPC_KERNELLIKE_SYNC
    q->lock = FL_IPC_LOCK_INIT;
#else
    if (pthread_mutex_init(&q->mu, NULL) != 0) {
        free(q->buf);
        free(q);
        return NULL;
    }
    pthread_condattr_t attr;
    if (pthread_condattr_init(&attr) != 0) {
        pthread_mutex_destroy(&q->mu);
        free(q->buf);
        free(q);
        return NULL;
    }
    if (pthread_condattr_setclock(&attr, CLOCK_MONOTONIC) != 0) {
        pthread_condattr_destroy(&attr);
        pthread_mutex_destroy(&q->mu);
        free(q->buf);
        free(q);
        return NULL;
    }
    if (pthread_cond_init(&q->can_read, &attr) != 0) {
        pthread_condattr_destroy(&attr);
        pthread_mutex_destroy(&q->mu);
        free(q->buf);
        free(q);
        return NULL;
    }
    pthread_condattr_destroy(&attr);
    if (pthread_cond_init(&q->can_write, NULL) != 0) {
        pthread_cond_destroy(&q->can_read);
        pthread_mutex_destroy(&q->mu);
        free(q->buf);
        free(q);
        return NULL;
    }
    if (pthread_cond_init(&q->drain, NULL) != 0) {
        pthread_cond_destroy(&q->can_write);
        pthread_cond_destroy(&q->can_read);
        pthread_mutex_destroy(&q->mu);
        free(q->buf);
        free(q);
        return NULL;
    }
#endif
    return q;
}

/**
 * Destroy a message queue and free its resources.
 *
 * If `q` is NULL the call is a no-op. Otherwise this function marks the
 * queue as closing, wakes any threads blocked on the queue, waits for active
 * waiters to drain, destroys synchronization primitives, and frees the
 * queue's buffer and the `msgq_t` itself.
 *
 * @param q Pointer to the message queue to destroy, or NULL.
 */
void msgq_destroy(msgq_t *q) {
    if (!q) {
        return;
    }
#ifndef IPC_KERNELLIKE_SYNC
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
    pthread_cond_destroy(&q->can_write);  /* TODO: used in msgq_send when blocking */
    pthread_cond_destroy(&q->drain);
#endif
    free(q->buf);
    free(q);
}

/**
 * Enqueue a message into the fixed-size message queue, copying up to the queue's message size.
 *
 * The function copies `size` bytes from `msg` into the next available slot, zeroing the remainder
 * of the slot if `size` is smaller than the queue's configured message size. If using pthread-based
 * synchronization, waiting receivers are notified when a message is inserted.
 *
 * @param q Pointer to the message queue.
 * @param msg Pointer to the message data to send.
 * @param size Number of bytes from `msg` to copy into the queue slot; must be > 0 and <= queue message size.
 * @returns `0` on success, `-1` on failure (invalid arguments, `size` exceeds slot size, queue is full, or queue is closing).
 */
int msgq_send(msgq_t *q, const void *msg, size_t size) {
    if (!q || !msg || size == 0) {
        return -1;
    }
    if (size > q->message_size) {
        return -1;
    }
#ifdef IPC_KERNELLIKE_SYNC
    fl_ipc_lock(&q->lock);
#else
    pthread_mutex_lock(&q->mu);
    if (q->closing) {
        pthread_mutex_unlock(&q->mu);
        return -1;
    }
#endif
    if (q->len >= q->max_messages) {
#ifdef IPC_KERNELLIKE_SYNC
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
#ifdef IPC_KERNELLIKE_SYNC
    fl_ipc_unlock(&q->lock);
#else
    pthread_cond_broadcast(&q->can_read);
    pthread_mutex_unlock(&q->mu);
#endif
    return 0;
}

/**
 * Receive a single message from the message queue, waiting up to the given timeout.
 *
 * If a message is available this copies up to `min(size, q->message_size)` bytes into
 * `msg`, advances the queue head, and makes space available for writers. When compiled
 * with pthread synchronization the call will block up to `timeout_ms` milliseconds if
 * the queue is empty; a `timeout_ms` value of 0 causes an immediate (non-blocking) return.
 * If the queue is closing or an error/timeout occurs, the function fails without copying.
 *
 * @param q Pointer to the message queue to receive from.
 * @param msg Destination buffer to receive the message into.
 * @param size Size of the destination buffer; only up to `min(size, q->message_size)` bytes are copied.
 * @param timeout_ms Maximum time in milliseconds to wait for a message when using pthread synchronization (ignored in kernel-like mode).
 * @returns 0 on success, -1 on error (invalid arguments, timeout, queue closing, or no message available).
 */
int msgq_receive(msgq_t *q, void *msg, size_t size, uint64_t timeout_ms) {
    if (!q || !msg || size == 0) {
        return -1;
    }
#ifdef IPC_KERNELLIKE_SYNC
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
#ifdef IPC_KERNELLIKE_SYNC
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
#ifdef IPC_KERNELLIKE_SYNC
    fl_ipc_unlock(&q->lock);
#else
    pthread_cond_broadcast(&q->can_write);
    pthread_mutex_unlock(&q->mu);
#endif
    return 0;
}
