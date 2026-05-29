/*
 * `server` shell built-in.
 *
 * Verbs:
 *   server host <ip:port>                        -- bind listener + start bg
 *   server join <ip:port> [-bind <local_ip>]     -- connect; optional source bind
 *   server msg <text...>                          -- broadcast (global, CYN)
 *   server msg -all <text...>                     -- broadcast (explicit)
 *   server msg -user <name> [-id <N>] <text...>   -- private (CYN)
 *   server msg -id <N> <text...>                  -- private by id
 *   server announce <text...>                     -- host-only; blue announce
 *   server nick -id <N> -name <nick>              -- host-only global nick
 *   server nick -local -id <N> -name <local-nick> -- client-local nick
 *   server connected                              -- roster (anyone)
 *   server leave                                  -- client disconnect (or
 *                                                    host: transfer-and-stop)
 *   server kill                                   -- host-only; kill all peers
 *
 * Output palette (userland/shell/fl_colors.h):
 *   KRED  -> "[ERROR] ..."                  -- usage and authz errors
 *   KGRN  -> "[Server] ..."                 -- local success acknowledgements
 *   KYEL  -> warnings and interactive prompts (nick prompt)
 *   KBLU  -> "[Server Announcement]: ..."   -- host/peer-pushed announces
 *   KCYN  -> "[Server Message, ...]: ..."   -- public / private chat
 */

#include "cmd_decl.h"
#include "cmd_batch.h"
#include "fl_colors.h"
#include "net_client.h"
#include "net_server.h"
#include "net_ipv4.h"
#include "server_bg.h"
#include "session.h"
#include "shell_io.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

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

/* Public accessor so cmd_exit (and any other shell teardown path) can
 * cleanly drop the session before the process dies. */
void cmd_server_atexit(void);

/* ------------------------------------------------------------------------- */
/* Helpers                                                                   */
/* ------------------------------------------------------------------------- */

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
    /*
     * TODO(#280): IPv6 dual-stack will need bracketed-endpoint parsing
     *   `[2001:db8::1]:49913`
     * because an IPv6 literal contains colons that would otherwise
     * confuse the `strrchr(buf, ':')` port split below. The right shape
     * once the v6 stack lands:
     *   - if buf[0] == '['  -> find matching ']' then expect ":port"
     *     after it, parse the bracketed portion with `inet_pton(AF_INET6,
     *     ...)` into a `struct in6_addr`, and extend `*addr_be_out` to a
     *     family-tagged variant (see also OP_CTRL_HOST_PROMOTE item 11).
     *   - otherwise fall through to the current IPv4 path (this function).
     * Until #280 promotes from [DEFERRED], this stays v4-only and any v6
     * literal is rejected here by `fl_net_ipv4_parse_literal`.
     */
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
static void start_client_bg(void);
static void spawn_promote_thread(int am_new_host, uint32_t new_ip_be,
                                 uint16_t new_port);
static void maybe_handle_nick_prompt_sync(void);

static void client_event_print(fl_net_server_event_kind_t kind, const char *text,
                               fl_net_server_member_id_t mid, void *data) {
    (void)data;
    switch (kind) {
    case FL_NET_SERVER_EVENT_JOIN_ANNOUNCE:
    case FL_NET_SERVER_EVENT_LEAVE_ANNOUNCE:
    case FL_NET_SERVER_EVENT_NICK_SET_ANNOUNCE:
    case FL_NET_SERVER_EVENT_SERVER_ANNOUNCE:
        fl_color_announce("%s", text);
        break;
    case FL_NET_SERVER_EVENT_NICK_PROMPT:
        /* In normal flow we handle the prompt synchronously in verb_join
         * before starting the background loop. If it arrives later (e.g.
         * after an auto-reconnect on host transfer), surface it as a cyan
         * SERVER message — the user can run `server nick -name <nick>` on
         * their own schedule. */
        fl_color_server_dm(
            "Your principal '%s' collides with another connected user. "
            "Run 'server set-nick <nick>' to choose a nickname.",
            current_principal());
        break;
    case FL_NET_SERVER_EVENT_MSG: {
        /* Public chat — already prefixed "Sender: text" by the host. Split
         * it back into sender + text so we render with the CYN msg helper. */
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
    case FL_NET_SERVER_EVENT_MEMBER_LIST:
        /* Silent: we already cached the roster in net_client. */
        break;
    case FL_NET_SERVER_EVENT_ERR:
        fl_color_error("%s", text);
        break;
    case FL_NET_SERVER_EVENT_CLOSED:
        fl_color_warn("session closed by host");
        break;
    case FL_NET_SERVER_EVENT_HOST_PROMOTE: {
        /* Wire payload: [u16_be new_id][u32 new_ip_be][u16_be new_port].
         * No user-visible message here — the OLD host already broadcast a
         * blue "[Server Announcement]: <display> is now the host" line
         * (server-side SERVER_ANNOUNCE) right before sending this frame, so
         * every peer has been told. We just decode and spawn the takeover/
         * reconnect transition thread silently. */
        const uint8_t *p = (const uint8_t *)text;
        uint16_t new_id_be = mid;
        uint32_t new_ip_be;
        uint16_t new_port;
        if (!p) {
            fl_color_warn("host promote received with no payload; leaving");
            return;
        }
        new_ip_be = (uint32_t)p[2] | ((uint32_t)p[3] << 8) |
                    ((uint32_t)p[4] << 16) | ((uint32_t)p[5] << 24);
        new_port = (uint16_t)(((uint16_t)p[6] << 8) | p[7]);
        spawn_promote_thread(new_id_be == g_client.assigned_member_id ? 1 : 0,
                             new_ip_be, new_port);
        break;
    }
    case FL_NET_SERVER_EVENT_HOST_REDIRECT:
    case FL_NET_SERVER_EVENT_HELLO_ACK:
    case FL_NET_SERVER_EVENT_NONE:
    default:
        break;
    }
}

/*
 * The client BG loop exits on its own when the session is closed by the
 * host (CTRL_KILL / EOF). At that point `g_client_bg` still points at the
 * thread handle but the thread itself has returned; without this reap the
 * next `server join` would see the non-NULL pointer in start_client_bg()
 * and skip starting a new loop, leaving the user "joined" but never
 * receiving announcements (Codex P2 follow-up).
 *
 * pthread_join on an already-returned thread is well-defined — it returns
 * immediately and frees the kernel-side resources, which is exactly the
 * cleanup we want.
 */
static void reap_client_bg_if_dead(void) {
    if (g_client_bg &&
        fl_net_client_state(&g_client) == FL_NET_CLIENT_STATE_DISCONNECTED) {
        fl_server_bg_stop_client(g_client_bg);
        g_client_bg = NULL;
    }
}

static void start_client_bg(void) {
    reap_client_bg_if_dead();
    if (g_client_bg)
        return;
    (void)fl_server_bg_start_client(&g_client, client_event_print, NULL, &g_client_bg);
}

/* ------------------------------------------------------------------------- */
/* Host transfer transition (spawned as a short-lived detached thread when   */
/* OP_CTRL_HOST_PROMOTE arrives on the client bg loop)                       */
/* ------------------------------------------------------------------------- */

typedef struct {
    int    am_new_host;
    uint32_t new_ip_be;
    uint16_t new_port;
    uint32_t local_ip_be;
    char     principal[FL_NET_SERVER_PRINCIPAL_MAX];
} promote_args_t;

static void *promote_thread_main(void *arg) {
    promote_args_t *pa = (promote_args_t *)arg;
    struct timespec ts = { 0, 80 * 1000 * 1000 }; /* 80ms settle window */
    nanosleep(&ts, NULL);

    /* Drop the old client (we are NOT inside that bg loop). */
    if (g_client_bg) {
        fl_server_bg_stop_client(g_client_bg);
        g_client_bg = NULL;
    }
    fl_net_client_disconnect(&g_client);

    if (pa->am_new_host) {
        fl_result_t rc = fl_net_server_host_start(&g_server, pa->local_ip_be,
                                                  pa->new_port, pa->principal);
        if (rc != FL_RESULT_OK) {
            fl_color_error("host takeover bind failed (rc=%d); please run "
                           "'server host <ip:port>' manually", (int)rc);
            free(pa);
            return NULL;
        }
        rc = fl_server_bg_start_server(&g_server, &g_server_bg);
        if (rc != FL_RESULT_OK) {
            fl_net_server_host_stop(&g_server);
            fl_color_error("host takeover bg start failed (rc=%d)", (int)rc);
            free(pa);
            return NULL;
        }
        g_server_running = 1;
        /* No local "you are now the host" message here: the OLD host's
         * `fl_net_server_transfer_and_stop()` already broadcast a blue
         * "[Server Announcement]: <display> is now the host" to every peer
         * (including this one) before tearing down, so the user has already
         * seen the same line as everyone else. The promotion mechanics are
         * mute by design. New host always reindexes to member_id 1 by virtue
         * of starting a fresh `fl_net_server_host_start()`; reconnecting
         * peers get sequential ids from 2 upward (stack-style for the auto
         * path; the future "host selects successor" path will extend the
         * map instead, but is not in this train). */
    } else {
        /* Re-join the new host. Use our same local IP as source so the new
         * host can see us coming from a consistent address. The new host's
         * listener may still be coming up — retry a few times. */
        fl_result_t rc = FL_RESULT_ERR;
        for (int attempt = 0; attempt < 10; attempt++) {
            rc = fl_net_client_connect_from(&g_client, pa->local_ip_be,
                                            pa->new_ip_be, pa->new_port,
                                            pa->principal, 1000u);
            if (rc == FL_RESULT_OK)
                break;
            {
                struct timespec rs = { 0, 150 * 1000 * 1000 }; /* 150ms */
                nanosleep(&rs, NULL);
            }
        }
        if (rc != FL_RESULT_OK) {
            fl_color_error("reconnect to new host failed (rc=%d); run "
                           "'server join <ip:port>' manually", (int)rc);
            free(pa);
            return NULL;
        }
        /* Do NOT run the synchronous Y/N nick prompt from this background
         * thread — the interpreter's foreground read loop is also reading
         * from stdin, so two threads would race for the user's input. If a
         * NICK_PROMPT frame arrives, the client-event sink surfaces it as a
         * cyan SERVER message and the user can react with `server nick` on
         * their own schedule. */
        start_client_bg();
        fl_color_success("reconnected to new host on member_id %u",
                         (unsigned)g_client.assigned_member_id);
    }
    free(pa);
    return NULL;
}

static void spawn_promote_thread(int am_new_host, uint32_t new_ip_be,
                                 uint16_t new_port) {
    pthread_t tid;
    promote_args_t *pa = (promote_args_t *)calloc(1, sizeof(*pa));
    if (!pa)
        return;
    pa->am_new_host = am_new_host;
    pa->new_ip_be = new_ip_be;
    pa->new_port = new_port;
    pa->local_ip_be = g_client.local_ip_be;
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
    /* Drain frames non-blocking until we either see NICK_PROMPT (handled
     * inline) or there is nothing pending. Any non-prompt frame received
     * during this window is dispatched through the same path the BG loop
     * would use (`fl_net_client_dispatch_frame`) so the host's initial
     * MEMBER_LIST_SNAPSHOT / JOIN_ANNOUNCE / SERVER_ANNOUNCE are preserved
     * and `server connected` + `server msg -user` work immediately after
     * the join completes (Codex P2 follow-up). */
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
            /* Take the shell stdout lock for the entire Y/N + nick sequence
             * so background announcements (from any future bg client) cannot
             * interleave between the prompt and the user's typed answer.
             * The interpreter's prompt_active is already 0 here (we are
             * inside submit_single_command), so async helpers will not try
             * to redraw "shell> " on top of our yellow prompt. */
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
        /* Non-prompt frame: feed it through the normal client dispatch so
         * the cached roster snapshot, announcements, and any other
         * background-loop side effects survive even when this drain
         * consumed the frame before the BG loop got to it. */
        (void)fl_net_client_dispatch_frame(&g_client, opcode, payload, plen,
                                            client_event_print, NULL);
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
        fl_color_error("server host failed (rc=%d); is the port already bound?",
                       (int)rc);
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
    uint32_t peer_be = 0;
    uint16_t port = 0;
    uint32_t local_be = 0;
    fl_result_t rc;

    if (argc < 3) {
        fl_color_error("usage: server join <ip:port> [-bind <local_ip>]");
        return 1;
    }
    /* Reap any background loop left from a previous session that the host
     * closed on its side (the thread has already exited but the handle
     * was never freed). Must happen BEFORE the "already joined" check so
     * a stale handle does not block a legitimate re-join. */
    reap_client_bg_if_dead();
    if (g_server_running) {
        fl_color_error("already hosting; kill first to join a different server");
        return 1;
    }
    if (fl_net_client_state(&g_client) == FL_NET_CLIENT_STATE_CONNECTED) {
        fl_color_error("already joined; leave first");
        return 1;
    }
    if (parse_endpoint(argv[2], &peer_be, &port) != 0) {
        fl_color_error("invalid ip:port '%s'", argv[2]);
        return 1;
    }
    /* Optional -bind <local_ip> for multi-IP demos / tests. */
    for (int i = 3; i + 1 < argc; i++) {
        if (!strcmp(argv[i], "-bind")) {
            if (!fl_net_ipv4_parse_literal(argv[i + 1], &local_be)) {
                fl_color_error("invalid -bind ip '%s'", argv[i + 1]);
                return 1;
            }
            i++;
        }
    }
    rc = fl_net_client_connect_from(&g_client, local_be, peer_be, port,
                                    current_principal(), 3000u);
    if (rc == FL_RESULT_NOSYS) {
        fl_color_error("hosted sockets unavailable; cannot join");
        return 1;
    }
    if (rc != FL_RESULT_OK) {
        fl_color_error("server join failed (rc=%d)", (int)rc);
        return 1;
    }
    fl_color_success("joined as '%s' (member_id %u)", g_client.display_name,
                     (unsigned)g_client.assigned_member_id);
    /* Synchronously handle the NICK_PROMPT (if any) BEFORE we start the
     * background loop so the Y/N dialogue is clean. */
    maybe_handle_nick_prompt_sync();
    start_client_bg();
    return 0;
}

static int verb_leave(void) {
    if (g_server_running) {
        /* Host leaving: transfer + stop. */
        fl_net_server_member_id_t new_host = FL_NET_SERVER_MEMBER_ID_NONE;
        if (g_server_bg) {
            fl_server_bg_stop_server(g_server_bg);
            g_server_bg = NULL;
        }
        (void)fl_net_server_transfer_and_stop(&g_server, &new_host);
        g_server_running = 0;
        if (new_host == FL_NET_SERVER_MEMBER_ID_NONE) {
            fl_color_success("session terminated (no remaining members)");
        }
        /* When transfer DID happen, fl_net_server_transfer_and_stop already
         * broadcast and locally echoed "[Server Announcement]: <display> is
         * now the host" — no extra local line needed. */
        return 0;
    }
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

    if (!g_server_running &&
        fl_net_client_state(&g_client) != FL_NET_CLIENT_STATE_CONNECTED) {
        fl_color_error("not in a session; host or join first");
        return 1;
    }

    if (!is_private) {
        /* Global broadcast. No local "You -> All" echo is rendered: the
         * server does not echo our own message back to us (see issue
         * feedback) and the user already typed the text themselves, so
         * silence is success. Errors still surface as [ERROR]. */
        if (g_server_running) {
            if (fl_net_server_send_public(&g_server, joined) != FL_RESULT_OK) {
                fl_color_error("server msg: broadcast failed");
                return 1;
            }
            return 0;
        }
        if (fl_net_client_send_msg(&g_client, joined) != FL_RESULT_OK) {
            fl_color_error("server msg: send failed");
            return 1;
        }
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
            fl_color_error("no such recipient '%s' (use -id <N> to disambiguate)",
                           target_name ? target_name : "?");
            return 1;
        }

        if (g_server_running) {
            (void)fl_net_server_member_display(&g_server, recipient,
                                               recipient_display,
                                               sizeof(recipient_display));
            if (fl_net_server_send_private(&g_server, recipient, joined) != FL_RESULT_OK) {
                fl_color_error("server msg: deliver failed");
                return 1;
            }
        } else {
            (void)fl_net_client_member_display(&g_client, recipient,
                                               recipient_display,
                                               sizeof(recipient_display));
            if (fl_net_client_send_private(&g_client, recipient, joined) != FL_RESULT_OK) {
                fl_color_error("server msg: send failed");
                return 1;
            }
        }
        /* As with the global path, no local "You -> <recipient>" echo:
         * silence == success. Recipient renders their own "From X -> You". */
        (void)recipient_display;
    }
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

static int verb_connected(void) {
    if (g_server_running) {
        char disp[FL_NET_SERVER_DISPLAY_NAME_MAX];
        size_t count = fl_net_server_member_count(&g_server);
        fl_shell_io_lock();
        for (size_t k = 0; k < count; k++) {
            const fl_net_server_member_t *m = fl_net_server_member_at(&g_server, k);
            if (!m) continue;
            fl_net_server_member_display(&g_server, m->member_id, disp, sizeof(disp));
            printf("[%u] %s%s\n", (unsigned)m->member_id, disp,
                   m->is_host ? " <- host" : "");
        }
        fflush(stdout);
        fl_shell_io_unlock();
        return 0;
    }
    if (fl_net_client_state(&g_client) != FL_NET_CLIENT_STATE_CONNECTED) {
        fl_color_error("server connected: not currently in a session");
        return 1;
    }
    /* Client view from the cached member-list snapshot. */
    {
        size_t count = fl_net_client_member_count(&g_client);
        char disp[FL_NET_SERVER_DISPLAY_NAME_MAX];
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
    if (g_server_running) {
        fl_net_server_member_id_t new_host = FL_NET_SERVER_MEMBER_ID_NONE;
        if (g_server_bg) {
            fl_server_bg_stop_server(g_server_bg);
            g_server_bg = NULL;
        }
        (void)fl_net_server_transfer_and_stop(&g_server, &new_host);
        g_server_running = 0;
        return;
    }
    if (fl_net_client_state(&g_client) == FL_NET_CLIENT_STATE_CONNECTED) {
        if (g_client_bg) {
            fl_server_bg_stop_client(g_client_bg);
            g_client_bg = NULL;
        }
        fl_net_client_disconnect(&g_client);
    }
}

/* ------------------------------------------------------------------------- */
/* Command surface                                                           */
/* ------------------------------------------------------------------------- */

int cmd_server_run(int argc, char **argv) {
    if (argc < 2) {
        fl_color_error("usage: server <host|join|msg|announce|nick|connected|leave|kill> ...");
        return 1;
    }
    if (!strcmp(argv[1], "host"))      return verb_host(argc, argv);
    if (!strcmp(argv[1], "join"))      return verb_join(argc, argv);
    if (!strcmp(argv[1], "leave"))     return verb_leave();
    if (!strcmp(argv[1], "kill"))      return verb_kill();
    if (!strcmp(argv[1], "msg"))       return verb_msg(argc, argv);
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
