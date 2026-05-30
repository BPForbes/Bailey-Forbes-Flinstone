/*
 * cmd_net_tools.c — common-distro networking shell verbs that route only
 * through internal Flinstone APIs (no arpa/inet.h, no libc DNS calls).
 *
 *   arp [-a | -s <ip> <mac> | -d <ip>]      ARP cache read / static add / delete
 *   ifconfig                                 List loopback + TAP drivers + counters
 *   route                                    Dump the longest-prefix routing table
 *   netstat [-u]                             UDP demux bound ports
 *   nslookup <host>                          Resolve hostname via fl_net_resolve_ipv4
 *   netsh <verb> ...                         Windows-style umbrella -> arp/ifconfig/route/netstat/nslookup
 *
 * Every formatter uses fl_net_ipv4_format_addr (in-tree) and the bit-shift
 * helpers in net_endian.h. MAC parsing and formatting are also in-tree
 * (no libc dependency beyond strtol).
 */

#include "cmd_decl.h"
#include "cmd_batch.h"

#include "net_arp.h"
#include "net_dns.h"
#include "net_ipv4.h"
#include "net_netdev.h"
#include "net_route.h"
#include "net_udp.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------------- */
/* Local helpers                                                             */
/* ------------------------------------------------------------------------- */

/* Parse "aa:bb:cc:dd:ee:ff" into 6 bytes. Returns 0 on success, -1 on
 * malformed input. Accepts ':' or '-' as separators. */
static int parse_mac(const char *s, uint8_t out[6]) {
    unsigned i;
    if (!s || !out)
        return -1;
    for (i = 0; i < 6u; i++) {
        char *end = NULL;
        long v;
        if (!s[0])
            return -1;
        errno = 0;
        v = strtol(s, &end, 16);
        if (errno != 0 || end == s || v < 0 || v > 0xFF)
            return -1;
        out[i] = (uint8_t)v;
        s = end;
        if (i < 5u) {
            if (*s != ':' && *s != '-')
                return -1;
            s++;
        }
    }
    return *s == '\0' ? 0 : -1;
}

static void format_mac(const uint8_t mac[6], char *out, size_t cap) {
    if (cap == 0)
        return;
    if (cap < 18u) {
        out[0] = '\0';
        return;
    }
    (void)snprintf(out, cap, "%02x:%02x:%02x:%02x:%02x:%02x",
                   mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

static const char *iface_name(const fl_net_driver_t *drv) {
    if (drv && drv == fl_net_netdev_loopback())
        return "lo";
    if (drv && drv == fl_net_netdev_tap())
        return "tap0";
    return drv ? "drv" : "(none)";
}

/* ------------------------------------------------------------------------- */
/* arp                                                                       */
/* ------------------------------------------------------------------------- */

static int arp_print_cache(void) {
    fl_net_arp_cache_row_t rows[FL_NET_ARP_CACHE_MAX];
    unsigned n = fl_net_arp_cache_snapshot(rows, FL_NET_ARP_CACHE_MAX);
    unsigned now = fl_net_arp_tick_now();
    char ip[16];
    char mac[18];
    if (n == 0u) {
        puts("arp: cache empty");
        return 0;
    }
    printf("%-16s %-17s %s\n", "Address", "HWaddress", "AgeTicks");
    for (unsigned i = 0; i < n; i++) {
        fl_net_ipv4_format_addr(rows[i].ip_be, ip, sizeof(ip));
        format_mac(rows[i].mac, mac, sizeof(mac));
        printf("%-16s %-17s %u\n", ip, mac,
               (unsigned)(now - rows[i].age_ticks));
    }
    return 0;
}

int cmd_arp_run(int argc, char **argv) {
    if (argc < 2 || !strcmp(argv[1], "-a")) {
        return arp_print_cache();
    }
    if (!strcmp(argv[1], "-s") && argc == 4) {
        uint32_t ip_be = 0u;
        uint8_t mac[6];
        if (fl_net_ipv4_parse_literal(argv[2], &ip_be) == 0) {
            fprintf(stderr, "arp: invalid IPv4 '%s'\n", argv[2]);
            return 1;
        }
        if (parse_mac(argv[3], mac) != 0) {
            fprintf(stderr, "arp: invalid MAC '%s'\n", argv[3]);
            return 1;
        }
        if (fl_net_arp_cache_insert(ip_be, mac) != FL_RESULT_OK) {
            fprintf(stderr, "arp: insert failed\n");
            return 1;
        }
        printf("arp: %s -> %s added\n", argv[2], argv[3]);
        return 0;
    }
    if (!strcmp(argv[1], "-d") && argc == 3) {
        uint32_t ip_be = 0u;
        fl_result_t rc;
        if (fl_net_ipv4_parse_literal(argv[2], &ip_be) == 0) {
            fprintf(stderr, "arp: invalid IPv4 '%s'\n", argv[2]);
            return 1;
        }
        rc = fl_net_arp_cache_delete(ip_be);
        if (rc == FL_RESULT_NOENT) {
            fprintf(stderr, "arp: %s not in cache\n", argv[2]);
            return 1;
        }
        if (rc != FL_RESULT_OK) {
            fprintf(stderr, "arp: delete failed (rc=%d)\n", (int)rc);
            return 1;
        }
        printf("arp: %s removed\n", argv[2]);
        return 0;
    }
    fprintf(stderr,
            "arp: usage: arp [-a | -s <ip> <mac> | -d <ip>]\n");
    return 1;
}

__attribute__((used))
int cmd_arp_batch_tokens_count(int argc, char **argv, int i) {
    int used = 1;
    int j = i + 1;
    if (j < argc) {
        if (!strcmp(argv[j], "-a")) {
            return used + 1;
        }
        if (!strcmp(argv[j], "-s") && j + 2 < argc) {
            return used + 3;
        }
        if (!strcmp(argv[j], "-d") && j + 1 < argc) {
            return used + 2;
        }
    }
    return used;
}

/* ------------------------------------------------------------------------- */
/* ifconfig                                                                  */
/* ------------------------------------------------------------------------- */

static void ifconfig_print_driver(const char *name, fl_net_driver_t *drv) {
    fl_net_netdev_stats_t stats = {0};
    if (!drv) {
        printf("%-6s status: down\n", name);
        return;
    }
    fl_net_netdev_stats(drv, &stats);
    printf("%-6s mtu %u  tx %llu  rx %llu\n",
           name,
           drv->mtu ? drv->mtu : 1500u,
           (unsigned long long)stats.tx_frames,
           (unsigned long long)stats.rx_frames);
}

int cmd_ifconfig_run(int argc, char **argv) {
    (void)argc;
    (void)argv;
    ifconfig_print_driver("lo", fl_net_netdev_loopback());
    if (fl_net_netdev_tap_is_open()) {
        ifconfig_print_driver("tap0", fl_net_netdev_tap());
    } else {
        const char *err = fl_net_netdev_tap_last_error();
        if (err && err[0])
            printf("tap0   status: closed  (%s)\n", err);
        else
            printf("tap0   status: closed\n");
    }
    return 0;
}

__attribute__((used))
int cmd_ifconfig_batch_tokens_count(int argc, char **argv, int i) {
    (void)argc;
    (void)argv;
    (void)i;
    return 1;
}

/* ------------------------------------------------------------------------- */
/* route                                                                     */
/* ------------------------------------------------------------------------- */

int cmd_route_run(int argc, char **argv) {
    fl_net_route_entry_t rows[FL_NET_ROUTE_TABLE_MAX];
    unsigned n;
    char dest[16];
    char gw[16];
    char src[16];
    (void)argc;
    (void)argv;
    n = fl_net_route_snapshot(rows, FL_NET_ROUTE_TABLE_MAX);
    if (n == 0u) {
        puts("route: table empty");
        return 0;
    }
    printf("%-19s %-15s %-15s %s\n",
           "Destination", "Gateway", "Source", "Iface");
    for (unsigned i = 0; i < n; i++) {
        char dest_pfx[20];
        fl_net_ipv4_format_addr(rows[i].addr_be, dest, sizeof(dest));
        fl_net_ipv4_format_addr(rows[i].gw_be, gw, sizeof(gw));
        fl_net_ipv4_format_addr(rows[i].src_ip_be, src, sizeof(src));
        (void)snprintf(dest_pfx, sizeof(dest_pfx), "%s/%u",
                       dest, (unsigned)rows[i].prefix_len);
        printf("%-19s %-15s %-15s %s\n",
               dest_pfx, gw, src, iface_name(rows[i].drv));
    }
    return 0;
}

__attribute__((used))
int cmd_route_batch_tokens_count(int argc, char **argv, int i) {
    (void)argc;
    (void)argv;
    (void)i;
    return 1;
}

/* ------------------------------------------------------------------------- */
/* netstat                                                                   */
/* ------------------------------------------------------------------------- */

int cmd_netstat_run(int argc, char **argv) {
    uint16_t ports[64];
    unsigned n;
    int udp_only = 0;
    /* Accept the documented `-u` / `--udp` flag (UDP-only summary; today
     * the only protocol we expose anyway, but the flag keeps the surface
     * stable as we add TCP/socket listings). Unknown options error out
     * so the batch token counter and the runtime stay aligned. */
    for (int k = 1; k < argc; k++) {
        if (!strcmp(argv[k], "-u") || !strcmp(argv[k], "--udp"))
            udp_only = 1;
        else {
            fprintf(stderr, "netstat: unknown option '%s' (usage: netstat [-u])\n",
                    argv[k]);
            return 1;
        }
    }
    (void)udp_only; /* reserved for future protocol gating */
    n = fl_net_udp_bound_ports_snapshot(ports, (unsigned)(sizeof(ports) /
                                                          sizeof(ports[0])));
    printf("Proto Local-Port  State\n");
    if (n == 0u) {
        puts("udp     (none)     -");
    } else {
        for (unsigned i = 0; i < n; i++)
            printf("udp     %-9u  BOUND\n", (unsigned)ports[i]);
    }
    return 0;
}

__attribute__((used))
int cmd_netstat_batch_tokens_count(int argc, char **argv, int i) {
    int used = 1;
    int j = i + 1;
    /* Consume any leading option tokens (starting with '-') so batch
     * dispatch doesn't leave unknown flags like "-x" dangling as the
     * next command, allowing cmd_netstat_run() to report invalid flags. */
    while (j < argc) {
        if (argv[j][0] == '-') {
            used++;
            j++;
            continue;
        }
        break;
    }
    return used;
}

/* ------------------------------------------------------------------------- */
/* nslookup                                                                  */
/* ------------------------------------------------------------------------- */

int cmd_nslookup_run(int argc, char **argv) {
    uint32_t addr_be = 0u;
    char resolved[16];
    fl_result_t rc;
    if (argc != 2) {
        fprintf(stderr, "nslookup: usage: nslookup <host>\n");
        return 1;
    }
    rc = fl_net_resolve_ipv4(argv[1], &addr_be, resolved, sizeof(resolved));
    if (rc != FL_RESULT_OK) {
        fprintf(stderr, "nslookup: '%s' did not resolve (rc=%d)\n",
                argv[1], (int)rc);
        return 1;
    }
    printf("Name:    %s\nAddress: %s\n", argv[1], resolved);
    return 0;
}

__attribute__((used))
int cmd_nslookup_batch_tokens_count(int argc, char **argv, int i) {
    int used = 1;
    (void)argv;
    if (i + 1 < argc)
        used++;
    return used;
}

/* ------------------------------------------------------------------------- */
/* netsh — Windows-style umbrella that dispatches to the above               */
/* ------------------------------------------------------------------------- */

int cmd_netsh_run(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr,
                "netsh: usage: netsh <arp|ifconfig|route|netstat|nslookup> [args]\n");
        return 1;
    }
    if (!strcmp(argv[1], "arp"))
        return cmd_arp_run(argc - 1, argv + 1);
    if (!strcmp(argv[1], "ifconfig"))
        return cmd_ifconfig_run(argc - 1, argv + 1);
    if (!strcmp(argv[1], "route"))
        return cmd_route_run(argc - 1, argv + 1);
    if (!strcmp(argv[1], "netstat"))
        return cmd_netstat_run(argc - 1, argv + 1);
    if (!strcmp(argv[1], "nslookup"))
        return cmd_nslookup_run(argc - 1, argv + 1);
    fprintf(stderr, "netsh: unknown sub-verb '%s'\n", argv[1]);
    return 1;
}

__attribute__((used))
int cmd_netsh_batch_tokens_count(int argc, char **argv, int i) {
    int used = 1;
    int j = i + 1;
    if (j >= argc)
        return used;
    used++; /* sub-verb token */
    if (!strcmp(argv[j], "arp"))
        return used + cmd_arp_batch_tokens_count(argc, argv, j) - 1;
    if (!strcmp(argv[j], "netstat"))
        return used + cmd_netstat_batch_tokens_count(argc, argv, j) - 1;
    if (!strcmp(argv[j], "nslookup")) {
        if (j + 1 < argc)
            used++;
        return used;
    }
    /* ifconfig / route are bare verbs (no extra tokens) */
    return used;
}
