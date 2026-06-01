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

#include "contract_p3_session_wire.h"
#include "net_client.h"
#include "net_endian.h"
#include "net_ipv4.h"
#include "net_server.h"
#include "net_socket.h"
#include "server_bg.h"
#include "fl_colors.h"

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
    int privates;
    int errs;
    int closed;
    int host_promotes;   /* mid == own assigned_member_id */
    int host_redirects;  /* mid is some other peer's id */
    char last_join[160];
    char last_leave[160];
    char last_nick_announce[256];
    char last_server_announce[256];
    char last_msg[256];
    char last_private[256];
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
    case FL_NET_SERVER_EVENT_MSG_PRIVATE:
        log->privates++;
        snprintf(log->last_private, sizeof(log->last_private), "%s",
                 text ? text : "");
        break;
    case FL_NET_SERVER_EVENT_ERR:
        log->errs++;
        snprintf(log->last_err, sizeof(log->last_err), "%s", text ? text : "");
        break;
    case FL_NET_SERVER_EVENT_CLOSED:
        log->closed++;
        break;
    case FL_NET_SERVER_EVENT_HOST_PROMOTE:
        log->host_promotes++;
        break;
    case FL_NET_SERVER_EVENT_HOST_REDIRECT:
        log->host_redirects++;
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
    uint32_t loopback = fl_net_htonl(0x7F000001u); /* 127.0.0.1 */
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

/* ------------------------------------------------------------------------- */
/* Private + public messages, roster snapshot, leave-restores-singleton      */
/* ------------------------------------------------------------------------- */

static int test_messages_and_roster(void) {
    fl_net_server_t srv;
    fl_net_client_t cJack, cJill;
    event_log_t logJack = {0}, logJill = {0};
    fl_net_client_t *clients[2] = { &cJack, &cJill };
    event_log_t *logs[2] = { &logJack, &logJill };
    fl_server_bg_t *bg = NULL;
    uint32_t loopback = fl_net_htonl(0x7F000001u);
    const uint16_t port = 49811u;
    fl_result_t rc;
    fl_net_server_member_id_t jill_id, jack_id;

    fl_color_set_disabled(1);

    rc = fl_net_server_host_start(&srv, loopback, port, "HostUser");
    if (rc == FL_RESULT_NOSYS) {
        fprintf(stderr, "skip: hosted sockets unavailable\n");
        return 0;
    }
    ASSERT(rc == FL_RESULT_OK);
    ASSERT(fl_server_bg_start_server(&srv, &bg) == FL_RESULT_OK);

    fl_net_client_init(&cJack);
    fl_net_client_init(&cJill);

    ASSERT(fl_net_client_connect(&cJack, loopback, port, "Jack", 2000u) ==
           FL_RESULT_OK);
    ASSERT(fl_net_client_connect(&cJill, loopback, port, "Jill", 2000u) ==
           FL_RESULT_OK);
    pump(clients, logs, 2, 30);

    /* Both clients should have received a member-list snapshot. */
    ASSERT(fl_net_client_member_count(&cJack) >= 3);
    ASSERT(fl_net_client_member_count(&cJill) >= 3);

    /* Jack -> Jill private message. */
    jill_id = fl_net_client_member_lookup(&cJack, "Jill", 0);
    ASSERT(jill_id != FL_NET_SERVER_MEMBER_ID_NONE);
    {
        int jill_priv_before = logJill.privates;
        int jack_msg_before = logJack.msgs;
        int jack_priv_before = logJack.privates;
        ASSERT(fl_net_client_send_private(&cJack, jill_id, "Hello") == FL_RESULT_OK);
        pump(clients, logs, 2, 30);

        /* Recipient receives one MSG_PRIVATE with "Hello"; sender stays
         * quiet on both public and private channels. */
        ASSERT(logJill.privates == jill_priv_before + 1);
        ASSERT(strstr(logJill.last_private, "Hello") != NULL);
        ASSERT(strstr(logJack.last_msg, "Hello") == NULL);
        ASSERT(strstr(logJill.last_msg, "Hello") == NULL);
        ASSERT(logJack.msgs == jack_msg_before);
        ASSERT(logJack.privates == jack_priv_before);

        jack_id = fl_net_client_member_lookup(&cJill, "Jack", 0);
        ASSERT(jack_id != FL_NET_SERVER_MEMBER_ID_NONE);
        {
            char disp[FL_NET_SERVER_DISPLAY_NAME_MAX] = {0};
            (void)fl_net_client_member_display(&cJill, jack_id, disp, sizeof(disp));
            ASSERT(strcmp(disp, "Jack") == 0);
        }
    }

    /* Jack -> All: Jill receives, Jack does not (server skips sender). */
    {
        int jack_msg_before = logJack.msgs;
        ASSERT(fl_net_client_send_msg(&cJack, "Hi everyone") == FL_RESULT_OK);
        pump(clients, logs, 2, 30);
        ASSERT(strstr(logJill.last_msg, "Jack: Hi everyone") != NULL);
        ASSERT(logJack.msgs == jack_msg_before); /* no echo back to sender */
    }

    /* Local-only nick: Jack nicks Jill to "Sis". */
    ASSERT(fl_net_client_set_local_nick(&cJack, jill_id, "Sis") == FL_RESULT_OK);
    {
        char disp[FL_NET_SERVER_DISPLAY_NAME_MAX] = {0};
        ASSERT(fl_net_client_member_display(&cJack, jill_id, disp,
                                            sizeof(disp)) == FL_RESULT_OK);
        ASSERT(strcmp(disp, "Sis") == 0);
    }
    /* Host-global nick should override local-only. Host nicks Jill to "Boss". */
    ASSERT(fl_net_server_set_host_nick(&srv, jill_id, "Boss") == FL_RESULT_OK);
    pump(clients, logs, 2, 30);
    {
        char disp[FL_NET_SERVER_DISPLAY_NAME_MAX] = {0};
        ASSERT(fl_net_client_member_display(&cJack, jill_id, disp,
                                            sizeof(disp)) == FL_RESULT_OK);
        ASSERT(strcmp(disp, "Boss") == 0); /* host-global wins over local "Sis" */
    }

    /* Nick is session-only: client.principal (the shell login at connect
     * time) is never touched. Backs `server leave; whoami` showing the
     * shell username, not the nick. */
    ASSERT(strcmp(cJill.principal, "Jill") == 0);

    fl_net_client_disconnect(&cJack);
    fl_net_client_disconnect(&cJill);
    ASSERT(fl_server_bg_stop_server(bg) == FL_RESULT_OK);
    fl_net_server_host_stop(&srv);
    fl_net_sock_shutdown();
    return 0;
}

/* When a member with a `{N}` disambiguation leaves, the remaining singleton
 * should lose its `{N}` suffix (recompute_disambig) — the user's "original
 * name is maintained" rule. */
static int test_leave_restores_singleton(void) {
    fl_net_server_t srv;
    fl_net_client_t cA, cB;
    event_log_t logA = {0}, logB = {0};
    fl_net_client_t *clients[2] = { &cA, &cB };
    event_log_t *logs[2] = { &logA, &logB };
    fl_server_bg_t *bg = NULL;
    uint32_t loopback = fl_net_htonl(0x7F000001u);
    const uint16_t port = 49821u;
    fl_result_t rc;

    fl_color_set_disabled(1);

    rc = fl_net_server_host_start(&srv, loopback, port, "HostUser");
    if (rc == FL_RESULT_NOSYS)
        return 0;
    ASSERT(rc == FL_RESULT_OK);
    ASSERT(fl_server_bg_start_server(&srv, &bg) == FL_RESULT_OK);

    fl_net_client_init(&cA);
    fl_net_client_init(&cB);
    ASSERT(fl_net_client_connect(&cA, loopback, port, "Flinstone", 2000u) == FL_RESULT_OK);
    ASSERT(fl_net_client_connect(&cB, loopback, port, "Flinstone", 2000u) == FL_RESULT_OK);
    pump(clients, logs, 2, 30);

    /* Now we have two "Flinstone" members → both render with {N}. */
    {
        char disp_a[FL_NET_SERVER_DISPLAY_NAME_MAX] = {0};
        char disp_b[FL_NET_SERVER_DISPLAY_NAME_MAX] = {0};
        fl_net_server_member_display(&srv, (fl_net_server_member_id_t)2u,
                                     disp_a, sizeof(disp_a));
        fl_net_server_member_display(&srv, (fl_net_server_member_id_t)3u,
                                     disp_b, sizeof(disp_b));
        ASSERT(strcmp(disp_a, "Flinstone {1}") == 0);
        ASSERT(strcmp(disp_b, "Flinstone {2}") == 0);
    }

    /* Client A leaves. After dispatch, B should now render as plain "Flinstone". */
    fl_net_client_disconnect(&cA);
    pump(clients, logs, 2, 50);
    {
        char disp[FL_NET_SERVER_DISPLAY_NAME_MAX] = {0};
        ASSERT(fl_net_server_member_display(&srv,
                                            (fl_net_server_member_id_t)3u,
                                            disp, sizeof(disp)) == FL_RESULT_OK);
        ASSERT(strcmp(disp, "Flinstone") == 0); /* singleton restored */
    }

    fl_net_client_disconnect(&cB);
    ASSERT(fl_server_bg_stop_server(bg) == FL_RESULT_OK);
    fl_net_server_host_stop(&srv);
    fl_net_sock_shutdown();
    return 0;
}

/* Host transfer: when the host stops via transfer_and_stop, the chosen
 * successor id matches the lowest non-host member_id and is broadcast in
 * the OP_CTRL_HOST_PROMOTE payload. */
static int test_host_transfer_on_leave(void) {
    fl_net_server_t srv;
    fl_net_client_t cA, cB, cC;
    event_log_t logA = {0}, logB = {0}, logC = {0};
    fl_net_client_t *clients[3] = { &cA, &cB, &cC };
    event_log_t *logs[3] = { &logA, &logB, &logC };
    fl_server_bg_t *bg = NULL;
    uint32_t loopback = fl_net_htonl(0x7F000001u);
    const uint16_t port = 49831u;
    fl_net_server_member_id_t new_host = FL_NET_SERVER_MEMBER_ID_NONE;
    fl_result_t rc;

    fl_color_set_disabled(1);

    rc = fl_net_server_host_start(&srv, loopback, port, "HostUser");
    if (rc == FL_RESULT_NOSYS)
        return 0;
    ASSERT(rc == FL_RESULT_OK);
    ASSERT(fl_server_bg_start_server(&srv, &bg) == FL_RESULT_OK);

    fl_net_client_init(&cA);
    fl_net_client_init(&cB);
    fl_net_client_init(&cC);
    ASSERT(fl_net_client_connect(&cA, loopback, port, "A", 2000u) == FL_RESULT_OK);
    ASSERT(fl_net_client_connect(&cB, loopback, port, "B", 2000u) == FL_RESULT_OK);
    ASSERT(fl_net_client_connect(&cC, loopback, port, "C", 2000u) == FL_RESULT_OK);
    pump(clients, logs, 3, 30);

    /* Stop bg loop, then transfer & stop. */
    ASSERT(fl_server_bg_stop_server(bg) == FL_RESULT_OK);
    rc = fl_net_server_transfer_and_stop(&srv, &new_host);
    ASSERT(rc == FL_RESULT_OK);
    /* Lowest non-host member_id is 2 (cA). */
    ASSERT(new_host == (fl_net_server_member_id_t)2u);

    /* Drain the client receive paths so OP_CTRL_HOST_PROMOTE delivery
     * is verified, not assumed. cA (the chosen successor) must observe
     * HOST_PROMOTE; cB and cC must observe HOST_REDIRECT (the net_client
     * dispatcher splits them by mid==assigned_member_id). The
     * "[Server Announcement]: <display> is now the host" line counts as
     * a server_announce on every recipient. */
    pump(clients, logs, 3, 60);
    ASSERT(logA.host_promotes >= 1);
    ASSERT(logB.host_redirects >= 1);
    ASSERT(logC.host_redirects >= 1);
    ASSERT(logA.server_announces >= 1);
    ASSERT(logB.server_announces >= 1);
    ASSERT(logC.server_announces >= 1);

    fl_net_client_disconnect(&cA);
    fl_net_client_disconnect(&cB);
    fl_net_client_disconnect(&cC);
    fl_net_sock_shutdown();
    return 0;
}

/* #284 regression: ensure CTRL_HOST_PROMOTE serializes peer_ip_be the
 * same way on LE and BE hosts. Asserts (a) put/get_u32_nbo round-trip
 * preserves bits and (b) the wire byte order matches the IPv4 dotted
 * quad regardless of host endianness (we know the test runs on the
 * dev box's host endianness, but the assertion captures the contract:
 * the wire bytes must be the network-order bytes of `s_addr`). */
static int test_endian_host_promote_payload(void) {
    uint8_t wire[4] = {0};
    uint32_t round_trip = 0u;
    /* 192.168.10.2 in host order (0xC0A80A02), then converted to the
     * project's NBO scalar via the endian helper so the test is
     * portable across LE and BE hosts (the previous hand-assembled
     * little-endian literal broke on BE). */
    uint32_t ip_nbo = fl_net_htonl(0xC0A80A02U);
    /* On both LE and BE hosts, the memory layout of `ip_nbo` after the
     * htonl above is [192,168,10,2] (network byte order). memcpy
     * serialization must preserve that. */
    fl_net_put_u32_nbo(wire, ip_nbo);
    ASSERT(wire[0] == 192u);
    ASSERT(wire[1] == 168u);
    ASSERT(wire[2] == 10u);
    ASSERT(wire[3] == 2u);
    round_trip = fl_net_get_u32_nbo(wire);
    ASSERT(round_trip == ip_nbo);
    return 0;
}

static int test_endpoint_brackets(void) {
    uint32_t ip = 0u;
    uint16_t port = 0u;
    ASSERT(fl_net_endpoint_parse_v4("127.0.0.1:49913", &ip, &port));
    ASSERT(port == 49913u);
    ASSERT(fl_net_endpoint_parse_v4("[127.0.0.1]:49913", &ip, &port));
    ASSERT(port == 49913u);
    ASSERT(fl_net_endpoint_parse_v4("[::1]:49913", &ip, &port));
    ASSERT(port == 49913u);
    ASSERT(fl_net_ipv4_is_loopback(ip));
    return 0;
}

static int test_host_promote6_v4_mapped(void) {
    uint8_t payload[FL_NET_SESSION_CTRL_HOST_PROMOTE6_PAYLOAD_LEN];
    uint32_t ip = 0u;
    uint32_t expect = fl_net_htonl(0xC0A80A02U);
    memset(payload, 0, sizeof(payload));
    fl_net_put_u16_be(payload, 2u);
    fl_net_put_u32_nbo(payload + 2 + 12, expect);
    payload[2 + 10] = 0xffu;
    payload[2 + 11] = 0xffu;
    fl_net_put_u16_be(payload + 18, 50123u);
    ASSERT(fl_net_ipv6_wire_to_v4(payload + 2, &ip));
    ASSERT(ip == expect);
    return 0;
}

int main(void) {
    printf("test_p3_server: bracketed endpoint parse (#280)... ");
    fflush(stdout);
    if (test_endpoint_brackets() != 0)
        return 1;
    puts("ok");

    printf("test_p3_server: OP_CTRL_HOST_PROMOTE6 v4-mapped (#283)... ");
    fflush(stdout);
    if (test_host_promote6_v4_mapped() != 0)
        return 1;
    puts("ok");

    printf("test_p3_server: endian round-trip for OP_CTRL_HOST_PROMOTE... ");
    fflush(stdout);
    if (test_endian_host_promote_payload() != 0)
        return 1;
    puts("ok");

    printf("test_p3_server: announce/join/leave/nick... ");
    fflush(stdout);
    if (test_announce_join_leave_nick() != 0)
        return 1;
    puts("ok");

    printf("test_p3_server: private+public messages + local nicks... ");
    fflush(stdout);
    if (test_messages_and_roster() != 0)
        return 1;
    puts("ok");

    printf("test_p3_server: leave restores singleton principal... ");
    fflush(stdout);
    if (test_leave_restores_singleton() != 0)
        return 1;
    puts("ok");

    printf("test_p3_server: host transfer picks lowest non-host id... ");
    fflush(stdout);
    if (test_host_transfer_on_leave() != 0)
        return 1;
    puts("ok");
    return 0;
}
