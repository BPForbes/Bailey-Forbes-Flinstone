/*
 * tests/test_p3_server_lan.c
 *
 * Multi-IP loopback-range host/join (127.0.0.2 host, 127.0.0.3 client bind)
 * without extra ip addr setup. Proves non-127.0.0.1 LAN-style endpoints work.
 */

#include "net_client.h"
#include "net_endian.h"
#include "net_endpoint.h"
#include "net_ipv4.h"
#include "net_server.h"
#include "net_socket.h"
#include "server_bg.h"
#include "fl_colors.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define ASSERT(c)                                                              \
    do {                                                                       \
        if (!(c)) {                                                            \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);       \
            return 1;                                                          \
        }                                                                      \
    } while (0)

static uint16_t pick_port(void) {
    return (uint16_t)(48000u + (unsigned)(getpid() % 2000u));
}

static int test_multi_ip_host_join(void) {
    fl_net_server_t srv;
    fl_net_client_t client;
    fl_server_bg_t *srv_bg = NULL;
    fl_server_bg_t *cli_bg = NULL;
    uint32_t host_be;
    uint32_t client_be;
    uint16_t port = pick_port();
    fl_result_t rc;

    if (!fl_net_ipv4_parse_literal("127.0.0.2", &host_be))
        return 1;
    if (!fl_net_ipv4_parse_literal("127.0.0.3", &client_be))
        return 1;

    rc = fl_net_sock_init();
    if (rc == FL_RESULT_NOSYS) {
        puts("skip: hosted sockets unavailable");
        return 0;
    }
    ASSERT(rc == FL_RESULT_OK);

    rc = fl_net_server_host_start(&srv, host_be, port, "HostLan");
    if (rc != FL_RESULT_OK) {
        int e = fl_net_sock_last_errno();
        if (e != 0)
            fprintf(stderr, "host_start errno=%d\n", e);
        return 1;
    }
    ASSERT(fl_server_bg_start_server(&srv, &srv_bg) == FL_RESULT_OK);

    {
        fl_net_endpoint_t peer;
        fl_net_endpoint_t local;
        fl_net_endpoint_from_v4(host_be, port, &peer);
        fl_net_endpoint_from_v4(client_be, 0u, &local);
        rc = fl_net_client_connect_ep(&client, &local, &peer, "ClientLan", 3000u);
    }
    ASSERT(rc == FL_RESULT_OK);

    rc = fl_net_client_send_msg(&client, "hello from multi-ip lab");
    ASSERT(rc == FL_RESULT_OK);

    for (int i = 0; i < 40; i++) {
        (void)fl_net_server_accept_pending(&srv, NULL, 0u);
        (void)fl_net_server_poll_members(&srv);
        usleep(10000);
    }

    (void)fl_server_bg_stop_client(cli_bg);
    (void)fl_server_bg_stop_server(srv_bg);
    fl_net_client_disconnect(&client);
    fl_net_server_host_stop(&srv);
    return 0;
}

int main(void) {
    int fails = 0;
    printf("test_p3_server_lan: multi-ip host/join... ");
    fflush(stdout);
    if (test_multi_ip_host_join() != 0) {
        puts("FAIL");
        fails++;
    } else {
        puts("ok");
    }
    return fails ? 1 : 0;
}
