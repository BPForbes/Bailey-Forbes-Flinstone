#ifndef CMD_SERVER_FILE_H
#define CMD_SERVER_FILE_H

#include "net_client.h"
#include "net_server.h"
#include "server_bg.h"

#include <pthread.h>

typedef struct cmd_server_ctx {
    pthread_mutex_t *mutex;
    fl_net_server_t *server;
    fl_server_bg_t **server_bg;
    int *server_running;
    fl_net_client_t *client;
    fl_server_bg_t **client_bg;
} cmd_server_ctx_t;

int cmd_server_file_run(const cmd_server_ctx_t *ctx, int argc, char **argv);

/** Gives the delivery module the history gate and source for the deliveries this shell sent (FL-04). Call once,
 *  after fl_net_kq_install. Until it is called every history request is refused, as before. */
void cmd_server_file_install_history(void);
int cmd_server_send_file_alias(const cmd_server_ctx_t *ctx, int argc, char **argv);

#endif /* CMD_SERVER_FILE_H */
