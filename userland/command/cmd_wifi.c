#include "cmd_decl.h"
#include "cmd_batch.h"
#include "cmd_authutil.h"
#include "fl/authz_subsystem.h"
#include "fl/session.h"
#include "contract_p2_authz.h"
#include "net_wifi_db.h"
#include "net_ipv4.h"
#include "net_ipv6.h"
#include "net_wifi_netdev.h"
#include "net_wifi_host_linux.h"
#include "net_wifi_station.h"

#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>

static const char *auth_mode_name(uint8_t mode) {
    switch (mode) {
    case FL_WIFI_AUTH_OPEN:
        return "open";
    case FL_WIFI_AUTH_OWE:
        return "owe";
    case FL_WIFI_AUTH_WPA2_PSK:
        return "wpa2";
    case FL_WIFI_AUTH_WPA3_SAE:
        return "wpa3";
    case FL_WIFI_AUTH_8021X:
        return "8021x";
    default:
        return "?";
    }
}

static const char *band_name(uint8_t band) {
    switch (band) {
    case FL_WIFI_BAND_2GHZ:
        return "2.4";
    case FL_WIFI_BAND_5GHZ:
        return "5";
    case FL_WIFI_BAND_6GHZ:
        return "6";
    default:
        return "any";
    }
}

/* In-tree station IPv4 for peer hints (lab netdev or host bind address). */
static int wifi_peer_ipv4(char *buf, size_t cap, uint32_t *be_out) {
    if (fl_net_wifi_netdev_is_up()) {
        uint32_t nd = 0u;
        if (fl_net_wifi_netdev_ipv4(&nd) == FL_RESULT_OK && nd != 0u) {
            if (be_out)
                *be_out = nd;
            if (buf && cap > 0u)
                fl_net_ipv4_format_addr(nd, buf, cap);
            return 1;
        }
    }
    return 0;
}

static int wifi_usage(void) {
    fputs("Usage:\n"
          "  wifi scan [-band any|2|5|6]\n"
          "  wifi join <ssid>              Join by network name (prompts for WiFi password)\n"
          "  wifi join -b <bssid> <ssid>  Pin to a specific AP by MAC address\n"
          "  wifi leave                   Disconnect and drop WLAN addresses\n"
          "  wifi known\n"
          "  wifi status\n"
          "  Lab scan: set FL_NET_WIFI_HOME_SSID to include your home network in scan results.\n"
          "  Default: in-tree 802.11 lab (LabAxHome/GuestOpen; DHCP on wlan-lab).\n"
          "  Real Wi-Fi (opt-in): set FL_NET_WIFI_FLINSTONE_PS on WSL, or FL_NET_WIFI_USE_WPA=1\n"
          "    on native Linux with wpa_cli/nmcli. See README.md and docs/P3_NETWORKING.md.\n",
          stderr);
    return 1;
}

static int parse_bssid_arg(const char *s, uint8_t out[6]) {
    unsigned o[6];
    if (!s || !out)
        return 0;
    if (sscanf(s, "%x:%x:%x:%x:%x:%x", &o[0], &o[1], &o[2], &o[3], &o[4], &o[5]) != 6)
        return 0;
    for (int i = 0; i < 6; i++)
        out[i] = (uint8_t)o[i];
    return 1;
}

static uint8_t parse_band(const char *s) {
    if (!s || !s[0] || !strcmp(s, "any"))
        return FL_WIFI_BAND_ANY;
    if (!strcmp(s, "2") || !strcmp(s, "2.4"))
        return FL_WIFI_BAND_2GHZ;
    if (!strcmp(s, "5"))
        return FL_WIFI_BAND_5GHZ;
    if (!strcmp(s, "6"))
        return FL_WIFI_BAND_6GHZ;
    return FL_WIFI_BAND_ANY;
}

static int cmd_wifi_scan(int argc, char **argv) {
    uint8_t band = FL_WIFI_BAND_ANY;
    fl_net_wifi_scan_entry_t entries[16];
    size_t count = 0;
    size_t i;
    fl_result_t rc;
    int a;

    for (a = 2; a < argc; a++) {
        if (!strcmp(argv[a], "-band") && a + 1 < argc) {
            band = parse_band(argv[++a]);
            continue;
        }
        fprintf(stderr, "wifi scan: unknown option '%s'\n", argv[a]);
        return 1;
    }
    if (fl_authz_subsystem_check((unsigned)FL_AUTHZ_OP_NETDEV_IO, NULL) ==
        FL_AUTHZ_DENY) {
        fputs("wifi scan: permission denied (netdev I/O)\n", stderr);
        return 1;
    }
    if (fl_wifi_db_open(NULL) != FL_RESULT_OK) {
        fputs("wifi scan: could not open profile database\n", stderr);
        return 1;
    }
    (void)fl_net_wifi_station_init();
    rc = fl_net_wifi_scan(band, 3000u);
    if (rc == FL_RESULT_NOSYS) {
        fputs("wifi scan: no Wi-Fi NIC driver (P4 backend pending)\n", stderr);
        fl_wifi_db_close();
        return 1;
    }
    if (rc != FL_RESULT_OK) {
        fprintf(stderr, "wifi scan: failed (%d)\n", (int)rc);
        fl_wifi_db_close();
        return 1;
    }
    rc = fl_net_wifi_scan_result(entries, 16, &count);
    if (rc != FL_RESULT_OK) {
        fprintf(stderr, "wifi scan: results failed (%d)\n", (int)rc);
        fl_wifi_db_close();
        return 1;
    }
    if (fl_net_wifi_station_lab_backend())
        fputs("wifi scan: in-tree lab simulation (802.11ax APs)\n", stderr);
    else if (fl_net_wifi_host_linux_opted_in() && fl_net_wifi_host_linux_available()) {
        const char *backend = fl_net_wifi_host_linux_backend_name();
        const char *iface = fl_net_wifi_host_linux_iface();
        fprintf(stderr, "wifi scan: host backend %s on %s\n",
                backend ? backend : "host", iface ? iface : "?");
    } else
        fputs("wifi scan: hardware Wi-Fi driver backend\n", stderr);
    printf("SSID            BSSID          RSSI  CH  BW  Band  Auth  HE  Color\n");
    for (i = 0; i < count; i++) {
        const fl_net_wifi_scan_entry_t *e = &entries[i];
        printf("%-15s %02x:%02x:%02x:%02x:%02x:%02x  %4d  %3u %3u  %-4s  %-5s %c   %u\n",
               e->ssid, e->bssid[0], e->bssid[1], e->bssid[2], e->bssid[3], e->bssid[4],
               e->bssid[5], (int)e->rssi_dbm, (unsigned)e->channel,
               (unsigned)e->channel_width_mhz, band_name(e->band),
               auth_mode_name(e->auth_mode), e->he_supported ? 'Y' : 'n', (unsigned)e->bss_color);
    }
    if (count == 0u)
        puts("(no networks seen)");
    fl_wifi_db_close();
    return 0;
}

static int cmd_wifi_join(int argc, char **argv) {
    const char *name = NULL;
    const char *bssid_arg = NULL;
    char wifi_pw[64];
    fl_net_wifi_cred_t cred;
    fl_net_wifi_scan_entry_t entries[16];
    size_t count = 0;
    size_t i;
    int a;
    fl_result_t rc;

    memset(wifi_pw, 0, sizeof(wifi_pw));

    for (a = 2; a < argc; a++) {
        if (!strcmp(argv[a], "-b") && a + 1 < argc) {
            bssid_arg = argv[++a];
            continue;
        }
        if (!name) {
            name = argv[a];
            continue;
        }
        fprintf(stderr, "wifi join: unexpected argument '%s'\n", argv[a]);
        return wifi_usage();
    }
    if (!name) {
        fputs("wifi join: missing network name\n", stderr);
        return 1;
    }
    if (fl_authz_subsystem_check((unsigned)FL_AUTHZ_OP_NETDEV_IO, NULL) ==
        FL_AUTHZ_DENY) {
        fputs("wifi join: permission denied (netdev I/O)\n", stderr);
        return 1;
    }
    if (fl_wifi_db_open(NULL) != FL_RESULT_OK) {
        fputs("wifi join: could not open profile database\n", stderr);
        return 1;
    }
    (void)fl_net_wifi_station_init();
    (void)fl_net_wifi_scan(FL_WIFI_BAND_ANY, 3000u);
    (void)fl_net_wifi_scan_result(entries, 16, &count);

    /* If the session carries sudo elevation, re-authenticate before joining. */
    if (fl_session_has_elevation() && !fl_session_is_elevated_account()) {
        char sudo_pw[128];
        int auth_ok;
        memset(sudo_pw, 0, sizeof(sudo_pw));
        if (cmd_read_password("Sudo Password: ", sudo_pw, sizeof(sudo_pw)) != 0) {
            fputs("wifi join: sudo password read failed\n", stderr);
            fl_wifi_db_close();
            return 1;
        }
        auth_ok = fl_session_verify_password(sudo_pw);
        cmd_wipe_password(sudo_pw, sizeof(sudo_pw));
        if (!auth_ok) {
            fputs("wifi join: sudo authentication failed\n", stderr);
            fl_wifi_db_close();
            return 1;
        }
    }

    /* Skip the password prompt for networks previously joined — the OS backend
     * (wpa_supplicant / nmcli / FlinstonePowershell) already has the credential
     * saved.  If the saved profile has expired or been cleared the connect call
     * returns FL_RESULT_INVAL and we fall back to prompting. */
    {
        fl_wifi_db_router_t known_row;
        int is_known = (fl_wifi_db_find(name, &known_row) == FL_RESULT_OK &&
                        known_row.password_hash[0] &&
                        strcmp(known_row.password_hash, "-") != 0);
        if (!is_known) {
            if (cmd_read_password("WiFi Password: ", wifi_pw, sizeof(wifi_pw)) != 0) {
                fputs("wifi join: password read failed\n", stderr);
                fl_wifi_db_close();
                return 1;
            }
        } else {
            printf("wifi join: '%s' is a known network\n", name);
        }
    }

    memset(&cred, 0, sizeof(cred));
    strncpy(cred.ssid, name, sizeof(cred.ssid) - 1u);
    if (bssid_arg && !parse_bssid_arg(bssid_arg, cred.bssid)) {
        fprintf(stderr, "wifi join: invalid BSSID '%s' (use aa:bb:cc:dd:ee:ff)\n", bssid_arg);
        cmd_wipe_password(wifi_pw, sizeof(wifi_pw));
        fl_wifi_db_close();
        return 1;
    }
    if (wifi_pw[0]) {
        strncpy(cred.passphrase, wifi_pw, sizeof(cred.passphrase) - 1u);
        if (fl_wifi_db_set_password(name, wifi_pw) != FL_RESULT_OK) {
            fputs("wifi join: could not store credential hash\n", stderr);
            fl_net_wifi_cred_scrub_passphrase(&cred);
            cmd_wipe_password(wifi_pw, sizeof(wifi_pw));
            fl_wifi_db_close();
            return 1;
        }
    }
    cmd_wipe_password(wifi_pw, sizeof(wifi_pw));

    for (i = 0; i < count; i++) {
        if (strcmp(entries[i].ssid, name))
            continue;
        if (bssid_arg && memcmp(entries[i].bssid, cred.bssid, 6) != 0)
            continue;
        cred.auth_mode = entries[i].auth_mode;
        cred.band_hint = entries[i].band;
        if (!bssid_arg)
            memcpy(cred.bssid, entries[i].bssid, 6);
        (void)fl_wifi_db_apply_scan_entry(name, &entries[i]);
        break;
    }
    /* Default to WPA2-PSK when auth mode is unknown — most home routers are
     * WPA2.  On the host path (nmcli/wpa_supplicant) this is overridden by the
     * AP's actual capabilities; on the lab path it sets the synthetic AP mode. */
    if (cred.auth_mode == 0 && cred.passphrase[0])
        cred.auth_mode = FL_WIFI_AUTH_WPA2_PSK;
    rc = fl_net_wifi_connect(&cred, 30000u);
    /* If the OS backend no longer has saved credentials (profile cleared or
     * first join on a new machine), fall back to prompting for the password. */
    if (rc == FL_RESULT_INVAL && !cred.passphrase[0]) {
        fputs("wifi join: saved profile not found — enter password:\n", stderr);
        if (cmd_read_password("WiFi Password: ", wifi_pw, sizeof(wifi_pw)) != 0 ||
                !wifi_pw[0]) {
            cmd_wipe_password(wifi_pw, sizeof(wifi_pw));
            fl_wifi_db_close();
            return 1;
        }
        strncpy(cred.passphrase, wifi_pw, sizeof(cred.passphrase) - 1u);
        if (cred.auth_mode == 0)
            cred.auth_mode = FL_WIFI_AUTH_WPA2_PSK;
        (void)fl_wifi_db_set_password(name, wifi_pw);
        cmd_wipe_password(wifi_pw, sizeof(wifi_pw));
        rc = fl_net_wifi_connect(&cred, 30000u);
    }
    if (rc == FL_RESULT_NOSYS) {
        fputs("wifi join: no Wi-Fi backend (need wpa_supplicant on FL_NET_WIFI_IFACE or lab mode)\n", stderr);
        goto cleanup;
    }
    /* NOENT is structurally unreachable today: the lab path synthesises an AP
     * entry so connect never returns NOENT, and the host path returns ERR on
     * nmcli/wpa_cli failure.  Kept as a safety net for future backends. */
    if (rc == FL_RESULT_NOENT) {
        fprintf(stderr, "wifi join: network '%s' not found\n", name);
        goto cleanup;
    }
    if (rc == FL_RESULT_INVAL) {
        fputs("wifi join: password required for secured network\n", stderr);
        goto cleanup;
    }
    if (rc == FL_RESULT_TIMEDOUT) {
        const char *iface = fl_net_wifi_netdev_iface();
        fprintf(stderr, "wifi join: timed out waiting for association on %s\n", iface);
        goto cleanup;
    }
    if (rc != FL_RESULT_OK) {
        fprintf(stderr, "wifi join: failed (%d)\n", (int)rc);
        goto cleanup;
    }
    (void)fl_wifi_db_mark_joined(name, 1);
    fl_net_wifi_cred_scrub_passphrase(&cred);
    printf("wifi join: associated with '%s' (state %d", name, (int)fl_net_wifi_state());
    if (fl_net_wifi_netdev_is_up()) {
        char peer_ip[32];
        uint32_t peer_be = 0u;
        if (wifi_peer_ipv4(peer_ip, sizeof(peer_ip), &peer_be))
            printf(", %s %s", fl_net_wifi_netdev_iface(), peer_ip);
    } else if (fl_net_wifi_station_netdev() != NULL) {
        printf(", %s netdev UP", fl_net_wifi_netdev_iface());
    }
    puts(")");
    fl_wifi_db_close();
    return 0;
cleanup:
    fl_net_wifi_cred_scrub_passphrase(&cred);
    fl_wifi_db_close();
    return 1;
}


static int cmd_wifi_leave(int argc, char **argv) {
    fl_result_t rc;
    (void)argc;
    (void)argv;
    if (fl_authz_subsystem_check((unsigned)FL_AUTHZ_OP_NETDEV_IO, NULL) ==
        FL_AUTHZ_DENY) {
        fputs("wifi leave: permission denied (netdev I/O)\n", stderr);
        return 1;
    }
    (void)fl_net_wifi_station_init();
    rc = fl_net_wifi_disconnect();
    if (rc != FL_RESULT_OK) {
        fprintf(stderr, "wifi leave: failed (%d)\n", (int)rc);
        return 1;
    }
    if (fl_wifi_db_open(NULL) == FL_RESULT_OK) {
        fl_wifi_db_router_t rows[FL_WIFI_DB_MAX_ROUTERS];
        size_t count = 0;
        size_t i;
        if (fl_wifi_db_list(rows, FL_WIFI_DB_MAX_ROUTERS, &count) == FL_RESULT_OK) {
            for (i = 0; i < count; i++) {
                if (rows[i].is_joined)
                    (void)fl_wifi_db_mark_joined(rows[i].name, 0);
            }
        }
        fl_wifi_db_close();
    }
    puts("wifi leave: disconnected (WLAN IPv4/IPv6 removed from shell env)");
    return 0;
}

static int cmd_wifi_status(int argc, char **argv) {
    char ip[32];
    uint32_t ip_be = 0u;
    (void)argc;
    (void)argv;
    printf("Wi-Fi state: %d\n", (int)fl_net_wifi_state());
    if (fl_net_wifi_station_lab_backend())
        puts("Backend: in-tree 802.11 lab (wlan-lab DHCP)");
    else if (fl_net_wifi_station_host_backend()) {
        const char *backend = fl_net_wifi_host_linux_backend_name();
        char bind_ip[32];
        uint32_t bind_be = 0u;
        uint8_t prefix = 24u;

        printf("Backend: %s (%s)\n", backend ? backend : "host",
               fl_net_wifi_host_linux_iface());
        if (fl_net_wifi_host_linux_ipv4_route(&bind_be, &prefix, NULL) == FL_RESULT_OK &&
            bind_be != 0u) {
            fl_net_ipv4_format_addr(bind_be, bind_ip, sizeof(bind_ip));
            printf("Station L3 (host bind): %s/%u\n", bind_ip, (unsigned)prefix);
            printf("Server host: server host :<port> (WSL portproxy) or server host %s:<port>\n",
                   bind_ip);
            {
                const char *win = fl_net_wifi_host_linux_windows_ipv4();
                if (win && win[0])
                    printf("LAN peers join: %s:<port> after server host sets portproxy\n", win);
            }
        } else {
            puts("Station L3: waiting for host DHCP (wifi status again in a moment)");
        }
    } else if (fl_net_wifi_host_linux_opted_in() && fl_net_wifi_host_linux_available()) {
        const char *backend = fl_net_wifi_host_linux_backend_name();
        printf("Backend: %s available on %s (not associated)\n",
               backend ? backend : "host", fl_net_wifi_host_linux_iface());
    } else
        puts("Backend: in-tree 802.11 lab (not associated)");
    {
        const char *ifname = fl_net_wifi_netdev_iface();
        if (fl_net_wifi_netdev_is_up() &&
            fl_net_wifi_netdev_ipv4(&ip_be) == FL_RESULT_OK) {
            char ip6[64];
            char peer_ip[32];
            char gw_ip[32];
            char dns_ip[32];
            char mask_ip[32];
            fl_net_wifi_l3_profile_t l3;
            uint8_t addr6[16];
            uint8_t p6 = 0u;
            unsigned prefix = 24u;

            fl_net_ipv4_format_addr(ip_be, ip, sizeof(ip));
            if (fl_net_wifi_netdev_l3_profile(&l3)) {
                if (l3.netmask != 0u) {
                    unsigned bits = 0u;
                    uint32_t m = ntohl(l3.netmask);
                    while (m & 0x80000000u) {
                        bits++;
                        m <<= 1;
                    }
                    prefix = bits > 0u ? bits : 24u;
                }
                printf("Station L3 (%s): %s/%u", ifname, ip, prefix);
                if (l3.gateway != 0u) {
                    fl_net_ipv4_format_addr(l3.gateway, gw_ip, sizeof(gw_ip));
                    printf("  gateway %s", gw_ip);
                }
                if (l3.dns != 0u) {
                    fl_net_ipv4_format_addr(l3.dns, dns_ip, sizeof(dns_ip));
                    printf("  dns %s", dns_ip);
                }
                if (l3.netmask != 0u) {
                    fl_net_ipv4_format_addr(l3.netmask, mask_ip, sizeof(mask_ip));
                    printf("  mask %s", mask_ip);
                }
                putchar('\n');
            } else {
                printf("Station L3 (%s): %s\n", ifname, ip);
            }
            if (wifi_peer_ipv4(peer_ip, sizeof(peer_ip), NULL))
                printf("Server host/join: %s:<port> or server host :<port>\n", peer_ip);
            if (fl_net_wifi_netdev_ipv6(addr6, &p6) == FL_RESULT_OK &&
                fl_net_ipv6_format_addr(addr6, ip6, sizeof(ip6)))
                printf("Station IPv6: %s/%u\n", ip6, (unsigned)p6);
        } else if (fl_net_wifi_station_netdev() != NULL) {
            printf("Interface %s: associating…\n", ifname);
        } else {
            printf("Interface %s: down (wifi join to associate)\n", ifname);
        }
    }
    return 0;
}

static int cmd_wifi_known(int argc, char **argv) {
    fl_wifi_db_router_t rows[FL_WIFI_DB_MAX_ROUTERS];
    size_t count = 0;
    size_t i;

    (void)argc;
    (void)argv;
    if (fl_wifi_db_open(NULL) != FL_RESULT_OK) {
        fputs("wifi known: could not open profile database\n", stderr);
        return 1;
    }
    if (fl_wifi_db_list(rows, FL_WIFI_DB_MAX_ROUTERS, &count) != FL_RESULT_OK) {
        fputs("wifi known: list failed\n", stderr);
        fl_wifi_db_close();
        return 1;
    }
    printf("Name            BSSID          Band  Ch  Auth  Joined  HE  Last-join\n");
    for (i = 0; i < count; i++) {
        const fl_wifi_db_router_t *r = &rows[i];
        printf("%-15s %-12s %-4s %3u %-5s %s     %c   %llu\n", r->name,
               r->bssid_hex[0] ? r->bssid_hex : "(none)", band_name(r->band),
               (unsigned)r->channel, auth_mode_name(r->auth_mode),
               r->is_joined ? "yes" : "no", r->he_supported ? 'Y' : 'n',
               (unsigned long long)r->last_joined_at);
    }
    if (count == 0u)
        puts("(no saved Wi-Fi profiles)");
    fl_wifi_db_close();
    return 0;
}

int cmd_wifi_run(int argc, char **argv) {
    if (argc < 2)
        return wifi_usage();
    if (!strcmp(argv[1], "scan"))
        return cmd_wifi_scan(argc, argv);
    if (!strcmp(argv[1], "join"))
        return cmd_wifi_join(argc, argv);
    if (!strcmp(argv[1], "known"))
        return cmd_wifi_known(argc, argv);
    if (!strcmp(argv[1], "leave"))
        return cmd_wifi_leave(argc, argv);
    if (!strcmp(argv[1], "status"))
        return cmd_wifi_status(argc, argv);
    fprintf(stderr, "wifi: unknown subcommand '%s'\n", argv[1]);
    return wifi_usage();
}

__attribute__((used))
int cmd_wifi_batch_tokens_count(int argc, char **argv, int i) {
    int used = 1;
    int j = i + 1;

    (void)argc;
    if (j >= argc)
        return used;
    used++;
    if (!strcmp(argv[j], "scan")) {
        while (j + 1 < argc && argv[j + 1][0] == '-')
            used += 2, j += 2;
        return used;
    }
    if (!strcmp(argv[j], "join")) {
        int k = j + 1;
        while (k < argc && argv[k][0] == '-') {
            if (!strcmp(argv[k], "-b") && k + 1 < argc)
                k += 2;
            else
                k++;
        }
        if (k < argc)
            used += (k - j);
        /* password is now prompted interactively, not a batch token */
        return used;
    }
    if (!strcmp(argv[j], "known"))
        return used;
    if (!strcmp(argv[j], "leave"))
        return used;
    if (!strcmp(argv[j], "status"))
        return used;
    return used;
}
