/*
 * test_p3_udp_cmds.c — Loopback echo test for the `udpsend` and `udplisten`
 * shell verbs (issue #239 acceptance: "udpsend and udplisten pass loopback
 * echo test"). Drives the same argc/argv path the shell dispatch uses,
 * with udplisten running on a background thread so the synchronous udpsend
 * can deliver one datagram and the listener can prove the round-trip.
 */

#include "cmd_decl.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define ASSERT(c) \
    do { \
        if (!(c)) { \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); \
            return 1; \
        } \
    } while (0)

typedef struct {
    int rc;
} listen_args_t;

static void *listen_thread(void *arg) {
    listen_args_t *la = (listen_args_t *)arg;
    char *argv[] = {(char *)"udplisten", (char *)"58921", (char *)"-c",
                    (char *)"1", (char *)"-W", (char *)"5000",
                    (char *)"-bind", (char *)"127.0.0.1", NULL};
    int argc = 8;
    la->rc = cmd_udplisten_run(argc, argv);
    return NULL;
}

static int test_udp_cmds_loopback(void) {
    pthread_t tid;
    listen_args_t la = {0};
    int send_rc;
    char *send_argv[] = {(char *)"udpsend", (char *)"127.0.0.1:58921",
                         (char *)"hello", (char *)"udp", (char *)"world",
                         NULL};
    int send_argc = 5;

    ASSERT(pthread_create(&tid, NULL, listen_thread, &la) == 0);
    /* Let udplisten bind before udpsend fires. 50 ms is generous on a
     * loopback. */
    usleep(50 * 1000);

    send_rc = cmd_udpsend_run(send_argc, send_argv);
    ASSERT(send_rc == 0);

    pthread_join(tid, NULL);
    ASSERT(la.rc == 0);
    return 0;
}

int main(void) {
    printf("test_udp_cmds_loopback... ");
    if (test_udp_cmds_loopback() != 0)
        return 1;
    puts("ok");
    puts("test_p3_udp_cmds: all passed");
    return 0;
}
