/*
 * `server` shell built-in (foundations).
 *
 * Verbs implemented in this train:
 *
 *   server host <ip:port>            -- bind listener + start background loop
 *   server join <ip:port> [nick]     -- connect; optional nick request
 *   server msg <text>                -- broadcast chat (any member)
 *   server announce <text>           -- host-only; blue "[Server Announcement]"
 *   server nick -id <n> -name <nick> -- host-only global nick reassignment
 *   server connected                 -- print roster (host side)
 *   server leave                     -- client disconnect
 *   server kill                      -- host-only; tear down session
 *
 * Output palette (see userland/shell/fl_colors.h):
 *   RED  -> "[ERROR] ..."                  -- usage and authz errors
 *   GRN  -> "[Server] ..."                 -- local success acknowledgements
 *   BLU  -> "[Server Announcement] ..."    -- host/peer-pushed announcements
 *
 * State model: a process holds exactly **one** of
 *   - a live host server (g_server_running == 1), or
 *   - a live joined client (g_client.state == CONNECTED).
 * Verbs that need one side error out clearly when the other side is active.
 */

#include "cmd_decl.h"
#include "cmd_batch.h"
#include "fl_colors.h"
#include "net_client.h"
#include "net_server.h"
#include "net_ipv4.h"
#include "server_bg.h"
#include "session.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------------- */
/* Process-wide session state                                                */
/* ------------------------------------------------------------------------- */

static fl_net_server_t g_server;
static fl_server_bg_t *g_server_bg;
static int g_server_running;

static fl_net_client_t g_client;
static fl_server_bg_t *g_client_bg;

static const char *current_principal(void) {
    const char *u = fl_session_current_user();
    if (u && u[0])
        return u;
    return "Flinstone";
}

/* ------------------------------------------------------------------------- */
/* Helpers                                                                   */
/* ------------------------------------------------------------------------- */

/* Parse "ip:port" into (addr_be, port_host). IPv4 literal only (v1). */
static int parse_endpoint(const char *s, uint32_t *addr_be_out, uint16_t *port_out) {
    char buf[64];
    char *colon;
    long port;
    size_t n;
    char *end;

    if (!s || !addr_be_out || !port_out)
        return -1;
    n = strnlen(s, sizeof(buf));
    if (n == 0u || n >= sizeof(buf))
        return -1;
    memcpy(buf, s, n);
    buf[n] = '\0';
    colon = strrchr(buf, ':');
    if (!colon || colon == buf)
        return -1;
    *colon = '\0';

    if (!fl_net_ipv4_parse_literal(buf, addr_be_out))
        return -1;

    errno = 0;
    port = strtol(colon + 1, &end, 10);
    if (errno != 0 || end == colon + 1 || (end && *end != '\0'))
        return -1;
    if (port <= 0 || port > 65535)
        return -1;
    *port_out = (uint16_t)port;
    return 0;
}

static fl_net_server_member_id_t parse_member_id_arg(const char *s) {
    long v;
    char *end = NULL;
    if (!s || !s[0])
        return FL_NET_SERVER_MEMBER_ID_NONE;
    errno = 0;
    v = strtol(s, &end, 10);
    if (errno != 0 || end == s || (end && *end != '\0'))
        return FL_NET_SERVER_MEMBER_ID_NONE;
    if (v <= 0 || v > 65535)
        return FL_NET_SERVER_MEMBER_ID_NONE;
    return (fl_net_server_member_id_t)v;
}

/* ------------------------------------------------------------------------- */
/* Client-side event sink                                                    */
/* ------------------------------------------------------------------------- */

static void client_event_print(fl_net_server_event_kind_t kind, const char *text,
                               fl_net_server_member_id_t mid, void *data) {
    (void)mid;
    (void)data;
    switch (kind) {
    case FL_NET_SERVER_EVENT_JOIN_ANNOUNCE:
    case FL_NET_SERVER_EVENT_LEAVE_ANNOUNCE:
    case FL_NET_SERVER_EVENT_NICK_SET_ANNOUNCE:
    case FL_NET_SERVER_EVENT_SERVER_ANNOUNCE:
        fl_color_announce("%s", text);
        break;
    case FL_NET_SERVER_EVENT_NICK_PROMPT:
        fl_color_warn("nick prompt: %s", text);
        break;
    case FL_NET_SERVER_EVENT_MSG:
        /* Chat lines render plain so the shell does not colour user content. */
        printf("%s\n", text);
        fflush(stdout);
        break;
    case FL_NET_SERVER_EVENT_ERR:
        fl_color_error("%s", text);
        break;
    case FL_NET_SERVER_EVENT_CLOSED:
        fl_color_warn("session closed by host");
        break;
    case FL_NET_SERVER_EVENT_HELLO_ACK:
    case FL_NET_SERVER_EVENT_NONE:
    default:
        break;
    }
}

/* ------------------------------------------------------------------------- */
/* Verb handlers                                                             */
/* ------------------------------------------------------------------------- */

static int verb_host(int argc, char **argv) {
    uint32_t addr_be = 0;
    uint16_t port = 0;
    fl_result_t rc;

    if (argc < 3) {
        fl_color_error("usage: server host <ip:port>");
        return 1;
    }
    if (g_server_running) {
        fl_color_error("server already hosting");
        return 1;
    }
    if (fl_net_client_state(&g_client) == FL_NET_CLIENT_STATE_CONNECTED) {
        fl_color_error("already joined a server; leave first");
        return 1;
    }
    if (parse_endpoint(argv[2], &addr_be, &port) != 0) {
        fl_color_error("invalid ip:port '%s'", argv[2]);
        return 1;
    }
    rc = fl_net_server_host_start(&g_server, addr_be, port, current_principal());
    if (rc == FL_RESULT_NOSYS) {
        fl_color_error("hosted sockets unavailable; cannot host");
        return 1;
    }
    if (rc != FL_RESULT_OK) {
        fl_color_error("server host failed (rc=%d)", (int)rc);
        return 1;
    }
    rc = fl_server_bg_start_server(&g_server, &g_server_bg);
    if (rc != FL_RESULT_OK) {
        fl_net_server_host_stop(&g_server);
        fl_color_error("server background start failed (rc=%d)", (int)rc);
        return 1;
    }
    g_server_running = 1;
    fl_color_success("hosting as '%s' on %s", current_principal(), argv[2]);
    return 0;
}

static int verb_join(int argc, char **argv) {
    uint32_t addr_be = 0;
    uint16_t port = 0;
    fl_result_t rc;
    const char *nick = NULL;

    if (argc < 3) {
        fl_color_error("usage: server join <ip:port> [nick]");
        return 1;
    }
    if (g_server_running) {
        fl_color_error("already hosting; kill first to join a different server");
        return 1;
    }
    if (fl_net_client_state(&g_client) == FL_NET_CLIENT_STATE_CONNECTED) {
        fl_color_error("already joined; leave first");
        return 1;
    }
    if (parse_endpoint(argv[2], &addr_be, &port) != 0) {
        fl_color_error("invalid ip:port '%s'", argv[2]);
        return 1;
    }
    if (argc >= 4 && argv[3] && argv[3][0])
        nick = argv[3];

    rc = fl_net_client_connect(&g_client, addr_be, port, current_principal(), 3000u);
    if (rc == FL_RESULT_NOSYS) {
        fl_color_error("hosted sockets unavailable; cannot join");
        return 1;
    }
    if (rc != FL_RESULT_OK) {
        fl_color_error("server join failed (rc=%d)", (int)rc);
        return 1;
    }
    rc = fl_server_bg_start_client(&g_client, client_event_print, NULL, &g_client_bg);
    if (rc != FL_RESULT_OK) {
        fl_net_client_disconnect(&g_client);
        fl_color_error("client background start failed (rc=%d)", (int)rc);
        return 1;
    }
    fl_color_success("joined as '%s' (member_id %u)", g_client.display_name,
                     (unsigned)g_client.assigned_member_id);
    if (nick) {
        if (fl_net_client_set_nick(&g_client, nick) == FL_RESULT_OK)
            fl_color_success("requested nick '%s'", nick);
        else
            fl_color_error("nick request '%s' rejected", nick);
    }
    return 0;
}

static int verb_leave(void) {
    if (fl_net_client_state(&g_client) != FL_NET_CLIENT_STATE_CONNECTED) {
        fl_color_error("not currently joined");
        return 1;
    }
    if (g_client_bg) {
        fl_server_bg_stop_client(g_client_bg);
        g_client_bg = NULL;
    }
    fl_net_client_disconnect(&g_client);
    fl_color_success("left session");
    return 0;
}

static int verb_kill(void) {
    if (!g_server_running) {
        fl_color_error("not hosting; nothing to kill");
        return 1;
    }
    if (g_server_bg) {
        fl_server_bg_stop_server(g_server_bg);
        g_server_bg = NULL;
    }
    fl_net_server_host_stop(&g_server);
    g_server_running = 0;
    fl_color_success("session terminated");
    return 0;
}

static int verb_msg(int argc, char **argv) {
    char joined[FL_NET_SESSION_MAX_MSG];
    size_t off = 0;
    int i;
    fl_result_t rc;

    if (argc < 3) {
        fl_color_error("usage: server msg <text...>");
        return 1;
    }
    if (fl_net_client_state(&g_client) != FL_NET_CLIENT_STATE_CONNECTED &&
        !g_server_running) {
        fl_color_error("not in a session; host or join first");
        return 1;
    }
    /* Join argv[2..] with single spaces. */
    joined[0] = '\0';
    for (i = 2; i < argc; i++) {
        size_t need = strnlen(argv[i], sizeof(joined));
        if (off > 0u && off + 1u < sizeof(joined)) {
            joined[off++] = ' ';
            joined[off] = '\0';
        }
        if (off + need >= sizeof(joined))
            need = sizeof(joined) - 1u - off;
        if (need > 0u) {
            memcpy(joined + off, argv[i], need);
            off += need;
            joined[off] = '\0';
        }
    }
    if (off == 0u) {
        fl_color_error("server msg: empty text");
        return 1;
    }

    if (g_server_running) {
        /* Host: broadcast as "Host: text" via OP_MSG_BROADCAST. */
        char line[FL_NET_SESSION_MAX_MSG];
        char disp[FL_NET_SERVER_DISPLAY_NAME_MAX];
        int n;
        fl_net_server_member_display(&g_server, FL_NET_SERVER_MEMBER_ID_HOST,
                                     disp, sizeof(disp));
        n = snprintf(line, sizeof(line), "%s: %s", disp, joined);
        if (n <= 0) {
            fl_color_error("server msg: encode failed");
            return 1;
        }
        if ((size_t)n >= sizeof(line))
            n = (int)sizeof(line) - 1;
        /* Reach into the server: send MSG_BROADCAST to every peer. */
        for (size_t k = 0; k < fl_net_server_member_count(&g_server); k++) {
            const fl_net_server_member_t *m = fl_net_server_member_at(&g_server, k);
            if (!m || m->is_host || m->peer_handle == FL_NET_SOCK_INVALID)
                continue;
            (void)fl_net_session_send_frame(m->peer_handle,
                                            (uint8_t)FL_NET_SESSION_OP_MSG_BROADCAST,
                                            (const uint8_t *)line, (uint16_t)n);
        }
        /* Local echo for the host. */
        printf("%s\n", line);
        fflush(stdout);
        return 0;
    }

    rc = fl_net_client_send_msg(&g_client, joined);
    if (rc != FL_RESULT_OK) {
        fl_color_error("server msg: send failed (rc=%d)", (int)rc);
        return 1;
    }
    return 0;
}

static int verb_announce(int argc, char **argv) {
    char joined[FL_NET_SERVER_ANNOUNCEMENT_MAX];
    size_t off = 0;
    int i;

    if (!g_server_running) {
        fl_color_error("server announce: host only");
        return 1;
    }
    if (argc < 3) {
        fl_color_error("usage: server announce <text...>");
        return 1;
    }
    joined[0] = '\0';
    for (i = 2; i < argc; i++) {
        size_t need = strnlen(argv[i], sizeof(joined));
        if (off > 0u && off + 1u < sizeof(joined)) {
            joined[off++] = ' ';
            joined[off] = '\0';
        }
        if (off + need >= sizeof(joined))
            need = sizeof(joined) - 1u - off;
        if (need > 0u) {
            memcpy(joined + off, argv[i], need);
            off += need;
            joined[off] = '\0';
        }
    }
    if (off == 0u) {
        fl_color_error("server announce: empty text");
        return 1;
    }
    if (fl_net_server_announce(&g_server, "%s", joined) != FL_RESULT_OK) {
        fl_color_error("server announce: broadcast failed");
        return 1;
    }
    return 0;
}

static int verb_nick(int argc, char **argv) {
    fl_net_server_member_id_t target = FL_NET_SERVER_MEMBER_ID_NONE;
    const char *new_nick = NULL;
    int i;
    fl_result_t rc;

    if (!g_server_running) {
        fl_color_error("server nick: host only");
        return 1;
    }
    for (i = 2; i + 1 < argc; i++) {
        if (!strcmp(argv[i], "-id"))
            target = parse_member_id_arg(argv[++i]);
        else if (!strcmp(argv[i], "-name"))
            new_nick = argv[++i];
    }
    if (target == FL_NET_SERVER_MEMBER_ID_NONE || !new_nick) {
        fl_color_error("usage: server nick -id <member_id> -name <nick>");
        return 1;
    }
    rc = fl_net_server_set_host_nick(&g_server, target, new_nick);
    if (rc == FL_RESULT_BUSY) {
        fl_color_error("nickname '%s' is taken or matches another member's username",
                       new_nick);
        return 1;
    }
    if (rc == FL_RESULT_NOENT) {
        fl_color_error("no such member_id %u", (unsigned)target);
        return 1;
    }
    if (rc != FL_RESULT_OK) {
        fl_color_error("server nick: rejected (rc=%d)", (int)rc);
        return 1;
    }
    fl_color_success("nick set on member_id %u", (unsigned)target);
    return 0;
}

static int verb_connected(void) {
    char disp[FL_NET_SERVER_DISPLAY_NAME_MAX];
    size_t count;

    if (!g_server_running) {
        fl_color_error("server connected: host only (run on host)");
        return 1;
    }
    count = fl_net_server_member_count(&g_server);
    for (size_t k = 0; k < count; k++) {
        const fl_net_server_member_t *m = fl_net_server_member_at(&g_server, k);
        if (!m)
            continue;
        fl_net_server_member_display(&g_server, m->member_id, disp, sizeof(disp));
        printf("[%u] %s%s\n", (unsigned)m->member_id, disp,
               m->is_host ? " <- host" : "");
    }
    fflush(stdout);
    return 0;
}

/* ------------------------------------------------------------------------- */
/* Command surface                                                           */
/* ------------------------------------------------------------------------- */

int cmd_server_run(int argc, char **argv) {
    if (argc < 2) {
        fl_color_error("usage: server <host|join|msg|announce|nick|connected|leave|kill> ...");
        return 1;
    }
    if (!strcmp(argv[1], "host"))
        return verb_host(argc, argv);
    if (!strcmp(argv[1], "join"))
        return verb_join(argc, argv);
    if (!strcmp(argv[1], "leave"))
        return verb_leave();
    if (!strcmp(argv[1], "kill"))
        return verb_kill();
    if (!strcmp(argv[1], "msg"))
        return verb_msg(argc, argv);
    if (!strcmp(argv[1], "announce"))
        return verb_announce(argc, argv);
    if (!strcmp(argv[1], "nick"))
        return verb_nick(argc, argv);
    if (!strcmp(argv[1], "connected"))
        return verb_connected();
    fl_color_error("unknown server verb '%s'", argv[1]);
    return 1;
}

__attribute__((used))
int cmd_server_batch_tokens_count(int argc, char **argv, int i) {
    int used = 1;
    int j = i + 1;
    /* Consume all positional args until a non-server token shape; cmd_server
     * verbs are 0..many trailing free-form tokens. We greedily eat the rest
     * of the batch slice. */
    (void)argv;
    while (j < argc) {
        used++;
        j++;
    }
    return used;
}
