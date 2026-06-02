/*
 * `server` shell built-in: host / join / msg / announce / nick / set-nick /
 * connected / leave / kill verbs. See docs/SERVER.md for the user surface
 * and the colour palette (KRED / KGRN / KYEL / KBLU / KCYN).
 */

#include "cmd_decl.h"
#include "cmd_batch.h"
#include "cmd_server_file.h"
#include "contract_p3_session_wire.h"
#include "fl_colors.h"
#include "net_client.h"
#include "net_endian.h"
#include "net_endpoint.h"
#include "net_ipv4.h"
#include "net_server.h"
#include "server_bg.h"
#include "server_shared_db.h"
#include "session.h"
#include "shell_io.h"

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* ------------------------------------------------------------------------- */
/* Process-wide session state                                                */
/* ------------------------------------------------------------------------- */

static pthread_mutex_t session_mutex = PTHREAD_MUTEX_INITIALIZER;

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

/* Public accessor so cmd_exit (and any other shell teardown path) can
 * cleanly drop the session before the process dies. */
void cmd_server_atexit(void);

/* ------------------------------------------------------------------------- */
/* Helpers                                                                   */
/* ------------------------------------------------------------------------- */

static int parse_endpoint_full(const char *s, fl_net_endpoint_t *out) {
    return fl_net_endpoint_parse(s, out) ? 0 : -1;
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

static void join_argv_into(char *dst, size_t cap, int argc, char **argv, int from) {
    size_t off = 0;
    dst[0] = '\0';
    for (int i = from; i < argc; i++) {
        size_t need = strnlen(argv[i], cap);
        if (off > 0u && off + 1u < cap) {
            dst[off++] = ' ';
            dst[off] = '\0';
        }
        if (off + need >= cap)
            need = cap - 1u - off;
        if (need > 0u) {
            memcpy(dst + off, argv[i], need);
            off += need;
            dst[off] = '\0';
        }
    }
}

/* ------------------------------------------------------------------------- */
/* Client-side event sink (called from the background pthread)               */
/* ------------------------------------------------------------------------- */

/* Forward declarations referenced by the client event sink. */
static fl_result_t start_client_bg(void);
static void spawn_promote_thread(int am_new_host, const fl_net_endpoint_t *new_host);
static void maybe_handle_nick_prompt_sync(void);

static void client_event_print(fl_net_server_event_kind_t kind, const char *text,
                               fl_net_server_member_id_t mid,
                               const fl_net_addr_t *host_addr, void *data) {
    (void)data;
    switch (kind) {
    case FL_NET_SERVER_EVENT_JOIN_ANNOUNCE:
    case FL_NET_SERVER_EVENT_LEAVE_ANNOUNCE:
    case FL_NET_SERVER_EVENT_NICK_SET_ANNOUNCE:
    case FL_NET_SERVER_EVENT_SERVER_ANNOUNCE:
        fl_color_announce("%s", text);
        break;
    case FL_NET_SERVER_EVENT_NICK_PROMPT:
        /* verb_join handles the sync prompt; only surfaces here after a
         * post-reconnect collision where BG can't read stdin. */
        fl_color_server_dm(
            "Your principal '%s' collides with another connected user. "
            "Run 'server set-nick <nick>' to choose a nickname.",
            current_principal());
        break;
    case FL_NET_SERVER_EVENT_MSG: {
        /* Public chat is prefixed "Sender: text" by the host; split back. */
        const char *colon = strchr(text, ':');
        if (colon && colon > text && colon[1] == ' ') {
            char sender[FL_NET_SERVER_DISPLAY_NAME_MAX];
            size_t slen = (size_t)(colon - text);
            if (slen >= sizeof(sender)) slen = sizeof(sender) - 1u;
            memcpy(sender, text, slen);
            sender[slen] = '\0';
            fl_color_msg_from_all(sender, "%s", colon + 2);
        } else {
            fl_color_msg_from_all("?", "%s", text);
        }
        break;
    }
    case FL_NET_SERVER_EVENT_MSG_PRIVATE: {
        char sender[FL_NET_SERVER_DISPLAY_NAME_MAX];
        if (fl_net_client_member_display(&g_client, mid, sender,
                                         sizeof(sender)) != FL_RESULT_OK)
            snprintf(sender, sizeof(sender), "member %u", (unsigned)mid);
        fl_color_msg_from_user(sender, "%s", text);
        break;
    }
    case FL_NET_SERVER_EVENT_FILE_OFFER:
        if (text[0]) {
            int use_color = fl_color_is_enabled_for(stdout);
            if (fl_color_prelude_hook)
                fl_color_prelude_hook();
            if (use_color)
                fputs(KCYN, stdout);
            fputs(text, stdout);
            if (use_color)
                fputs(KNRM, stdout);
            fputc('\n', stdout);
            fflush(stdout);
            if (fl_color_postlude_hook)
                fl_color_postlude_hook();
        }
        break;
    case FL_NET_SERVER_EVENT_MEMBER_LIST:
        /* Silent: we already cached the roster in net_client. */
        break;
    case FL_NET_SERVER_EVENT_ERR:
        fl_color_error("%s", text);
        break;
    case FL_NET_SERVER_EVENT_CLOSED:
        fl_color_warn("session closed by host");
        break;
    case FL_NET_SERVER_EVENT_HOST_PROMOTE:
    case FL_NET_SERVER_EVENT_HOST_REDIRECT: {
        int am_new_host = (kind == FL_NET_SERVER_EVENT_HOST_PROMOTE) ? 1 : 0;
        (void)mid;
        (void)text;
        if (!host_addr) {
            fl_color_warn("host promote received with no address; leaving");
            return;
        }
        spawn_promote_thread(am_new_host, (const fl_net_endpoint_t *)host_addr);
        break;
    }
    case FL_NET_SERVER_EVENT_HELLO_ACK:
    case FL_NET_SERVER_EVENT_NONE:
    default:
        break;
    }
}

/* When the host closes the session, the BG client loop exits on its own
 * but g_client_bg still points at the thread handle. pthread_join on a
 * returned thread is well-defined, so call it here before the next join
 * looks at the pointer and skips starting a new loop. */
static void reap_client_bg_if_dead(void) {
    if (g_client_bg &&
        fl_net_client_state(&g_client) == FL_NET_CLIENT_STATE_DISCONNECTED) {
        fl_server_bg_stop_client(g_client_bg);
        g_client_bg = NULL;
    }
}

static fl_result_t start_client_bg(void) {
    reap_client_bg_if_dead();
    if (g_client_bg)
        return FL_RESULT_OK;
    return fl_server_bg_start_client(&g_client, client_event_print, NULL,
                                     &g_client_bg);
}

/* ------------------------------------------------------------------------- */
/* Host transfer transition (spawned as a short-lived detached thread when   */
/* OP_CTRL_HOST_PROMOTE arrives on the client bg loop)                       */
/* ------------------------------------------------------------------------- */

typedef struct {
    int am_new_host;
    fl_net_endpoint_t new_host;
    fl_net_endpoint_t local;
    char principal[FL_NET_SERVER_PRINCIPAL_MAX];
} promote_args_t;

static void *promote_thread_main(void *arg) {
    promote_args_t *pa = (promote_args_t *)arg;
    struct timespec ts = { 0, 80 * 1000 * 1000 }; /* 80ms settle window */
    fl_result_t rc;

    nanosleep(&ts, NULL);

    pthread_mutex_lock(&session_mutex);
    if (g_client_bg) {
        fl_server_bg_stop_client(g_client_bg);
        g_client_bg = NULL;
    }
    fl_net_client_disconnect(&g_client);
    pthread_mutex_unlock(&session_mutex);

    if (pa->am_new_host) {
        fl_net_endpoint_t bind_ep = pa->local;
        bind_ep.port_host = pa->new_host.port_host;
        rc = fl_net_server_host_start_ep(&g_server, &bind_ep, pa->principal);
        pthread_mutex_lock(&session_mutex);
        if (rc != FL_RESULT_OK) {
            pthread_mutex_unlock(&session_mutex);
            fl_color_error("host takeover bind failed (rc=%d); please run "
                           "'server host <ip:port>' manually", (int)rc);
            free(pa);
            return NULL;
        }
        rc = fl_server_bg_start_server(&g_server, &g_server_bg);
        if (rc != FL_RESULT_OK) {
            fl_net_server_host_stop(&g_server);
            pthread_mutex_unlock(&session_mutex);
            fl_color_error("host takeover bg start failed (rc=%d)", (int)rc);
            free(pa);
            return NULL;
        }
        g_server_running = 1;
        (void)fl_server_catalog_open();
        pthread_mutex_unlock(&session_mutex);
    } else {
        fl_net_endpoint_t local = pa->local;
        fl_net_endpoint_t new_host = pa->new_host;
        const fl_net_endpoint_t *local_ptr =
            (local.family != 0u) ? &local : NULL;
        char principal[FL_NET_SERVER_PRINCIPAL_MAX];
        strncpy(principal, pa->principal, sizeof(principal) - 1u);
        principal[sizeof(principal) - 1u] = '\0';

        rc = FL_RESULT_ERR;
        for (int attempt = 0; attempt < 10; attempt++) {
            rc = fl_net_client_connect_ep(&g_client, local_ptr, &new_host,
                                          principal, 1000u);
            if (rc == FL_RESULT_OK)
                break;
            {
                struct timespec rs = { 0, 150 * 1000 * 1000 }; /* 150ms */
                nanosleep(&rs, NULL);
            }
        }
        pthread_mutex_lock(&session_mutex);
        if (rc != FL_RESULT_OK) {
            pthread_mutex_unlock(&session_mutex);
            fl_color_error("reconnect to new host failed (rc=%d); run "
                           "'server join <ip:port>' manually", (int)rc);
            free(pa);
            return NULL;
        }
        {
            fl_result_t bg_rc = start_client_bg();
            if (bg_rc != FL_RESULT_OK) {
                fl_net_client_disconnect(&g_client);
                pthread_mutex_unlock(&session_mutex);
                fl_color_error("reconnect bg start failed (rc=%d); run "
                               "'server join <ip:port>' manually",
                               (int)bg_rc);
                free(pa);
                return NULL;
            }
        }
        fl_color_success("reconnected to new host on member_id %u",
                         (unsigned)g_client.assigned_member_id);
        pthread_mutex_unlock(&session_mutex);
    }
    free(pa);
    return NULL;
}

static void spawn_promote_thread(int am_new_host, const fl_net_endpoint_t *new_host) {
    pthread_t tid;
    promote_args_t *pa = (promote_args_t *)calloc(1, sizeof(*pa));
    if (!pa || !new_host)
        return;
    pa->am_new_host = am_new_host;
    pa->new_host = *new_host;
    pa->local = g_client.local_ep;
    strncpy(pa->principal, current_principal(), sizeof(pa->principal) - 1u);
    if (pthread_create(&tid, NULL, promote_thread_main, pa) != 0) {
        free(pa);
        return;
    }
    pthread_detach(tid);
}

/* ------------------------------------------------------------------------- */
/* Interactive Y/N nick prompt on join (synchronous; runs while raw_mode
 * is OFF in interpreter.c). Returns FL_RESULT_OK after the optional nick
 * has been sent, or when no prompt arrived. */
/* ------------------------------------------------------------------------- */

static int read_yn_default_n(void) {
    char buf[16];
    if (!fgets(buf, sizeof(buf), stdin))
        return 0;
    return (buf[0] == 'Y' || buf[0] == 'y') ? 1 : 0;
}

static int read_nick_line(char *out, size_t cap) {
    if (!fgets(out, (int)cap, stdin))
        return -1;
    /* strip trailing newline + spaces */
    size_t n = strlen(out);
    while (n > 0u && (out[n - 1] == '\n' || out[n - 1] == '\r' ||
                       out[n - 1] == ' ' || out[n - 1] == '\t')) {
        out[--n] = '\0';
    }
    return n > 0u ? 0 : -1;
}

static void maybe_handle_nick_prompt_sync(void) {
    /* Drain non-blocking until NICK_PROMPT or quiet. Non-prompt frames go
     * through fl_net_client_dispatch_frame so the host's initial
     * MEMBER_LIST_SNAPSHOT / JOIN_ANNOUNCE survive into the BG loop. */
    for (int i = 0; i < 50; i++) {
        uint8_t opcode = 0;
        uint8_t payload[FL_NET_SESSION_MAX_MSG];
        uint16_t plen = 0;
        fl_result_t rc;
        struct timespec ts = { 0, 20 * 1000 * 1000 }; /* 20ms */
        nanosleep(&ts, NULL);
        rc = fl_net_session_recv_frame_nb(g_client.peer_handle,
                                          &g_client.rx_state,
                                          &opcode, payload, sizeof(payload),
                                          &plen);
        if (rc == FL_RESULT_TIMEDOUT)
            continue;
        if (rc != FL_RESULT_OK)
            return;
        if (opcode == (uint8_t)FL_NET_SESSION_OP_NICK_PROMPT) {
            char nick[FL_NET_SERVER_NICK_MAX];
            /* Lock stdout for the whole Y/N + nick read so no async
             * announce can interleave between prompt and answer. */
            fl_shell_io_lock();
            fl_color_prompt_yellow(
                "Your username %s is already in use by another connected user, "
                "would you want to be nicked [Y/N]? ",
                current_principal());
            if (!read_yn_default_n()) {
                fl_shell_io_unlock();
                fl_color_success("keeping disambiguated principal");
                return;
            }
            fl_color_prompt_yellow("Please enter the nickname you wish to use: ");
            if (read_nick_line(nick, sizeof(nick)) != 0 || nick[0] == '\0') {
                fl_shell_io_unlock();
                fl_color_warn("empty nick — keeping disambiguated principal");
                return;
            }
            fl_shell_io_unlock();
            if (fl_net_client_set_nick(&g_client, nick) == FL_RESULT_OK)
                fl_color_success("nick requested: '%s'", nick);
            else
                fl_color_error("nick request '%s' rejected", nick);
            return;
        }
        /* Feed non-prompt frames through normal dispatch so cached roster
         * + announcements survive even though this drain consumed them. */
        (void)fl_net_client_dispatch_frame(&g_client, opcode, payload, plen,
                                            client_event_print, NULL);
    }
}

/* ------------------------------------------------------------------------- */
/* Verb handlers                                                             */
/* ------------------------------------------------------------------------- */

/* `server host <ip:port>` — bind the listener, insert host as member 1,
 * spawn the bg accept/poll loop. Fails if a join is already in flight or
 * if hosted sockets are unavailable on this build (FL_RESULT_NOSYS). */
static int verb_host(int argc, char **argv) {
    fl_net_endpoint_t ep;
    fl_result_t rc;

    if (argc < 3) {
        fl_color_error("usage: server host <ip:port>");
        return 1;
    }
    pthread_mutex_lock(&session_mutex);
    if (g_server_running) {
        pthread_mutex_unlock(&session_mutex);
        fl_color_error("server already hosting");
        return 1;
    }
    if (fl_net_client_state(&g_client) == FL_NET_CLIENT_STATE_CONNECTED) {
        pthread_mutex_unlock(&session_mutex);
        fl_color_error("already joined a server; leave first");
        return 1;
    }
    if (parse_endpoint_full(argv[2], &ep) != 0) {
        pthread_mutex_unlock(&session_mutex);
        fl_color_error("invalid ip:port '%s'", argv[2]);
        return 1;
    }
    rc = fl_net_server_host_start_ep(&g_server, &ep, current_principal());
    if (rc == FL_RESULT_NOSYS) {
        pthread_mutex_unlock(&session_mutex);
        fl_color_error("hosted sockets unavailable; cannot host");
        return 1;
    }
    if (rc != FL_RESULT_OK) {
        pthread_mutex_unlock(&session_mutex);
        fl_color_error("server host failed (rc=%d); is the port already bound?",
                       (int)rc);
        return 1;
    }
    rc = fl_server_bg_start_server(&g_server, &g_server_bg);
    if (rc != FL_RESULT_OK) {
        fl_net_server_host_stop(&g_server);
        pthread_mutex_unlock(&session_mutex);
        fl_color_error("server background start failed (rc=%d)", (int)rc);
        return 1;
    }
    g_server_running = 1;
    pthread_mutex_unlock(&session_mutex);
    {
        char bind_txt[128];
        if (fl_net_endpoint_format(&ep, bind_txt, sizeof(bind_txt)))
            fl_color_success("hosting as '%s' on %s", current_principal(), bind_txt);
        else
            fl_color_success("hosting as '%s' on %s", current_principal(), argv[2]);
    }
    return 0;
}

/* `server join <ip:port> [-bind <local_ip>]` — connect, synchronously
 * run the optional Y/N nick prompt (so the prompt and the user's typed
 * answer never race with a background announce), then hand off to the
 * bg receive loop. `-bind` selects the local source IP for multi-IP /
 * netns lab setups. */
static int verb_join(int argc, char **argv) {
    fl_net_endpoint_t peer;
    fl_net_endpoint_t local;
    fl_result_t rc;

    if (argc < 3) {
        fl_color_error("usage: server join <ip:port> [-bind <local_ip>]");
        return 1;
    }
    memset(&local, 0, sizeof(local));
    pthread_mutex_lock(&session_mutex);
    /* Reap any dead BG handle from a host-closed prior session BEFORE
     * the "already joined" check so a stale pointer cannot block re-join. */
    reap_client_bg_if_dead();
    if (g_server_running) {
        pthread_mutex_unlock(&session_mutex);
        fl_color_error("already hosting; kill first to join a different server");
        return 1;
    }
    if (fl_net_client_state(&g_client) == FL_NET_CLIENT_STATE_CONNECTED) {
        pthread_mutex_unlock(&session_mutex);
        fl_color_error("already joined; leave first");
        return 1;
    }
    if (parse_endpoint_full(argv[2], &peer) != 0) {
        fl_color_error("invalid ip:port '%s'", argv[2]);
        return 1;
    }
    /* Optional -bind <local_ip> for multi-IP demos / tests (IPv4 or [IPv6]). */
    for (int i = 3; i + 1 < argc; i++) {
        if (!strcmp(argv[i], "-bind")) {
            if (!fl_net_endpoint_parse(argv[i + 1], &local)) {
                pthread_mutex_unlock(&session_mutex);
                fl_color_error("invalid -bind address '%s'", argv[i + 1]);
                return 1;
            }
            i++;
        }
    }
    rc = fl_net_client_connect_ep(&g_client, local.family ? &local : NULL, &peer,
                                  current_principal(), 3000u);
    if (rc == FL_RESULT_NOSYS) {
        pthread_mutex_unlock(&session_mutex);
        fl_color_error("hosted sockets unavailable; cannot join");
        return 1;
    }
    if (rc != FL_RESULT_OK) {
        pthread_mutex_unlock(&session_mutex);
        fl_color_error("server join failed (rc=%d)", (int)rc);
        return 1;
    }
    {
        char peer_txt[128];
        if (fl_net_endpoint_format(&peer, peer_txt, sizeof(peer_txt)))
            fl_color_success("joined '%s' as '%s' (member_id %u)", peer_txt,
                             g_client.display_name, (unsigned)g_client.assigned_member_id);
        else
            fl_color_success("joined as '%s' (member_id %u)", g_client.display_name,
                             (unsigned)g_client.assigned_member_id);
    }
    /* Synchronously handle the NICK_PROMPT (if any) BEFORE we start the
     * background loop so the Y/N dialogue is clean. */
    maybe_handle_nick_prompt_sync();
    rc = start_client_bg();
    if (rc != FL_RESULT_OK) {
        /* Surfacing this prevents a "connected but deaf" session where
         * the user sees the join success line but never receives any
         * messages. Tear the half-open client back down. */
        fl_net_client_disconnect(&g_client);
        pthread_mutex_unlock(&session_mutex);
        fl_color_error("server join: background receive start failed (rc=%d)",
                       (int)rc);
        return 1;
    }
    pthread_mutex_unlock(&session_mutex);
    return 0;
}

/* `server leave` is dual-role: from a host shell it triggers transfer-
 * and-stop (the lowest non-host id is promoted and a blue announce
 * tells everyone); from a joined shell it simply disconnects. */
static int verb_leave(void) {
    pthread_mutex_lock(&session_mutex);
    if (g_server_running) {
        /* Host leaving: transfer + stop. */
        fl_net_server_member_id_t new_host = FL_NET_SERVER_MEMBER_ID_NONE;
        if (g_server_bg) {
            fl_server_bg_stop_server(g_server_bg);
            g_server_bg = NULL;
        }
        (void)fl_net_server_transfer_and_stop(&g_server, &new_host);
        g_server_running = 0;
        pthread_mutex_unlock(&session_mutex);
        if (new_host == FL_NET_SERVER_MEMBER_ID_NONE) {
            fl_color_success("session terminated (no remaining members)");
        }
        /* When transfer DID happen, fl_net_server_transfer_and_stop already
         * broadcast and locally echoed "[Server Announcement]: <display> is
         * now the host" — no extra local line needed. */
        return 0;
    }
    if (fl_net_client_state(&g_client) != FL_NET_CLIENT_STATE_CONNECTED) {
        pthread_mutex_unlock(&session_mutex);
        fl_color_error("not currently joined");
        return 1;
    }
    if (g_client_bg) {
        fl_server_bg_stop_client(g_client_bg);
        g_client_bg = NULL;
    }
    fl_net_client_disconnect(&g_client);
    pthread_mutex_unlock(&session_mutex);
    fl_color_success("left session");
    return 0;
}

static int verb_kill(void) {
    pthread_mutex_lock(&session_mutex);
    if (!g_server_running) {
        pthread_mutex_unlock(&session_mutex);
        fl_color_error("not hosting; nothing to kill");
        return 1;
    }
    if (g_server_bg) {
        fl_server_bg_stop_server(g_server_bg);
        g_server_bg = NULL;
    }
    fl_net_server_host_stop(&g_server);
    g_server_running = 0;
    pthread_mutex_unlock(&session_mutex);
    fl_color_success("session terminated");
    return 0;
}

/* `server msg [-all | -user <name> [-id <N>] | -id <N>] <text...>`
 * Routes through one of the public (MSG_BROADCAST) or private
 * (MSG_DIRECT) wire paths. Sender renders nothing on success; the
 * server skips the sender on fan-out so there is no echo to suppress. */
static int verb_msg(int argc, char **argv) {
    /* server msg [-all] <text...>
     * server msg -user <name> [-id <N>] <text...>
     * server msg -id <N> <text...>
     */
    int i;
    const char *target_name = NULL;
    unsigned target_disambig = 0;
    int is_private = 0;
    int explicit_all = 0;
    int first_text = 2;
    char joined[FL_NET_SESSION_MAX_MSG];

    if (argc < 3) {
        fl_color_error("usage: server msg [-all | -user <name> [-id <N>] | -id <N>] <text...>");
        return 1;
    }
    /* Parse leading -flags. */
    i = 2;
    while (i < argc && argv[i][0] == '-') {
        if (!strcmp(argv[i], "-all")) {
            explicit_all = 1;
            i++;
        } else if (!strcmp(argv[i], "-user") && i + 1 < argc) {
            target_name = argv[i + 1];
            is_private = 1;
            i += 2;
        } else if (!strcmp(argv[i], "-id") && i + 1 < argc) {
            target_disambig = (unsigned)atoi(argv[i + 1]);
            is_private = 1;
            i += 2;
        } else {
            break;
        }
    }
    first_text = i;
    if (first_text >= argc) {
        fl_color_error("server msg: missing message text");
        return 1;
    }
    if (explicit_all && is_private) {
        fl_color_error("server msg: -all and -user/-id are mutually exclusive");
        return 1;
    }
    join_argv_into(joined, sizeof(joined), argc, argv, first_text);
    if (joined[0] == '\0') {
        fl_color_error("server msg: empty text");
        return 1;
    }

    pthread_mutex_lock(&session_mutex);
    if (!g_server_running &&
        fl_net_client_state(&g_client) != FL_NET_CLIENT_STATE_CONNECTED) {
        pthread_mutex_unlock(&session_mutex);
        fl_color_error("not in a session; host or join first");
        return 1;
    }

    if (!is_private) {
        /* No local echo: server skips the sender on broadcast and the
         * user already typed the text. Silence = success; errors print. */
        if (g_server_running) {
            if (fl_net_server_send_public(&g_server, joined) != FL_RESULT_OK) {
                pthread_mutex_unlock(&session_mutex);
                fl_color_error("server msg: broadcast failed");
                return 1;
            }
            pthread_mutex_unlock(&session_mutex);
            return 0;
        }
        if (fl_net_client_send_msg(&g_client, joined) != FL_RESULT_OK) {
            pthread_mutex_unlock(&session_mutex);
            fl_color_error("server msg: send failed");
            return 1;
        }
        pthread_mutex_unlock(&session_mutex);
        return 0;
    }

    /* Private message — resolve recipient_id, render local confirmation. */
    {
        fl_net_server_member_id_t recipient = FL_NET_SERVER_MEMBER_ID_NONE;
        char recipient_display[FL_NET_SERVER_DISPLAY_NAME_MAX] = {0};

        if (target_name && !g_server_running) {
            recipient = fl_net_client_member_lookup(&g_client, target_name,
                                                    target_disambig);
        } else if (target_name && g_server_running) {
            /* Host side: walk own member registry. */
            for (size_t k = 0; k < fl_net_server_member_count(&g_server); k++) {
                const fl_net_server_member_t *m = fl_net_server_member_at(&g_server, k);
                if (!m || m->is_host) continue;
                if ((m->nick[0] && strcmp(m->nick, target_name) == 0) ||
                    (target_disambig == 0u &&
                     strcmp(m->principal, target_name) == 0) ||
                    (target_disambig != 0u &&
                     m->disambig_index == (uint8_t)target_disambig &&
                     strcmp(m->principal, target_name) == 0)) {
                    recipient = m->member_id;
                    break;
                }
            }
        } else if (!target_name) {
            /* -id only path: numeric member id. */
            recipient = (fl_net_server_member_id_t)target_disambig;
        }
        if (recipient == FL_NET_SERVER_MEMBER_ID_NONE) {
            pthread_mutex_unlock(&session_mutex);
            fl_color_error("no such recipient '%s' (use -id <N> to disambiguate)",
                           target_name ? target_name : "?");
            return 1;
        }

        if (g_server_running) {
            (void)fl_net_server_member_display(&g_server, recipient,
                                               recipient_display,
                                               sizeof(recipient_display));
            if (fl_net_server_send_private(&g_server, recipient, joined) != FL_RESULT_OK) {
                pthread_mutex_unlock(&session_mutex);
                fl_color_error("server msg: deliver failed");
                return 1;
            }
        } else {
            (void)fl_net_client_member_display(&g_client, recipient,
                                               recipient_display,
                                               sizeof(recipient_display));
            if (fl_net_client_send_private(&g_client, recipient, joined) != FL_RESULT_OK) {
                pthread_mutex_unlock(&session_mutex);
                fl_color_error("server msg: send failed");
                return 1;
            }
        }
        /* Recipient renders "From X -> You"; sender stays silent on success. */
        (void)recipient_display;
    }
    pthread_mutex_unlock(&session_mutex);
    return 0;
}

static int verb_announce(int argc, char **argv) {
    char joined[FL_NET_SERVER_ANNOUNCEMENT_MAX];
    if (!g_server_running) {
        fl_color_error("server announce: host only");
        return 1;
    }
    if (argc < 3) {
        fl_color_error("usage: server announce <text...>");
        return 1;
    }
    join_argv_into(joined, sizeof(joined), argc, argv, 2);
    if (joined[0] == '\0') {
        fl_color_error("server announce: empty text");
        return 1;
    }
    if (fl_net_server_announce(&g_server, "%s", joined) != FL_RESULT_OK) {
        fl_color_error("server announce: broadcast failed");
        return 1;
    }
    return 0;
}

/* `server set-nick <name>` -- joined user requests a host-global nick. The
 * host validates it the same way it validates a host-driven nick (no
 * collision with any other principal or nick) and broadcasts the
 * announcement on success. */
static int verb_set_nick(int argc, char **argv) {
    if (fl_net_client_state(&g_client) != FL_NET_CLIENT_STATE_CONNECTED) {
        fl_color_error("server set-nick: not currently joined");
        return 1;
    }
    if (argc < 3 || !argv[2] || !argv[2][0]) {
        fl_color_error("usage: server set-nick <nick>");
        return 1;
    }
    if (fl_net_client_set_nick(&g_client, argv[2]) != FL_RESULT_OK) {
        fl_color_error("server set-nick: invalid nick '%s'", argv[2]);
        return 1;
    }
    fl_color_success("nick requested: '%s'", argv[2]);
    return 0;
}

static int verb_nick(int argc, char **argv) {
    fl_net_server_member_id_t target = FL_NET_SERVER_MEMBER_ID_NONE;
    const char *new_nick = NULL;
    int is_local = 0;
    int i;

    for (i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "-local"))
            is_local = 1;
        else if (!strcmp(argv[i], "-id") && i + 1 < argc)
            target = parse_member_id_arg(argv[++i]);
        else if (!strcmp(argv[i], "-name") && i + 1 < argc)
            new_nick = argv[++i];
    }
    if (target == FL_NET_SERVER_MEMBER_ID_NONE || !new_nick) {
        fl_color_error("usage: server nick [-local] -id <member_id> -name <nick>");
        return 1;
    }
    if (is_local) {
        if (fl_net_client_state(&g_client) != FL_NET_CLIENT_STATE_CONNECTED) {
            fl_color_error("server nick -local: not currently joined");
            return 1;
        }
        if (fl_net_client_set_local_nick(&g_client, target, new_nick) != FL_RESULT_OK) {
            fl_color_error("server nick -local: unknown member_id %u", (unsigned)target);
            return 1;
        }
        fl_color_success("local nick on member_id %u set to '%s'",
                         (unsigned)target, new_nick);
        return 0;
    }
    if (!g_server_running) {
        fl_color_error("server nick: host only (pass -local for client-side override)");
        return 1;
    }
    {
        fl_result_t rc = fl_net_server_set_host_nick(&g_server, target, new_nick);
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
    }
    fl_color_success("nick set on member_id %u", (unsigned)target);
    return 0;
}

/* `server connected` — roster. Host reads the live registry; client
 * reads the cached OP_MEMBER_LIST_SNAPSHOT. Anyone can run it. */
static int verb_connected(void) {
    pthread_mutex_lock(&session_mutex);
    if (g_server_running) {
        char disp[FL_NET_SERVER_DISPLAY_NAME_MAX];
        size_t count = fl_net_server_member_count(&g_server);
        pthread_mutex_unlock(&session_mutex);
        fl_shell_io_lock();
        for (size_t k = 0; k < count; k++) {
            const fl_net_server_member_t *m = fl_net_server_member_at(&g_server, k);
            if (!m) continue;
            char peer_txt[128];
            fl_net_server_member_display(&g_server, m->member_id, disp, sizeof(disp));
            if (m->in_use && m->peer_addr.family &&
                fl_net_endpoint_format(&m->peer_addr, peer_txt, sizeof(peer_txt)))
                printf("[%u] %s  %s%s\n", (unsigned)m->member_id, disp, peer_txt,
                       m->is_host ? " <- host" : "");
            else
                printf("[%u] %s%s\n", (unsigned)m->member_id, disp,
                       m->is_host ? " <- host" : "");
        }
        fflush(stdout);
        fl_shell_io_unlock();
        return 0;
    }
    if (fl_net_client_state(&g_client) != FL_NET_CLIENT_STATE_CONNECTED) {
        pthread_mutex_unlock(&session_mutex);
        fl_color_error("server connected: not currently in a session");
        return 1;
    }
    /* Client view from the cached member-list snapshot. */
    {
        size_t count = fl_net_client_member_count(&g_client);
        char disp[FL_NET_SERVER_DISPLAY_NAME_MAX];
        pthread_mutex_unlock(&session_mutex);
        fl_shell_io_lock();
        for (size_t k = 0; k < count; k++) {
            const fl_net_client_member_t *m = fl_net_client_member_at(&g_client, k);
            if (!m) continue;
            (void)fl_net_client_member_display(&g_client, m->member_id, disp,
                                               sizeof(disp));
            printf("[%u] %s%s%s\n", (unsigned)m->member_id, disp,
                   m->is_host ? " <- host" : "",
                   m->member_id == g_client.assigned_member_id ? " (you)" : "");
        }
        fflush(stdout);
        fl_shell_io_unlock();
    }
    return 0;
}

/* ------------------------------------------------------------------------- */
/* Public exit hook (called by cmd_exit before the shell tears down)         */
/* ------------------------------------------------------------------------- */

void cmd_server_atexit(void) {
    pthread_mutex_lock(&session_mutex);
    if (g_server_running) {
        fl_net_server_member_id_t new_host = FL_NET_SERVER_MEMBER_ID_NONE;
        if (g_server_bg) {
            fl_server_bg_stop_server(g_server_bg);
            g_server_bg = NULL;
        }
        (void)fl_net_server_transfer_and_stop(&g_server, &new_host);
        g_server_running = 0;
        pthread_mutex_unlock(&session_mutex);
        return;
    }
    if (fl_net_client_state(&g_client) == FL_NET_CLIENT_STATE_CONNECTED) {
        if (g_client_bg) {
            fl_server_bg_stop_client(g_client_bg);
            g_client_bg = NULL;
        }
        fl_net_client_disconnect(&g_client);
    }
    pthread_mutex_unlock(&session_mutex);
}

/* ------------------------------------------------------------------------- */
/* Command surface                                                           */
/* ------------------------------------------------------------------------- */

int cmd_server_run(int argc, char **argv) {
    cmd_server_ctx_t ctx = {
        .mutex = &session_mutex,
        .server = &g_server,
        .server_bg = &g_server_bg,
        .server_running = &g_server_running,
        .client = &g_client,
        .client_bg = &g_client_bg,
    };

    if (argc < 2) {
        fl_color_error("usage: server <host|join|msg|file|send|announce|nick|set-nick|connected|leave|kill> ...");
        return 1;
    }
    if (!strcmp(argv[1], "host"))      return verb_host(argc, argv);
    if (!strcmp(argv[1], "join"))      return verb_join(argc, argv);
    if (!strcmp(argv[1], "leave"))     return verb_leave();
    if (!strcmp(argv[1], "kill"))      return verb_kill();
    if (!strcmp(argv[1], "msg"))       return verb_msg(argc, argv);
    if (!strcmp(argv[1], "file"))      return cmd_server_file_run(&ctx, argc, argv);
    if (!strcmp(argv[1], "send"))      return cmd_server_send_file_alias(&ctx, argc, argv);
    if (!strcmp(argv[1], "announce"))  return verb_announce(argc, argv);
    if (!strcmp(argv[1], "nick"))      return verb_nick(argc, argv);
    if (!strcmp(argv[1], "set-nick"))  return verb_set_nick(argc, argv);
    if (!strcmp(argv[1], "connected")) return verb_connected();
    fl_color_error("unknown server verb '%s'", argv[1]);
    return 1;
}

__attribute__((used))
int cmd_server_batch_tokens_count(int argc, char **argv, int i) {
    int used = 1;
    int j = i + 1;
    (void)argv;
    while (j < argc) {
        used++;
        j++;
    }
    return used;
}
