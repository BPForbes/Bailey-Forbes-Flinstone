#ifndef FL_IPC_H
#define FL_IPC_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Pipe */
typedef struct pipe pipe_t;

pipe_t *pipe_create(size_t size);
void pipe_destroy(pipe_t *pipe);
ssize_t pipe_read(pipe_t *pipe, void *buf, size_t count);
ssize_t pipe_write(pipe_t *pipe, const void *buf, size_t count);

/* Message queue */
typedef struct msgq msgq_t;

msgq_t *msgq_create(size_t max_messages, size_t message_size);
void msgq_destroy(msgq_t *msgq);
int msgq_send(msgq_t *msgq, const void *msg, size_t size);
int msgq_receive(msgq_t *msgq, void *msg, size_t size, uint64_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* FL_IPC_H */
