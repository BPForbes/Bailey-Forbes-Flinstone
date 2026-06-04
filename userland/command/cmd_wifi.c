#include "cmd_decl.h"
#include "cmd_batch.h"
#include "fl/authz_subsystem.h"
#include "contract_p2_authz.h"
#include "net_wifi_db.h"
#include "net_wifi_host_linux.h"
#include "net_wifi_station.h"

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

static int wifi_usage(void) {
    fputs("Usage:\n"
          "  wifi scan [-band any|2|5|6]\n"
          "  wifi join <name> [password]\n"
          "  wifi known\n"
          "  wifi status\n",
          stderr);
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
    const char *name;
    const char *password = "";
    fl_net_wifi_cred_t cred;
    fl_net_wifi_scan_entry_t entries[16];
    size_t count = 0;
    size_t i;
    fl_result_t rc;

    if (argc < 3) {
        fputs("wifi join: missing network name\n", stderr);
        return 1;
    }
    name = argv[2];
    if (argc >= 4)
        password = argv[3];
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
    memset(&cred, 0, sizeof(cred));
    strncpy(cred.ssid, name, sizeof(cred.ssid) - 1u);
    if (password[0]) {
        strncpy(cred.passphrase, password, sizeof(cred.passphrase) - 1u);
        if (fl_wifi_db_set_password(name, password) != FL_RESULT_OK) {
            fputs("wifi join: could not store credential hash\n", stderr);
            fl_wifi_db_close();
            return 1;
        }
    }
    for (i = 0; i < count; i++) {
        if (!strcmp(entries[i].ssid, name)) {
            cred.auth_mode = entries[i].auth_mode;
            cred.band_hint = entries[i].band;
            memcpy(cred.bssid, entries[i].bssid, 6);
            (void)fl_wifi_db_apply_scan_entry(name, &entries[i]);
            break;
        }
    }
    if (cred.auth_mode == 0 && password[0])
        cred.auth_mode = FL_WIFI_AUTH_WPA3_SAE;
    rc = fl_net_wifi_connect(&cred, 15000u);
    if (rc == FL_RESULT_NOSYS) {
        fputs("wifi join: no Wi-Fi NIC driver (profile saved if password given)\n", stderr);
        fl_wifi_db_close();
        return 1;
    }
    if (rc == FL_RESULT_NOENT) {
        fprintf(stderr, "wifi join: network '%s' not found in last scan\n", name);
        fl_wifi_db_close();
        return 1;
    }
    if (rc == FL_RESULT_INVAL) {
        fputs("wifi join: password required for secured network\n", stderr);
        fl_wifi_db_close();
        return 1;
    }
    if (rc != FL_RESULT_OK) {
        fprintf(stderr, "wifi join: failed (%d)\n", (int)rc);
        fl_wifi_db_close();
        return 1;
    }
    (void)fl_wifi_db_mark_joined(name, 1);
    fl_net_wifi_cred_scrub_passphrase(&cred);
    printf("wifi join: associated with '%s' (state %d", name, (int)fl_net_wifi_state());
    if (fl_net_wifi_host_linux_available()) {
        char ip[32];
        if (fl_net_wifi_host_linux_ipv4(NULL, ip, sizeof(ip)) == FL_RESULT_OK)
            printf(", ip %s — peers: server join %s:<port>", ip, ip);
    } else if (fl_net_wifi_station_netdev() != NULL) {
        fputs(", loopback netdev UP", stdout);
    }
    puts(")");
    fl_wifi_db_close();
    return 0;
}

static int cmd_wifi_status(int argc, char **argv) {
    char ip[32];
    const char *iface;

    (void)argc;
    (void)argv;
    printf("Wi-Fi state: %d\n", (int)fl_net_wifi_state());
    if (fl_net_wifi_host_linux_available()) {
        iface = fl_net_wifi_host_linux_iface();
        printf("Backend: wpa_supplicant (%s)\n", iface);
        if (fl_net_wifi_host_linux_ipv4(NULL, ip, sizeof(ip)) == FL_RESULT_OK)
            printf("IPv4: %s (server host %s:<port> or server host -all <port>)\n", ip,
                   ip);
        else
            puts("IPv4: (none yet — wait for DHCP or check wpa_supplicant)");
    } else {
        puts("Backend: in-tree lab simulation (set FL_NET_WIFI_USE_WPA=1 with wpa_cli for WLAN)");
        if (fl_net_wifi_station_netdev() != NULL)
            puts("Lab netdev: loopback stand-in only");
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
        if (j + 1 < argc)
            used++;
        if (j + 2 < argc && argv[j + 2][0] != '-')
            used++;
        return used;
    }
    if (!strcmp(argv[j], "known"))
        return used;
    if (!strcmp(argv[j], "status"))
        return used;
    return used;
}
