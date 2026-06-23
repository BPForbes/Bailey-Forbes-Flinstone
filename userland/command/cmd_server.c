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
#include "net_iface.h"
#include "net_ipv6.h"
#include "net_ipv4.h"
#include "net_server.h"
#include "net_socket.h"
#include "net_sock_native.h"
#include "server_bg.h"
#include "server_shared_db.h"
#include "session.h"
#include "shell_io.h"
#include "net_macvlan.h"
#include "net_wifi_host_linux.h"
#include "net_wifi_netdev.h"
#include "fl_platform.h"
#include "threadpool.h"

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#if defined(__unix__) || defined(__APPLE__)
#include <termios.h>
#endif
#if defined(__linux__) || defined(__APPLE__) || defined(__FreeBSD__)
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#define FL_SERVER_UDP_HOSTED 1
#endif

/* ------------------------------------------------------------------------- */
/* Process-wide session state                                                */
/* ------------------------------------------------------------------------- */

static pthread_mutex_t session_mutex = PTHREAD_MUTEX_INITIALIZER;

static fl_net_server_t g_server;
static fl_server_bg_t *g_server_bg;
static int g_server_running;

static fl_net_client_t g_client;
static fl_server_bg_t *g_client_bg;

/* UDP beacon + reverse-dial listener — started on server host, stopped on kill/exit. */
static volatile int g_server_udp_stop;
static pthread_t    g_beacon_thread;
static int          g_beacon_thread_started;
static pthread_t    g_udp_listener_thread;
static int          g_udp_listener_thread_started;
static int          g_udp_listen_sock = -1; /* raw fd; closed to unblock recvfrom */

/* WSL portproxy state — torn down on server kill / leave / exit. */
static int      g_wsl_portproxy_active;
static char     g_wsl_portproxy_listen[32];
static uint16_t g_wsl_portproxy_port;

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

static void print_sock_error(const char *verb, fl_result_t rc) {
    int e = fl_net_sock_last_errno();
    if (e != 0)
        fl_color_error("%s failed (rc=%d): %s", verb, (int)rc, strerror(e));
    else if (rc == FL_RESULT_ACCES)
        fl_color_error("%s failed: permission denied (try a port >= 1024 or run as root)", verb);
    else if (rc == FL_RESULT_BUSY)
        fl_color_error("%s failed: address in use or not available on this host", verb);
    else
        fl_color_error("%s failed (rc=%d)", verb, (int)rc);
    if (e == EADDRNOTAVAIL) {
        char bind_ip[32];
        if (fl_net_iface_suggest_ipv4(NULL, bind_ip, sizeof(bind_ip)))
            fprintf(stderr, "hint: that IPv4 is not bindable here — try: %s :<port>  or  %s %s:<port>\n",
                    verb, verb, bind_ip);
        else
            fprintf(stderr, "hint: try: %s :<port>  (bind all interfaces)\n", verb);
    } else if (e == EACCES || rc == FL_RESULT_ACCES) {
        fputs("hint: on Linux/WSL ports below 1024 need root; try server host :8888 or another port >= 1024\n",
              stderr);
    }
}

static int wsl_portproxy_press_any_key(void) {
#if defined(__unix__) || defined(__APPLE__)
    struct termios old_tty;
    struct termios new_tty;
    unsigned char c;
    int           use_raw = 0;
    int           rc      = -1;

    if (!isatty(STDIN_FILENO))
        return 0;

    fl_shell_press_any_key_gate_enter();
    fputs(" Press any key to continue...", stdout);
    fflush(stdout);

    if (tcgetattr(STDIN_FILENO, &old_tty) == 0) {
        new_tty = old_tty;
        new_tty.c_lflag &= (tcflag_t)~(ICANON | ECHO | ISIG);
        new_tty.c_cc[VMIN]  = 1;
        new_tty.c_cc[VTIME] = 0;
        if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &new_tty) == 0)
            use_raw = 1;
    }

    for (;;) {
        if (use_raw) {
            ssize_t n = read(STDIN_FILENO, &c, 1);
            if (n < 0) {
                if (errno == EINTR) {
                    (void)write(STDOUT_FILENO, "^C\n", 3);
                    fputs(" Press any key to continue...", stdout);
                    fflush(stdout);
                    continue;
                }
                break;
            }
            if (n == 0)
                break;
            if (c == 3u) {
                (void)write(STDOUT_FILENO, "^C\n", 3);
                fputs(" Press any key to continue...", stdout);
                fflush(stdout);
                continue;
            }
            (void)tcsetattr(STDIN_FILENO, TCSAFLUSH, &old_tty);
            fputc('\n', stdout);
            fflush(stdout);
            rc = 0;
            break;
        }
        {
            int ch = fgetc(stdin);
            if (ch == EOF) {
                if (errno == EINTR) {
                    (void)write(STDOUT_FILENO, "^C\n", 3);
                    fputs(" Press any key to continue...", stdout);
                    fflush(stdout);
                    continue;
                }
                break;
            }
            if (ch == 3) {
                (void)write(STDOUT_FILENO, "^C\n", 3);
                fputs(" Press any key to continue...", stdout);
                fflush(stdout);
                continue;
            }
            fputc('\n', stdout);
            fflush(stdout);
            rc = 0;
            break;
        }
    }

    if (use_raw)
        (void)tcsetattr(STDIN_FILENO, TCSAFLUSH, &old_tty);
    fl_shell_press_any_key_gate_leave();
    return rc;
#else
    return 0;
#endif
}

static void wsl_portproxy_notice_setup_message(void) {
    fl_color_warn("NOTICE: Flinstone needs elevated permissions to expose this port "
                  "to the LAN and will modify Windows port mapping and firewall rules. "
                  "This is temporary and will be reverted when the server is killed.");
}

static void wsl_portproxy_notice_teardown_message(void) {
    fl_color_warn("NOTICE: Flinstone will remove the temporary Windows port mapping "
                  "and firewall rules (UAC may prompt).");
}

#if defined(FL_SERVER_UDP_HOSTED)
static void server_udp_stop(void);
#endif

static void host_abort_started_listener(void) {
    pthread_mutex_lock(&session_mutex);
    if (g_server_bg) {
        fl_server_bg_stop_server(g_server_bg);
        g_server_bg = NULL;
    }
    fl_net_server_host_stop(&g_server);
    g_server_running = 0;
    pthread_mutex_unlock(&session_mutex);
#if defined(FL_SERVER_UDP_HOSTED)
    server_udp_stop();
#endif
}

static int wsl_portproxy_teardown_wants_notice(void) {
    return g_wsl_portproxy_active && fl_platform_detect() == FL_PLATFORM_WSL;
}

static void wsl_portproxy_teardown_apply(void) {
    char listen[32];
    uint16_t port;

    if (!g_wsl_portproxy_active)
        return;
    strncpy(listen, g_wsl_portproxy_listen, sizeof(listen) - 1u);
    listen[sizeof(listen) - 1u] = '\0';
    port = g_wsl_portproxy_port;
    g_wsl_portproxy_active = 0;
    g_wsl_portproxy_listen[0] = '\0';
    g_wsl_portproxy_port = 0;
    if (fl_net_wifi_host_linux_server_proxy_del(listen, port) != 0) {
        fl_color_error("failed to remove Windows portproxy/firewall for %s:%u "
                       "(approve UAC or run server kill)",
                       listen, (unsigned)port);
    }
}

static void wsl_portproxy_teardown_interactive(void) {
    int needs_key;

    if (!g_wsl_portproxy_active)
        return;
    needs_key = wsl_portproxy_teardown_wants_notice();
    if (needs_key)
        wsl_portproxy_notice_teardown_message();
    if (needs_key)
        (void)wsl_portproxy_press_any_key();
    wsl_portproxy_teardown_apply();
}

static int wsl_portproxy_apply(const char *listen_ip, uint16_t port) {
    char wsl_ip[32];

    if (fl_platform_detect() != FL_PLATFORM_WSL)
        return -1;
    if (!listen_ip || !listen_ip[0] || port == 0u)
        return -1;
    if (fl_net_wifi_host_linux_wsl_ipv4(wsl_ip, sizeof(wsl_ip)) != 0)
        return -1;
    if (fl_net_wifi_host_linux_server_proxy(listen_ip, wsl_ip, port) != 0)
        return -1;
    strncpy(g_wsl_portproxy_listen, listen_ip, sizeof(g_wsl_portproxy_listen) - 1u);
    g_wsl_portproxy_listen[sizeof(g_wsl_portproxy_listen) - 1u] = '\0';
    g_wsl_portproxy_port = port;
    g_wsl_portproxy_active = 1;
    return 0;
}

static int wsl_addr_bindable_v4(uint32_t addr_be) {
    uint32_t wsl_be = 0u;

    if (addr_be == 0u || fl_net_ipv4_is_loopback(addr_be))
        return 1;
    if (fl_net_wifi_host_linux_ipv4_route(&wsl_be, NULL, NULL) == FL_RESULT_OK &&
        wsl_be == addr_be)
        return 1;
    if (fl_net_macvlan_get_registered(&wsl_be, NULL, NULL) && wsl_be == addr_be)
        return 1;
    if (fl_net_sock_native_eligible_bind_v4(addr_be))
        return 1;
    return 0;
}

static const char *wsl_rewrite_lan_bind_ep(const fl_net_endpoint_t *ep,
                                           fl_net_endpoint_t *bind_ep,
                                           char *display_buf, size_t display_cap) {
    const char *wip;

    if (fl_platform_detect() != FL_PLATFORM_WSL || !ep ||
        ep->family != FL_NET_ADDR_FAMILY_V4 || ep->addr.v4_be == 0u || !bind_ep)
        return NULL;

    wip = fl_net_wifi_host_linux_windows_ipv4();
    if (wip && wip[0]) {
        uint32_t win_be = 0u;
        if (fl_net_ipv4_parse_literal(wip, &win_be) && win_be == ep->addr.v4_be) {
            bind_ep->addr.v4_be = 0u;
            return wip;
        }
    }

    if (!wsl_addr_bindable_v4(ep->addr.v4_be)) {
        fl_net_ipv4_format_addr(ep->addr.v4_be, display_buf, display_cap);
        bind_ep->addr.v4_be = 0u;
        return display_buf;
    }
    return NULL;
}

static int wsl_portproxy_will_try(const fl_net_endpoint_t *bind_ep,
                                  const char *win_ip_display) {
    if (fl_platform_detect() != FL_PLATFORM_WSL)
        return 0;
    if (win_ip_display && win_ip_display[0])
        return 1;
    return bind_ep && bind_ep->family == FL_NET_ADDR_FAMILY_V4 &&
           bind_ep->port_host > 0u;
}

static int wsl_in_tree_lab_bind(const fl_net_endpoint_t *bind_ep) {
    if (!bind_ep || bind_ep->family != FL_NET_ADDR_FAMILY_V4)
        return 0;
    if (bind_ep->addr.v4_be != 0u) {
        if (fl_net_ipv4_is_loopback(bind_ep->addr.v4_be))
            return fl_net_wifi_netdev_is_up() && !fl_net_wifi_host_linux_opted_in();
        return fl_net_sock_native_eligible_bind_v4(bind_ep->addr.v4_be);
    }
    return fl_net_wifi_netdev_is_up() && !fl_net_wifi_host_linux_opted_in();
}

static int wsl_portproxy_should_skip(const fl_net_endpoint_t *bind_ep) {
    if (!bind_ep || bind_ep->family != FL_NET_ADDR_FAMILY_V4)
        return 0;
    if (bind_ep->addr.v4_be != 0u && fl_net_ipv4_is_loopback(bind_ep->addr.v4_be))
        return 1;
    return wsl_in_tree_lab_bind(bind_ep);
}

static const char *host_listen_ip_for_wsl(const fl_net_endpoint_t *ep,
                                          const char *win_ip_display,
                                          char *buf, size_t cap) {
    if (win_ip_display && win_ip_display[0]) {
        strncpy(buf, win_ip_display, cap - 1u);
        buf[cap - 1u] = '\0';
        return buf;
    }
    if (ep && ep->family == FL_NET_ADDR_FAMILY_V4 && ep->addr.v4_be != 0u) {
        fl_net_ipv4_format_addr(ep->addr.v4_be, buf, cap);
        return buf;
    }
    strncpy(buf, "0.0.0.0", cap - 1u);
    buf[cap - 1u] = '\0';
    return buf;
}

static void host_print_wsl_lan_hint(const char *listen_ip, uint16_t port) {
    const char *wip = fl_net_wifi_host_linux_windows_ipv4();
    char        peer[32];

    if (listen_ip && strcmp(listen_ip, "0.0.0.0") != 0) {
        strncpy(peer, listen_ip, sizeof(peer) - 1u);
        peer[sizeof(peer) - 1u] = '\0';
    } else if (wip && wip[0]) {
        strncpy(peer, wip, sizeof(peer) - 1u);
        peer[sizeof(peer) - 1u] = '\0';
    } else if (!fl_net_iface_suggest_ipv4(NULL, peer, sizeof(peer))) {
        return;
    }
    fl_color_success("peers on LAN can: server join %s:%u", peer, (unsigned)port);
}

static int parse_host_endpoint(int argc, char **argv, fl_net_endpoint_t *ep) {
    uint32_t any_be = 0u;
    long port;
    char *end = NULL;

    if (argc < 3 || !ep)
        return -1;
    if (!strcmp(argv[2], "-all")) {
        if (argc < 4) {
            fl_color_error("usage: server host -all <port>");
            return -1;
        }
        errno = 0;
        port = strtol(argv[3], &end, 10);
        if (errno != 0 || end == argv[3] || (end && *end != '\0') || port <= 0 || port > 65535) {
            fl_color_error("invalid port '%s'", argv[3]);
            return -1;
        }
        if (!fl_net_ipv4_parse_literal("0.0.0.0", &any_be))
            return -1;
        fl_net_endpoint_from_v4(any_be, (uint16_t)port, ep);
        return 0;
    }
    if (argv[2][0] == ':' && argv[2][1] != '\0') {
        errno = 0;
        port = strtol(argv[2] + 1, &end, 10);
        if (errno != 0 || end == argv[2] + 1 || (end && *end != '\0') || port <= 0 || port > 65535) {
            fl_color_error("invalid port '%s'", argv[2]);
            return -1;
        }
        if (!fl_net_ipv4_parse_literal("0.0.0.0", &any_be))
            return -1;
        fl_net_endpoint_from_v4(any_be, (uint16_t)port, ep);
        return 0;
    }
    if (strchr(argv[2], ':') == NULL) {
        errno = 0;
        port = strtol(argv[2], &end, 10);
        if (errno == 0 && end != argv[2] && end && *end == '\0' && port > 0 && port <= 65535) {
            if (!fl_net_ipv4_parse_literal("0.0.0.0", &any_be))
                return -1;
            fl_net_endpoint_from_v4(any_be, (uint16_t)port, ep);
            return 0;
        }
    }
    if (parse_endpoint_full(argv[2], ep) != 0)
        return -1;
    return 0;
}

static int verb_interfaces(int argc, char **argv) {
    fl_net_iface_entry_t entries[32];
    unsigned count = 0u;
    size_t i;
    char addr[32];
    char suggest[32];

    (void)argc;
    (void)argv;
    fl_net_iface_refresh();
    count = fl_net_iface_list(entries, 32);
    if (count == 0u) {
        fl_color_error("interface list unavailable on this build");
        return 1;
    }
    puts("Interfaces (IPv4; IPv6 when assigned by router after wifi join):");
    for (i = 0; i < count; i++) {
        char addr6[64];
        fl_net_ipv4_format_addr(entries[i].addr_be, addr, sizeof(addr));
        printf("  %s %-15s/%u %s%s", entries[i].name, addr,
               (unsigned)entries[i].prefix_len,
               (entries[i].flags & FL_NET_IFF_UP) ? "up" : "down",
               (entries[i].flags & FL_NET_IFF_LOOPBACK) ? " loopback" : "");
        if (entries[i].has_ipv6 &&
            fl_net_ipv6_format_addr(entries[i].addr6, addr6, sizeof(addr6)))
            printf("  v6 %s/%u", addr6, (unsigned)entries[i].prefix6_len);
        putchar('\n');
    }
    if (fl_net_iface_suggest_ipv4(NULL, suggest, sizeof(suggest)))
        printf("Suggested LAN join address for peers: %s\n", suggest);
    else
        puts("No non-loopback IPv4 found; use server host -all <port> for all interfaces.");
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
/* UDP beacon + reverse-dial listener (server-side LAN discovery / NAT bypass) */
/* ------------------------------------------------------------------------- */

#if defined(FL_SERVER_UDP_HOSTED)

/* Broadcasts a Flinstone discovery beacon to LAN every 2 s so joining
 * clients can find this server without knowing its IP in advance.        */
static void *server_beacon_thread_func(void *arg)
{
    uint16_t port = (uint16_t)(uintptr_t)arg;
    uint8_t  frame[16];
    struct sockaddr_in dst;
    int sock, one = 1;

    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0)
        return NULL;
    setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &one, sizeof(one));

    frame[0] = (uint8_t)FL_NET_DISCOVERY_MAGIC_0;
    frame[1] = (uint8_t)FL_NET_DISCOVERY_MAGIC_1;
    frame[2] = (uint8_t)FL_NET_DISCOVERY_MAGIC_2;
    frame[3] = (uint8_t)FL_NET_DISCOVERY_MAGIC_3;
    frame[4] = (uint8_t)FL_NET_DISCOVERY_OP_BEACON;
    frame[5] = (uint8_t)((port >> 8) & 0xFFu);
    frame[6] = (uint8_t)(port & 0xFFu);
    memset(frame + 7, 0, sizeof(frame) - 7u);

    memset(&dst, 0, sizeof(dst));
    dst.sin_family      = AF_INET;
    dst.sin_addr.s_addr = htonl(INADDR_BROADCAST);
    dst.sin_port        = htons(FL_NET_SERVER_DISCOVERY_PORT);

    while (!g_server_udp_stop) {
        sendto(sock, frame, sizeof(frame), 0,
               (const struct sockaddr *)&dst, sizeof(dst));
        sleep(2);
    }
    close(sock);
    return NULL;
}

/* Listens for UDP join-requests from clients that cannot connect via TCP
 * (e.g. the server is behind NAT). On receipt, dials TCP back to the
 * client's callback address and hands the socket to the server session. */
static void *server_udp_listener_thread_func(void *arg)
{
    uint8_t buf[16];
    struct sockaddr_in from;
    socklen_t fromlen;
    ssize_t n;
    (void)arg;

    while (!g_server_udp_stop) {
        fromlen = sizeof(from);
        n = recvfrom(g_udp_listen_sock, buf, sizeof(buf), 0,
                     (struct sockaddr *)&from, &fromlen);
        if (n < 0)
            break;
        if ((size_t)n < 11u)
            continue;
        if (buf[0] != (uint8_t)FL_NET_DISCOVERY_MAGIC_0 ||
            buf[1] != (uint8_t)FL_NET_DISCOVERY_MAGIC_1 ||
            buf[2] != (uint8_t)FL_NET_DISCOVERY_MAGIC_2 ||
            buf[3] != (uint8_t)FL_NET_DISCOVERY_MAGIC_3)
            continue;
        if (buf[4] != (uint8_t)FL_NET_DISCOVERY_OP_JOIN_REQ)
            continue;

        /* buf[5..8] = callback IPv4 BE, buf[9..10] = callback port BE */
        uint32_t cb_ip_be;
        uint16_t cb_port_be;
        memcpy(&cb_ip_be,   buf + 5, 4);
        memcpy(&cb_port_be, buf + 9, 2);

        fl_net_endpoint_t cb_ep;
        fl_net_endpoint_from_v4(cb_ip_be, ntohs(cb_port_be), &cb_ep);

        fl_net_sock_handle_t dial_h = FL_NET_SOCK_INVALID;
        fl_result_t rc = fl_net_sock_open_for(&cb_ep, FL_NET_SOCK_TYPE_STREAM, &dial_h);
        if (rc == FL_RESULT_OK)
            rc = fl_net_sock_connect_from_ep(dial_h, NULL, &cb_ep);
        if (rc == FL_RESULT_OK) {
            pthread_mutex_lock(&session_mutex);
            if (g_server_running)
                (void)fl_net_server_accept_handle(&g_server, dial_h, NULL, 0u);
            else
                fl_net_sock_close(dial_h);
            pthread_mutex_unlock(&session_mutex);
        } else {
            if (dial_h != FL_NET_SOCK_INVALID)
                fl_net_sock_close(dial_h);
        }
    }
    return NULL;
}

static void server_udp_start(uint16_t port)
{
    struct sockaddr_in sin;
    int sock, one = 1;

    g_server_udp_stop = 0;

    /* UDP listener on the discovery port for incoming join-requests. */
    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock >= 0) {
        setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        memset(&sin, 0, sizeof(sin));
        sin.sin_family      = AF_INET;
        sin.sin_addr.s_addr = INADDR_ANY;
        sin.sin_port        = htons(FL_NET_SERVER_DISCOVERY_PORT);
        if (bind(sock, (struct sockaddr *)&sin, sizeof(sin)) == 0) {
            g_udp_listen_sock = sock;
            if (pthread_create(&g_udp_listener_thread, NULL,
                               server_udp_listener_thread_func, NULL) == 0)
                g_udp_listener_thread_started = 1;
        } else {
            close(sock);
        }
    }

    /* Beacon thread — broadcasts server presence. */
    if (pthread_create(&g_beacon_thread, NULL, server_beacon_thread_func,
                       (void *)(uintptr_t)port) == 0)
        g_beacon_thread_started = 1;
}

static void server_udp_stop(void)
{
    g_server_udp_stop = 1;

    if (g_udp_listen_sock >= 0) {
        close(g_udp_listen_sock);
        g_udp_listen_sock = -1;
    }
    if (g_udp_listener_thread_started) {
        pthread_join(g_udp_listener_thread, NULL);
        g_udp_listener_thread_started = 0;
    }
    if (g_beacon_thread_started) {
        pthread_join(g_beacon_thread, NULL);
        g_beacon_thread_started = 0;
    }
}

/* Client-side: when direct TCP connect failed, open a local TCP listener,
 * send a UDP join-request to the server's discovery port, then wait for the
 * server to dial back. Works through server-side NAT (server dials outbound
 * to a client with a reachable IP). */
static fl_result_t server_join_reverse_dial(fl_net_client_t *client,
                                            const fl_net_endpoint_t *server_ep,
                                            const char *principal,
                                            unsigned timeout_ms)
{
    struct sockaddr_in sin;
    socklen_t slen;
    struct timeval tv;
    fd_set rfds;
    int listen_fd, peer_fd, ufd, one = 1;
    uint16_t listen_port;
    char own_ip_str[32];
    uint32_t own_ip_be = 0u;
    fl_net_sock_handle_t h;
    fl_result_t rc;

    if (server_ep->family != FL_NET_ADDR_FAMILY_V4)
        return FL_RESULT_NOSYS;

    /* Open TCP listener on an ephemeral port. */
    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0)
        return FL_RESULT_ERR;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    memset(&sin, 0, sizeof(sin));
    sin.sin_family      = AF_INET;
    sin.sin_addr.s_addr = INADDR_ANY;
    sin.sin_port        = 0; /* ephemeral */
    if (bind(listen_fd, (struct sockaddr *)&sin, sizeof(sin)) < 0 ||
        listen(listen_fd, 1) < 0) {
        close(listen_fd);
        return FL_RESULT_ERR;
    }
    slen = sizeof(sin);
    getsockname(listen_fd, (struct sockaddr *)&sin, &slen);
    listen_port = ntohs(sin.sin_port);

    /* Get own LAN IP so the server knows where to dial back. */
    if (!fl_net_iface_suggest_ipv4(NULL, own_ip_str, sizeof(own_ip_str)) ||
        !fl_net_ipv4_parse_literal(own_ip_str, &own_ip_be)) {
        close(listen_fd);
        return FL_RESULT_NOSYS;
    }

    /* Send UDP join-request: magic(4) + op(1) + own_ip_be(4) + port_be(2) */
    ufd = socket(AF_INET, SOCK_DGRAM, 0);
    if (ufd >= 0) {
        uint8_t req[11];
        struct sockaddr_in dst;
        uint16_t port_be = htons(listen_port);
        req[0] = (uint8_t)FL_NET_DISCOVERY_MAGIC_0;
        req[1] = (uint8_t)FL_NET_DISCOVERY_MAGIC_1;
        req[2] = (uint8_t)FL_NET_DISCOVERY_MAGIC_2;
        req[3] = (uint8_t)FL_NET_DISCOVERY_MAGIC_3;
        req[4] = (uint8_t)FL_NET_DISCOVERY_OP_JOIN_REQ;
        memcpy(req + 5, &own_ip_be, 4);
        memcpy(req + 9, &port_be,   2);
        memset(&dst, 0, sizeof(dst));
        dst.sin_family      = AF_INET;
        dst.sin_addr.s_addr = server_ep->addr.v4_be;
        dst.sin_port        = htons(FL_NET_SERVER_DISCOVERY_PORT);
        sendto(ufd, req, sizeof(req), 0, (const struct sockaddr *)&dst, sizeof(dst));
        close(ufd);
    }

    /* Wait for the server's TCP callback. */
    tv.tv_sec  = (long)(timeout_ms / 1000u);
    tv.tv_usec = (long)((timeout_ms % 1000u) * 1000L);
    FD_ZERO(&rfds);
    FD_SET(listen_fd, &rfds);
    if (select(listen_fd + 1, &rfds, NULL, NULL, &tv) <= 0) {
        close(listen_fd);
        return FL_RESULT_TIMEDOUT;
    }

    peer_fd = accept(listen_fd, NULL, NULL);
    close(listen_fd);
    if (peer_fd < 0)
        return FL_RESULT_ERR;

    /* Adopt the fd and run the HELLO handshake. */
    rc = fl_net_sock_from_fd(peer_fd, &h);
    if (rc != FL_RESULT_OK) {
        close(peer_fd);
        return rc;
    }
    rc = fl_net_client_adopt_handle(client, h, principal, timeout_ms);
    if (rc != FL_RESULT_OK)
        fl_net_sock_close(h);
    return rc;
}

#endif /* FL_SERVER_UDP_HOSTED */

/* ------------------------------------------------------------------------- */
/* Verb handlers                                                             */
/* ------------------------------------------------------------------------- */

/* `server host <ip:port>` — bind the listener, insert host as member 1,
 * spawn the bg accept/poll loop. Fails if a join is already in flight or
 * if hosted sockets are unavailable on this build (FL_RESULT_NOSYS). */
static int verb_host(int argc, char **argv) {
    fl_net_endpoint_t ep;
    fl_net_endpoint_t bind_ep;
    fl_result_t rc;
    const char *win_ip_display = NULL;

    if (argc < 3) {
        char suggest[32];
        fl_color_error("usage: server host <ip:port> | server host <port> | server host -all <port> | server host :<port>");
        if (fl_net_iface_suggest_ipv4(NULL, suggest, sizeof(suggest)))
            fprintf(stderr, "hint: after wifi join try  server host :8888  or  server host %s:8888\n",
                    suggest);
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
    fl_net_sock_clear_errno();
    if (parse_host_endpoint(argc, argv, &ep) != 0) {
        pthread_mutex_unlock(&session_mutex);
        return 1;
    }
    bind_ep = ep;

    /* Prefer fl0 macvlan IP when user did not specify a particular IP. */
    {
        uint32_t fl0_ip = 0u;
        if (bind_ep.family == FL_NET_ADDR_FAMILY_V4 && bind_ep.addr.v4_be == 0u &&
            fl_net_macvlan_get_registered(&fl0_ip, NULL, NULL) && fl0_ip != 0u)
            bind_ep.addr.v4_be = fl0_ip;
    }

    /* Windows / LAN IPv4 (e.g. 192.168.1.239) is not bindable in WSL — bind
     * 0.0.0.0 locally and expose the requested IP via portproxy. */
    {
        char lan_display[32];
        const char *rewrite = wsl_rewrite_lan_bind_ep(&ep, &bind_ep, lan_display,
                                                    sizeof(lan_display));
        if (rewrite)
            win_ip_display = rewrite;
    }

    {
        char listen_ip[32];
        const char *listen = host_listen_ip_for_wsl(&ep, win_ip_display,
                                                    listen_ip, sizeof(listen_ip));
        int defer_hosting = wsl_portproxy_will_try(&bind_ep, win_ip_display);

        if (defer_hosting) {
            int port_free =
                fl_net_wifi_host_linux_server_proxy_port_free(listen,
                                                              bind_ep.port_host);
            if (port_free == 0) {
                pthread_mutex_unlock(&session_mutex);
                fl_color_error("server host failed: Windows portproxy or firewall "
                               "for port %u is already configured on %s "
                               "(run server kill to remove stale rules, or choose "
                               "another port)",
                               (unsigned)bind_ep.port_host, listen);
                return 1;
            }
        }

        if (defer_hosting)
            wsl_portproxy_notice_setup_message();
        pthread_mutex_unlock(&session_mutex);

        if (defer_hosting && wsl_portproxy_press_any_key() != 0)
            return 1;

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

        rc = fl_net_server_host_start_ep(&g_server, &bind_ep, current_principal());
        if (rc == FL_RESULT_NOSYS) {
            pthread_mutex_unlock(&session_mutex);
            fl_color_error("hosted sockets unavailable; cannot host");
            return 1;
        }
        if (rc != FL_RESULT_OK) {
            pthread_mutex_unlock(&session_mutex);
            print_sock_error("server host", rc);
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

#if defined(FL_SERVER_UDP_HOSTED)
        server_udp_start(bind_ep.port_host);
#endif

        if (win_ip_display) {
            if (wsl_portproxy_apply(listen, ep.port_host) == 0) {
                fl_color_success("hosting as '%s' on %s:%u",
                                 current_principal(), win_ip_display,
                                 (unsigned)ep.port_host);
                fl_color_success("WSL portproxy active (%s → WSL)", listen);
                host_print_wsl_lan_hint(listen, ep.port_host);
            } else if (fl_net_wifi_host_linux_server_bridge_to(win_ip_display, NULL,
                                                               ep.port_host) == 0) {
                fl_color_success("hosting as '%s' on %s:%u",
                                 current_principal(), win_ip_display,
                                 (unsigned)ep.port_host);
                fl_color_success("LAN peers: server join %s:%u (bridge active, no admin)",
                                 win_ip_display, (unsigned)ep.port_host);
            } else {
                fl_color_error("server host failed: could not expose %s:%u to the LAN "
                               "(portproxy and bridge both failed — approve UAC or run "
                               "FlinstonePowershell.exe server-proxy %s <wsl-ip> %u)",
                               win_ip_display, (unsigned)ep.port_host, listen,
                               (unsigned)ep.port_host);
                host_abort_started_listener();
                return 1;
            }
        } else {
            if (!defer_hosting) {
                char bind_txt[128];
                if (fl_net_endpoint_format(&bind_ep, bind_txt, sizeof(bind_txt)))
                    fl_color_success("hosting as '%s' on %s", current_principal(), bind_txt);
                else
                    fl_color_success("hosting as '%s' on %s", current_principal(), argv[2]);
            }
            if (fl_platform_detect() == FL_PLATFORM_WSL &&
                fl_net_wifi_host_linux_wsl_mirrored() &&
                bind_ep.family == FL_NET_ADDR_FAMILY_V4 &&
                bind_ep.port_host > 0u) {
                char suggest[32];
                if (fl_net_iface_suggest_ipv4(NULL, suggest, sizeof(suggest)))
                    fl_color_success(
                        "peers on LAN can: server join %s:%u  (WSL mirrored — direct, no relay)",
                        suggest, (unsigned)bind_ep.port_host);
            } else if (fl_platform_detect() == FL_PLATFORM_WSL &&
                bind_ep.family == FL_NET_ADDR_FAMILY_V4 && bind_ep.port_host > 0u &&
                !wsl_portproxy_should_skip(&bind_ep)) {
                if (wsl_portproxy_apply(listen, bind_ep.port_host) == 0) {
                    if (defer_hosting) {
                        char bind_txt[128];
                        if (fl_net_endpoint_format(&bind_ep, bind_txt, sizeof(bind_txt)))
                            fl_color_success("hosting as '%s' on %s",
                                             current_principal(), bind_txt);
                        else
                            fl_color_success("hosting as '%s' on %s",
                                             current_principal(), argv[2]);
                    }
                    fl_color_success("WSL portproxy active (%s → WSL)", listen);
                    host_print_wsl_lan_hint(listen, bind_ep.port_host);
                } else {
                    const char *wip = fl_net_wifi_host_linux_windows_ipv4();
                    char        bip[32];
                    const char *bridge_target =
                        (bind_ep.addr.v4_be == 0u) ? NULL : bip;

                    if (bind_ep.addr.v4_be != 0u)
                        fl_net_ipv4_format_addr(bind_ep.addr.v4_be, bip, sizeof(bip));
                    if (wip && fl_net_wifi_host_linux_server_bridge_to(
                            wip, bridge_target, bind_ep.port_host) == 0) {
                        if (defer_hosting) {
                            char bind_txt[128];
                            if (fl_net_endpoint_format(&bind_ep, bind_txt, sizeof(bind_txt)))
                                fl_color_success("hosting as '%s' on %s",
                                                 current_principal(), bind_txt);
                            else
                                fl_color_success("hosting as '%s' on %s",
                                                 current_principal(), argv[2]);
                        }
                        fl_color_success("peers on LAN can: server join %s:%u (bridge active)",
                                         wip, (unsigned)bind_ep.port_host);
                    } else {
                        if (defer_hosting) {
                            char bind_txt[128];
                            if (fl_net_endpoint_format(&bind_ep, bind_txt, sizeof(bind_txt)))
                                fl_color_success("hosting as '%s' on %s",
                                                 current_principal(), bind_txt);
                            else
                                fl_color_success("hosting as '%s' on %s",
                                                 current_principal(), argv[2]);
                        }
                        fl_color_error("server running WSL-local only (LAN unreachable): "
                                       "portproxy and bridge both failed");
                        host_print_wsl_lan_hint(listen, bind_ep.port_host);
                    }
                }
            } else if (wsl_in_tree_lab_bind(&bind_ep)) {
                host_print_wsl_lan_hint(listen, bind_ep.port_host);
                puts("in-tree lab host: no Windows portproxy (simulated Wi-Fi). "
                     "Use server join <addr>:<port> on this shell, or bind :<port> "
                     "and join 127.0.0.1:<port> from another terminal.");
            } else if (bind_ep.family == FL_NET_ADDR_FAMILY_V4 &&
                       bind_ep.port_host > 0u &&
                       bind_ep.addr.v4_be != 0u &&
                       !fl_net_ipv4_is_loopback(bind_ep.addr.v4_be)) {
                char peer_ip[32];
                fl_net_ipv4_format_addr(bind_ep.addr.v4_be, peer_ip, sizeof(peer_ip));
                fl_color_success("peers on LAN can: server join %s:%u",
                                 peer_ip, (unsigned)bind_ep.port_host);
            } else {
                host_print_wsl_lan_hint(listen, bind_ep.port_host);
            }
        }
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
            if (!fl_net_endpoint_parse_bind(argv[i + 1], &local)) {
                pthread_mutex_unlock(&session_mutex);
                fl_color_error("invalid -bind address '%s'", argv[i + 1]);
                return 1;
            }
            i++;
        }
    }
    fl_net_sock_clear_errno();
    rc = fl_net_client_connect_ep(&g_client, local.family ? &local : NULL, &peer,
                                  current_principal(), 3000u);
    if (rc == FL_RESULT_NOSYS) {
        pthread_mutex_unlock(&session_mutex);
        fl_color_error("hosted sockets unavailable; cannot join");
        return 1;
    }
#if defined(FL_SERVER_UDP_HOSTED)
    if (rc != FL_RESULT_OK && peer.family == FL_NET_ADDR_FAMILY_V4) {
        /* Direct TCP connect failed — try reverse-dial NAT bypass.
         * We open a local TCP listener and ask the server to dial back. */
        fl_result_t rdr = server_join_reverse_dial(&g_client, &peer,
                                                   current_principal(), 5000u);
        if (rdr == FL_RESULT_OK)
            rc = FL_RESULT_OK;
    }
#endif
    if (rc != FL_RESULT_OK) {
        pthread_mutex_unlock(&session_mutex);
        print_sock_error("server join", rc);
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
        wsl_portproxy_teardown_interactive();
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
    wsl_portproxy_teardown_interactive();
#if defined(FL_SERVER_UDP_HOSTED)
    server_udp_stop();
#endif
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
            char peer_txt[128];
            (void)fl_net_client_member_display(&g_client, m->member_id, disp,
                                               sizeof(disp));
            if (m->peer_addr.family &&
                fl_net_endpoint_format(&m->peer_addr, peer_txt, sizeof(peer_txt)))
                printf("[%u] %s  %s%s%s\n", (unsigned)m->member_id, disp, peer_txt,
                       m->is_host ? " <- host" : "",
                       m->member_id == g_client.assigned_member_id ? " (you)" : "");
            else
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
    static int done;

    if (done)
        return;
    done = 1;

    pthread_mutex_lock(&session_mutex);
    if (g_server_running) {
        fl_net_server_member_id_t new_host = FL_NET_SERVER_MEMBER_ID_NONE;
        if (g_server_bg) {
            fl_server_bg_stop_server(g_server_bg);
            g_server_bg = NULL;
        }
        (void)fl_net_server_transfer_and_stop(&g_server, &new_host);
        g_server_running = 0;
    } else if (fl_net_client_state(&g_client) == FL_NET_CLIENT_STATE_CONNECTED) {
        if (g_client_bg) {
            fl_server_bg_stop_client(g_client_bg);
            g_client_bg = NULL;
        }
        fl_net_client_disconnect(&g_client);
    }
    pthread_mutex_unlock(&session_mutex);

    wsl_portproxy_teardown_interactive();
#if defined(FL_SERVER_UDP_HOSTED)
    server_udp_stop();
#endif
}

/* ------------------------------------------------------------------------- */
/* Command surface                                                           */
/* ------------------------------------------------------------------------- */

static int verb_netinit(void) {
    char parent[FL_NET_MACVLAN_IFNAMSIZ];
    uint8_t mac[6];
    uint32_t ip_be = 0u, mask_be = 0u, gw_be = 0u;
    fl_result_t rc;
    char ip_s[32], gw_s[32];
    uint8_t prefix = 24u;
    uint32_t mask_h;

    if (fl_net_macvlan_get_registered(&ip_be, &mask_be, &prefix) && ip_be != 0u) {
        fl_net_ipv4_format_addr(ip_be, ip_s, sizeof(ip_s));
        fl_color_success("fl0 already configured: %s/%u", ip_s, (unsigned)prefix);
        return 0;
    }

    rc = fl_net_macvlan_create(FL_NET_MACVLAN_NAME, parent);
    if (rc != FL_RESULT_OK) {
        if (rc == FL_RESULT_ACCES)
            fl_color_error("fl0 setup needs elevated permissions — run with sudo or:\n"
                           "  ip link add fl0 link <parent> type macvlan mode bridge\n"
                           "  ip link set fl0 up");
        else
            fl_color_error("fl0 create failed: no suitable LAN parent interface found");
        return 1;
    }
    rc = fl_net_macvlan_hwaddr(FL_NET_MACVLAN_NAME, mac);
    if (rc != FL_RESULT_OK) {
        fl_color_error("fl0: could not read hardware address");
        return 1;
    }
    rc = fl_net_macvlan_dhcp_lease(FL_NET_MACVLAN_NAME, mac, &ip_be, &mask_be, &gw_be, 5000u);
    if (rc != FL_RESULT_OK) {
        fl_color_error("fl0: DHCP lease failed (no response from router within 5 s)");
        return 1;
    }

    fl_net_ipv4_format_addr(ip_be, ip_s, sizeof(ip_s));
    mask_h = fl_net_ntohl(mask_be);
    prefix = 0u;
    while (mask_h & 0x80000000u) { prefix++; mask_h <<= 1; }
    fl_net_ipv4_format_addr(gw_be, gw_s, sizeof(gw_s));
    fl_color_success("fl0: %s/%u via %s", ip_s, (unsigned)prefix, gw_s);
    fl_net_iface_refresh();
    return 0;
}

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
        fl_color_error("usage: server <host|join|interfaces|netinit|msg|file|send|announce|nick|set-nick|connected|leave|kill> ...");
        return 1;
    }
    if (!strcmp(argv[1], "interfaces")) return verb_interfaces(argc, argv);
    if (!strcmp(argv[1], "netinit"))   return verb_netinit();
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
