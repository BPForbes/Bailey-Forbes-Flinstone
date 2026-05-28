/*
 * tests/test_p3_server.c
 *
 * Foundations test for P3-13 server / client / announcement layering.
 * Runs entirely against the in-process loopback (127.0.0.1) using the hosted
 * fl_net_sock_* path. Skips cleanly with status 0 on environments where
 * sockets are unavailable (FL_RESULT_NOSYS).
 *
 * Coverage:
 *   - host_start + HELLO/HELLO_ACK + JOIN_ANNOUNCE
 *   - SERVER_ANNOUNCE broadcast and OP_SERVER_ANNOUNCE event delivery
 *   - duplicate principal disambiguation ("Alice" + "Alice" -> "Alice {1}", "Alice {2}")
 *   - host-driven nick set + collision rejection + NICK_SET_ANNOUNCE
 *   - rejection when nick equals another live principal
 *   - leave -> LEAVE_ANNOUNCE on remaining peer
 */

#include "net_client.h"
#include "net_server.h"
#include "net_socket.h"
#include "server_bg.h"
#include "fl_colors.h"

#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#define ASSERT(c)                                                              \
    do {                                                                       \
        if (!(c)) {                                                            \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);       \
            return 1;                                                          \
        }                                                                      \
    } while (0)

/* ------------------------------------------------------------------------- */
/* Event capture                                                             */
/* ------------------------------------------------------------------------- */

typedef struct {
    int joins;
    int leaves;
    int nick_announces;
    int server_announces;
    int msgs;
    int errs;
    int closed;
    char last_join[160];
    char last_leave[160];
    char last_nick_announce[256];
    char last_server_announce[256];
    char last_msg[256];
    char last_err[256];
} event_log_t;

static void event_sink(fl_net_server_event_kind_t kind, const char *text,
                       fl_net_server_member_id_t mid, void *data) {
    event_log_t *log = (event_log_t *)data;
    (void)mid;
    if (!log)
        return;
    switch (kind) {
    case FL_NET_SERVER_EVENT_JOIN_ANNOUNCE:
        log->joins++;
        snprintf(log->last_join, sizeof(log->last_join), "%s", text ? text : "");
        break;
    case FL_NET_SERVER_EVENT_LEAVE_ANNOUNCE:
        log->leaves++;
        snprintf(log->last_leave, sizeof(log->last_leave), "%s", text ? text : "");
        break;
    case FL_NET_SERVER_EVENT_NICK_SET_ANNOUNCE:
        log->nick_announces++;
        snprintf(log->last_nick_announce, sizeof(log->last_nick_announce), "%s",
                 text ? text : "");
        break;
    case FL_NET_SERVER_EVENT_SERVER_ANNOUNCE:
        log->server_announces++;
        snprintf(log->last_server_announce, sizeof(log->last_server_announce),
                 "%s", text ? text : "");
        break;
    case FL_NET_SERVER_EVENT_MSG:
        log->msgs++;
        snprintf(log->last_msg, sizeof(log->last_msg), "%s", text ? text : "");
        break;
    case FL_NET_SERVER_EVENT_ERR:
        log->errs++;
        snprintf(log->last_err, sizeof(log->last_err), "%s", text ? text : "");
        break;
    case FL_NET_SERVER_EVENT_CLOSED:
        log->closed++;
        break;
    default:
        break;
    }
}

/* Pump the foreground clients a few times. The server runs in a real
 * background thread (server_bg) so the test can use synchronous client
 * connects without deadlocking on missing accept_pending calls. */
static void pump(fl_net_client_t *clients[], event_log_t *logs[],
                 unsigned n_clients, unsigned iters) {
    for (unsigned i = 0; i < iters; i++) {
        for (unsigned k = 0; k < n_clients; k++)
            (void)fl_net_client_poll(clients[k], event_sink, logs[k], 8u);
        struct timespec ts = { 0, 5 * 1000 * 1000 }; /* 5ms */
        nanosleep(&ts, NULL);
    }
}

/* ------------------------------------------------------------------------- */
/* Test bodies                                                               */
/* ------------------------------------------------------------------------- */

static int test_announce_join_leave_nick(void) {
    fl_net_server_t srv;
    fl_net_client_t cA, cB;
    event_log_t logA = {0}, logB = {0};
    fl_net_client_t *clients[2] = { &cA, &cB };
    event_log_t *logs[2] = { &logA, &logB };
    fl_server_bg_t *bg = NULL;
    uint32_t loopback = htonl(0x7F000001u); /* 127.0.0.1 */
    const uint16_t port = 49801u;
    fl_result_t rc;

    fl_color_set_disabled(1);

    rc = fl_net_server_host_start(&srv, loopback, port, "HostUser");
    if (rc == FL_RESULT_NOSYS) {
        fprintf(stderr, "skip: hosted sockets unavailable\n");
        return 0;
    }
    ASSERT(rc == FL_RESULT_OK);
    ASSERT(fl_net_server_member_count(&srv) == 1);

    /* Spin up the server background loop so synchronous client connects can
     * complete their HELLO/HELLO_ACK handshake without a foreground tick. */
    ASSERT(fl_server_bg_start_server(&srv, &bg) == FL_RESULT_OK);

    fl_net_client_init(&cA);
    fl_net_client_init(&cB);

    /* Client A joins as "Alice" — first instance, no disambig braces. */
    rc = fl_net_client_connect(&cA, loopback, port, "Alice", 2000u);
    ASSERT(rc == FL_RESULT_OK);
    pump(clients, logs, 1, 20);

    /* Client B joins also as "Alice" — collides. */
    rc = fl_net_client_connect(&cB, loopback, port, "Alice", 2000u);
    ASSERT(rc == FL_RESULT_OK);
    pump(clients, logs, 2, 20);

    ASSERT(fl_net_server_member_count(&srv) == 3); /* host + 2 clients */

    /* JOIN_ANNOUNCE: A should have seen B's join announcement. */
    ASSERT(logA.joins >= 1);
    /* B should have seen its own join (host emits announce to all). */
    ASSERT(logB.joins >= 1);

    /* Disambiguation rendered on host: Alice -> "Alice {1}" + "Alice {2}". */
    {
        char disp1[FL_NET_SERVER_DISPLAY_NAME_MAX] = {0};
        char disp2[FL_NET_SERVER_DISPLAY_NAME_MAX] = {0};
        const fl_net_server_member_t *m;
        unsigned saw1 = 0, saw2 = 0;
        for (size_t i = 0; i < fl_net_server_member_count(&srv); i++) {
            m = fl_net_server_member_at(&srv, i);
            if (!m || m->is_host)
                continue;
            char buf[FL_NET_SERVER_DISPLAY_NAME_MAX];
            ASSERT(fl_net_server_member_display(&srv, m->member_id, buf,
                                                sizeof(buf)) == FL_RESULT_OK);
            if (strcmp(buf, "Alice {1}") == 0) {
                strncpy(disp1, buf, sizeof(disp1) - 1);
                saw1 = 1;
            } else if (strcmp(buf, "Alice {2}") == 0) {
                strncpy(disp2, buf, sizeof(disp2) - 1);
                saw2 = 1;
            }
        }
        ASSERT(saw1 && saw2);
        (void)disp1;
        (void)disp2;
    }

    /* SERVER_ANNOUNCE from host -> both clients receive. */
    {
        int baseA = logA.server_announces;
        int baseB = logB.server_announces;
        ASSERT(fl_net_server_announce(&srv, "lobby open") == FL_RESULT_OK);
        pump(clients, logs, 2, 20);
        ASSERT(logA.server_announces == baseA + 1);
        ASSERT(logB.server_announces == baseB + 1);
        ASSERT(strstr(logA.last_server_announce, "lobby open") != NULL);
        ASSERT(strstr(logB.last_server_announce, "lobby open") != NULL);
    }

    /* Host-driven global nick on member_id 2 (first Alice) -> "Jeff".
     * NICK_SET_ANNOUNCE goes to everyone. */
    {
        int baseA = logA.nick_announces;
        int baseB = logB.nick_announces;
        rc = fl_net_server_set_host_nick(&srv,
                                         (fl_net_server_member_id_t)2u, "Jeff");
        ASSERT(rc == FL_RESULT_OK);
        pump(clients, logs, 2, 20);
        ASSERT(logA.nick_announces == baseA + 1);
        ASSERT(logB.nick_announces == baseB + 1);
        ASSERT(strstr(logA.last_nick_announce, "Jeff") != NULL);
        ASSERT(strstr(logB.last_nick_announce, "Jeff") != NULL);
    }

    /* Nick collision: trying to nick the other Alice to "HostUser" (real
     * username of another connected member) must be rejected. */
    rc = fl_net_server_set_host_nick(&srv, (fl_net_server_member_id_t)3u,
                                     "HostUser");
    ASSERT(rc == FL_RESULT_BUSY);

    /* After the nick change, "Alice {1}" is gone (slot 2 now renders "Jeff").
     * Slot 3 (other Alice) loses its disambig because only one "Alice"
     * principal is still rendered as principal. */
    {
        char buf[FL_NET_SERVER_DISPLAY_NAME_MAX];
        ASSERT(fl_net_server_member_display(&srv, (fl_net_server_member_id_t)2u,
                                            buf, sizeof(buf)) == FL_RESULT_OK);
        ASSERT(strcmp(buf, "Jeff") == 0);
    }

    /* Client A leaves -> client B should see a LEAVE_ANNOUNCE. */
    {
        int baseLeave = logB.leaves;
        fl_net_client_disconnect(&cA);
        pump(clients, logs, 2, 60);
        ASSERT(logB.leaves >= baseLeave + 1);
        ASSERT(strstr(logB.last_leave, "has left") != NULL);
    }

    fl_net_client_disconnect(&cB);
    /* Stop the background loop before tearing down the server so the loop
     * does not race the close on member sockets. */
    ASSERT(fl_server_bg_stop_server(bg) == FL_RESULT_OK);
    fl_net_server_host_stop(&srv);
    fl_net_sock_shutdown();
    return 0;
}

int main(void) {
    printf("test_p3_server: announce/join/leave/nick... ");
    fflush(stdout);
    if (test_announce_join_leave_nick() != 0)
        return 1;
    puts("ok");
    return 0;
}
