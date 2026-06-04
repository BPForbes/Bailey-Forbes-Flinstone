#include "net_wifi_host_linux.h"

#include "net_host_iface.h"
#include "net_ipv4.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__linux__)
#define FL_NET_WIFI_HOST_LINUX 1
#endif

#if defined(FL_NET_WIFI_HOST_LINUX)
static char s_wifi_iface[FL_NET_HOST_IFACE_NAME_MAX] = "wlan0";
static char s_wpa_cli[128] = "wpa_cli";
static fl_net_wifi_scan_entry_t s_wpa_scan[32];
static size_t s_wpa_scan_count;
static char s_wpa_joined_ssid[FL_WIFI_SSID_MAX];
static int s_wpa_probed;

static int env_truthy(const char *v) {
    return v && (v[0] == '1' || v[0] == 'y' || v[0] == 'Y' || v[0] == 't' || v[0] == 'T');
}

static int env_falsy(const char *v) {
    return v && (v[0] == '0' || v[0] == 'n' || v[0] == 'N' || v[0] == 'f' || v[0] == 'F');
}

static void load_env_once(void) {
    const char *iface;
    const char *cli;
    if (s_wpa_probed)
        return;
    s_wpa_probed = 1;
    iface = getenv("FL_NET_WIFI_IFACE");
    if (iface && iface[0])
        strncpy(s_wifi_iface, iface, sizeof(s_wifi_iface) - 1u);
    cli = getenv("FL_NET_WIFI_WPA_CLI");
    if (cli && cli[0])
        strncpy(s_wpa_cli, cli, sizeof(s_wpa_cli) - 1u);
}

static int wpa_use_requested(void) {
    const char *v = getenv("FL_NET_WIFI_USE_WPA");
    load_env_once();
    if (env_falsy(v))
        return 0;
    if (env_truthy(v))
        return 1;
    return -1; /* auto */
}

static int run_wpa_cli(const char *cmd, char *out, size_t out_cap) {
    char line[512];
    FILE *fp;

    if (!cmd)
        return 0;
    snprintf(line, sizeof(line), "%s -i %s %s 2>/dev/null", s_wpa_cli, s_wifi_iface, cmd);
    fp = popen(line, "r");
    if (!fp)
        return 0;
    if (out && out_cap > 0u) {
        size_t n = fread(out, 1, out_cap - 1u, fp);
        out[n] = '\0';
    } else {
        char discard[256];
        while (fgets(discard, sizeof(discard), fp) != NULL)
            ;
    }
    return pclose(fp) == 0;
}

static int parse_bssid(const char *s, uint8_t bssid[6]) {
    unsigned o[6];
    if (!s || !bssid)
        return 0;
    if (sscanf(s, "%x:%x:%x:%x:%x:%x", &o[0], &o[1], &o[2], &o[3], &o[4], &o[5]) != 6)
        return 0;
    for (int i = 0; i < 6; i++)
        bssid[i] = (uint8_t)o[i];
    return 1;
}

static uint8_t parse_auth_token(const char *flags) {
    if (!flags)
        return FL_WIFI_AUTH_OPEN;
    if (strstr(flags, "WPA3") || strstr(flags, "SAE"))
        return FL_WIFI_AUTH_WPA3_SAE;
    if (strstr(flags, "WPA2"))
        return FL_WIFI_AUTH_WPA2_PSK;
    if (strstr(flags, "WPA"))
        return FL_WIFI_AUTH_WPA2_PSK;
    return FL_WIFI_AUTH_OPEN;
}

static void parse_scan_results(const char *text) {
    const char *p = text;
    char line[512];

    s_wpa_scan_count = 0;
    if (!text)
        return;
    while (*p && s_wpa_scan_count < 32u) {
        size_t len = 0;
        fl_net_wifi_scan_entry_t *e;
        char bssid_txt[32];
        int freq = 0;
        int signal = -127;
        char flags[128];
        char ssid[FL_WIFI_SSID_MAX];

        while (*p == '\n' || *p == '\r')
            p++;
        if (!*p)
            break;
        {
            const char *eol = strchr(p, '\n');
            if (!eol)
                eol = p + strlen(p);
            len = (size_t)(eol - p);
            if (len >= sizeof(line))
                len = sizeof(line) - 1u;
            memcpy(line, p, len);
            line[len] = '\0';
            p = (*eol == '\n') ? eol + 1 : eol;
        }
        if (!strncmp(line, "bssid", 5))
            continue;
        flags[0] = '\0';
        ssid[0] = '\0';
        bssid_txt[0] = '\0';
        if (sscanf(line, "%31s %d %d %127[^\t] %63[^\n]", bssid_txt, &freq, &signal,
                   flags, ssid) < 4)
            continue;
        if (!bssid_txt[0] || !ssid[0])
            continue;
        e = &s_wpa_scan[s_wpa_scan_count++];
        memset(e, 0, sizeof(*e));
        (void)parse_bssid(bssid_txt, e->bssid);
        strncpy(e->ssid, ssid, sizeof(e->ssid) - 1u);
        e->rssi_dbm = signal;
        e->auth_mode = parse_auth_token(flags);
        e->band = (freq >= 5000) ? FL_WIFI_BAND_5GHZ : FL_WIFI_BAND_2GHZ;
        if (freq >= 5900)
            e->band = FL_WIFI_BAND_6GHZ;
        e->channel_width_mhz = 20;
    }
}
#endif

int fl_net_wifi_host_linux_available(void) {
#if !defined(FL_NET_WIFI_HOST_LINUX)
    return 0;
#else
    char out[64];
    int mode = wpa_use_requested();
    load_env_once();
    if (mode == 0)
        return 0;
    if (!run_wpa_cli("ping", out, sizeof(out)))
        return 0;
    return strstr(out, "PONG") != NULL;
#endif
}

const char *fl_net_wifi_host_linux_iface(void) {
#if !defined(FL_NET_WIFI_HOST_LINUX)
    return "";
#else
    load_env_once();
    return s_wifi_iface;
#endif
}

fl_result_t fl_net_wifi_host_linux_scan(uint8_t band, unsigned timeout_ms) {
#if !defined(FL_NET_WIFI_HOST_LINUX)
    (void)band;
    (void)timeout_ms;
    return FL_RESULT_NOSYS;
#else
    char out[8192];
    (void)band;
    (void)timeout_ms;
    if (!fl_net_wifi_host_linux_available())
        return FL_RESULT_NOSYS;
    if (!run_wpa_cli("scan", NULL, 0))
        return FL_RESULT_ERR;
    if (!run_wpa_cli("scan_results", out, sizeof(out)))
        return FL_RESULT_ERR;
    parse_scan_results(out);
    return FL_RESULT_OK;
#endif
}

fl_result_t fl_net_wifi_host_linux_scan_result(fl_net_wifi_scan_entry_t *entries, size_t cap,
                                               size_t *count_out) {
#if !defined(FL_NET_WIFI_HOST_LINUX)
    (void)entries;
    (void)cap;
    (void)count_out;
    return FL_RESULT_NOSYS;
#else
    size_t i;
    if (!entries || !count_out || cap == 0u)
        return FL_RESULT_INVAL;
    *count_out = 0u;
    for (i = 0; i < s_wpa_scan_count && i < cap; i++) {
        entries[i] = s_wpa_scan[i];
        (*count_out)++;
    }
    return FL_RESULT_OK;
#endif
}

fl_result_t fl_net_wifi_host_linux_connect(const fl_net_wifi_cred_t *cred,
                                           unsigned timeout_ms) {
#if !defined(FL_NET_WIFI_HOST_LINUX)
    (void)cred;
    (void)timeout_ms;
    return FL_RESULT_NOSYS;
#else
    char out[256];
    char cmd[512];
    int net_id;
    (void)timeout_ms;
    if (!cred || !cred->ssid[0])
        return FL_RESULT_INVAL;
    if (!fl_net_wifi_host_linux_available())
        return FL_RESULT_NOSYS;

    if (!run_wpa_cli("disconnect", NULL, 0))
        return FL_RESULT_ERR;
    if (!run_wpa_cli("remove_network all", NULL, 0))
        return FL_RESULT_ERR;
    if (!run_wpa_cli("add_network", out, sizeof(out)))
        return FL_RESULT_ERR;
    net_id = atoi(out);
    if (net_id < 0)
        return FL_RESULT_ERR;

    snprintf(cmd, sizeof(cmd), "set_network %d ssid \"%s\"", net_id, cred->ssid);
    if (!run_wpa_cli(cmd, NULL, 0))
        return FL_RESULT_ERR;
    if (cred->passphrase[0]) {
        snprintf(cmd, sizeof(cmd), "set_network %d psk \"%s\"", net_id, cred->passphrase);
        if (!run_wpa_cli(cmd, NULL, 0))
            return FL_RESULT_ERR;
    } else {
        snprintf(cmd, sizeof(cmd), "set_network %d key_mgmt NONE", net_id);
        if (!run_wpa_cli(cmd, NULL, 0))
            return FL_RESULT_ERR;
    }
    snprintf(cmd, sizeof(cmd), "enable_network %d", net_id);
    if (!run_wpa_cli(cmd, NULL, 0))
        return FL_RESULT_ERR;
    if (!run_wpa_cli("save_config", NULL, 0))
        return FL_RESULT_ERR;
    if (!run_wpa_cli("reconnect", NULL, 0))
        return FL_RESULT_ERR;

    strncpy(s_wpa_joined_ssid, cred->ssid, sizeof(s_wpa_joined_ssid) - 1u);
    return FL_RESULT_OK;
#endif
}

fl_result_t fl_net_wifi_host_linux_disconnect(void) {
#if !defined(FL_NET_WIFI_HOST_LINUX)
    return FL_RESULT_NOSYS;
#else
    s_wpa_joined_ssid[0] = '\0';
    if (!fl_net_wifi_host_linux_available())
        return FL_RESULT_NOSYS;
    if (!run_wpa_cli("disconnect", NULL, 0))
        return FL_RESULT_ERR;
    return FL_RESULT_OK;
#endif
}

fl_result_t fl_net_wifi_host_linux_ipv4(uint32_t *addr_be_out, char *buf, size_t buf_len) {
#if !defined(FL_NET_WIFI_HOST_LINUX)
    (void)addr_be_out;
    (void)buf;
    (void)buf_len;
    return FL_RESULT_NOSYS;
#else
    fl_net_host_iface_entry_t entries[32];
    size_t count = 0u;
    size_t i;

    load_env_once();
    if (!fl_net_host_iface_list(entries, 32, &count))
        return FL_RESULT_NOSYS;
    for (i = 0; i < count; i++) {
        if (!entries[i].is_up)
            continue;
        if (strcmp(entries[i].name, s_wifi_iface) != 0)
            continue;
        if (addr_be_out)
            *addr_be_out = entries[i].addr_be;
        if (buf && buf_len > 0u)
            fl_net_ipv4_format_addr(entries[i].addr_be, buf, buf_len);
        return FL_RESULT_OK;
    }
    return FL_RESULT_NOENT;
#endif
}
