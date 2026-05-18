#ifndef FL_IPC_H
#define FL_IPC_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Pipe */
typedef struct pipe pipe_t;

pipe_t *pipe_create(size_t size);
void pipe_destroy(pipe_t *pipe);
/* buf must be non-NULL; NULL is rejected with return value -1. */
ssize_t pipe_read(pipe_t *pipe, void *buf, size_t count);
/*
 * buf must be non-NULL; NULL or count==0 is rejected with -1 (errno EINVAL on host).
 * Otherwise returns bytes written (may be short). If the ring is full and count>0,
 * returns -1 with errno EAGAIN on host (non-blocking "try again"). Broken/closing pipe: -1, errno EPIPE.
 */
ssize_t pipe_write(pipe_t *pipe, const void *buf, size_t count);

/* Message queue */
typedef struct msgq msgq_t;

msgq_t *msgq_create(size_t max_messages, size_t message_size);
void msgq_destroy(msgq_t *msgq);
/*
 * On host: EINVAL invalid args; EPIPE queue closing; EAGAIN queue full (non-blocking).
 * Returns 0 on success, -1 on failure.
 */
int msgq_send(msgq_t *msgq, const void *msg, size_t size);
/* Host: EINVAL invalid args; EPIPE closing; EAGAIN no message (timeout_ms==0); ETIMEDOUT wait timed out. */
int msgq_receive(msgq_t *msgq, void *msg, size_t size, uint64_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* FL_IPC_H */
