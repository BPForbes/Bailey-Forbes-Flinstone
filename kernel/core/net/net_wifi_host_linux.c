#include "net_wifi_host_linux.h"

#include "net_iface.h"
#include "net_ipv4.h"
#include "net_ipv6.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#if defined(__linux__)
#define FL_NET_WIFI_HOST_LINUX 1
#include <ifaddrs.h>
#include <netinet/in.h>
#include <sys/socket.h>
#endif

#if defined(FL_NET_WIFI_HOST_LINUX)
typedef enum {
    FL_WIFI_HOST_NONE = 0,
    FL_WIFI_HOST_WPA_CLI,
    FL_WIFI_HOST_NMCLI
} fl_wifi_host_kind_t;

static char s_wifi_iface[FL_NET_IFACE_NAME_MAX] = "";
static char s_wpa_cli[128] = "wpa_cli";
static char s_nmcli[128] = "nmcli";
static char s_wpa_ctrl_dir[256] = "";
static fl_net_wifi_scan_entry_t s_wpa_scan[32];
static size_t s_wpa_scan_count;
static char s_wpa_joined_ssid[FL_WIFI_SSID_MAX];
static int s_host_probed;
static fl_wifi_host_kind_t s_host_kind = FL_WIFI_HOST_NONE;


static int env_truthy(const char *v) {
    return v && (v[0] == '1' || v[0] == 'y' || v[0] == 'Y' || v[0] == 't' || v[0] == 'T');
}

static int env_falsy(const char *v) {
    return v && (v[0] == '0' || v[0] == 'n' || v[0] == 'N' || v[0] == 'f' || v[0] == 'F');
}

static int iface_is_wireless(const char *name) {
    char path[256];
    if (!name || !name[0])
        return 0;
    snprintf(path, sizeof(path), "/sys/class/net/%s/wireless", name);
    return access(path, F_OK) == 0;
}

static int pick_wireless_iface(char *out, size_t out_cap) {
    FILE *fp;
    char line[256];
    const char *nm;

    if (!out || out_cap == 0u)
        return 0;
    nm = getenv("FL_NET_WIFI_IFACE");
    if (nm && nm[0] && iface_is_wireless(nm)) {
        strncpy(out, nm, out_cap - 1u);
        out[out_cap - 1u] = '\0';
        return 1;
    }
    fp = popen("nmcli -t -f DEVICE,TYPE device 2>/dev/null", "r");
    if (fp) {
        while (fgets(line, sizeof(line), fp) != NULL) {
            char dev[FL_NET_IFACE_NAME_MAX];
            char kind[32];
            if (sscanf(line, "%31[^:]:%31s", dev, kind) == 2 && !strcmp(kind, "wifi")) {
                pclose(fp);
                strncpy(out, dev, out_cap - 1u);
                out[out_cap - 1u] = '\0';
                return 1;
            }
        }
        pclose(fp);
    }
    fp = fopen("/proc/net/wireless", "r");
    if (fp) {
        (void)fgets(line, sizeof(line), fp);
        (void)fgets(line, sizeof(line), fp);
        while (fgets(line, sizeof(line), fp) != NULL) {
            char dev[FL_NET_IFACE_NAME_MAX];
            if (sscanf(line, " %31s", dev) == 1 && dev[0] != ':') {
                fclose(fp);
                strncpy(out, dev, out_cap - 1u);
                out[out_cap - 1u] = '\0';
                return 1;
            }
        }
        fclose(fp);
    }
    if (iface_is_wireless("wlan0")) {
        strncpy(out, "wlan0", out_cap - 1u);
        out[out_cap - 1u] = '\0';
        return 1;
    }
    return 0;
}

static void load_env_once(void) {
    const char *cli;
    const char *nm;
    if (s_host_probed)
        return;
    s_host_probed = 1;
    cli = getenv("FL_NET_WIFI_WPA_CLI");
    if (cli && cli[0])
        strncpy(s_wpa_cli, cli, sizeof(s_wpa_cli) - 1u);
    nm = getenv("FL_NET_WIFI_NMCLI");
    if (nm && nm[0])
        strncpy(s_nmcli, nm, sizeof(s_nmcli) - 1u);
    if (!pick_wireless_iface(s_wifi_iface, sizeof(s_wifi_iface)))
        strncpy(s_wifi_iface, "wlan0", sizeof(s_wifi_iface) - 1u);
}

static int wpa_use_requested(void) {
    const char *v = getenv("FL_NET_WIFI_USE_WPA");
    load_env_once();
    if (env_falsy(v))
        return 0;
    if (env_truthy(v))
        return 1;
    return -1;
}

static int run_wpa_cli(const char *cmd, char *out, size_t out_cap) {
    char line[512];
    FILE *fp;

    if (!cmd || s_host_kind != FL_WIFI_HOST_WPA_CLI)
        return 0;
    if (s_wpa_ctrl_dir[0])
        snprintf(line, sizeof(line), "%s -i %s -p %s %s 2>/dev/null", s_wpa_cli,
                 s_wifi_iface, s_wpa_ctrl_dir, cmd);
    else
        snprintf(line, sizeof(line), "%s -i %s %s 2>/dev/null", s_wpa_cli, s_wifi_iface,
                 cmd);
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

static int popen_nmcli(const char *args, char *out, size_t out_cap) {
    char line[768];
    FILE *fp;

    if (!args)
        return 0;
    snprintf(line, sizeof(line), "%s %s 2>/dev/null", s_nmcli, args);
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

static int run_nmcli(const char *args, char *out, size_t out_cap) {
    if (s_host_kind != FL_WIFI_HOST_NMCLI)
        return 0;
    return popen_nmcli(args, out, out_cap);
}

static int wpa_ping_with_ctrl(const char *ctrl_dir) {
    char out[64];
    char cmd[512];
    FILE *fp;

    if (ctrl_dir && ctrl_dir[0])
        snprintf(cmd, sizeof(cmd), "%s -i %s -p %s ping 2>/dev/null", s_wpa_cli,
                 s_wifi_iface, ctrl_dir);
    else
        snprintf(cmd, sizeof(cmd), "%s -i %s ping 2>/dev/null", s_wpa_cli, s_wifi_iface);
    fp = popen(cmd, "r");
    if (!fp)
        return 0;
    out[0] = '\0';
    (void)fread(out, 1, sizeof(out) - 1u, fp);
    out[sizeof(out) - 1u] = '\0';
    (void)pclose(fp);
    return strstr(out, "PONG") != NULL;
}

static int probe_wpa_cli(void) {
    static const char *ctrl_dirs[] = {"", "/run/wpa_supplicant", "/var/run/wpa_supplicant",
                                      NULL};
    size_t i;

    for (i = 0; ctrl_dirs[i] != NULL; i++) {
        if (wpa_ping_with_ctrl(ctrl_dirs[i])) {
            if (ctrl_dirs[i][0])
                strncpy(s_wpa_ctrl_dir, ctrl_dirs[i], sizeof(s_wpa_ctrl_dir) - 1u);
            else
                s_wpa_ctrl_dir[0] = '\0';
            s_host_kind = FL_WIFI_HOST_WPA_CLI;
            return 1;
        }
    }
    return 0;
}

static int probe_nmcli(void) {
    char out[128];

    if (!popen_nmcli("-t -f RUNNING general", out, sizeof(out)))
        return 0;
    if (!strstr(out, "yes"))
        return 0;
    s_host_kind = FL_WIFI_HOST_NMCLI;
    return 1;
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

static int cred_bssid_set(const uint8_t bssid[6]) {
    unsigned i;
    if (!bssid)
        return 0;
    for (i = 0; i < 6u; i++) {
        if (bssid[i] != 0u)
            return 1;
    }
    return 0;
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

static uint8_t band_from_freq(int freq) {
    if (freq >= 5900)
        return FL_WIFI_BAND_6GHZ;
    if (freq >= 5000)
        return FL_WIFI_BAND_5GHZ;
    if (freq > 0)
        return FL_WIFI_BAND_2GHZ;
    return FL_WIFI_BAND_ANY;
}

static void parse_scan_results(const char *text, uint8_t band_filter) {
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
        if (sscanf(line, "%31s %d %d %127[^\t] %63[^\n]", bssid_txt, &freq, &signal, flags,
                   ssid) < 4)
            continue;
        if (!bssid_txt[0] || !ssid[0])
            continue;
        e = &s_wpa_scan[s_wpa_scan_count];
        memset(e, 0, sizeof(*e));
        (void)parse_bssid(bssid_txt, e->bssid);
        strncpy(e->ssid, ssid, sizeof(e->ssid) - 1u);
        e->rssi_dbm = signal;
        e->auth_mode = parse_auth_token(flags);
        e->band = band_from_freq(freq);
        e->channel_width_mhz = 20;
        if (band_filter != FL_WIFI_BAND_ANY && e->band != band_filter)
            continue;
        s_wpa_scan_count++;
    }
}

static void unescape_nmcli_field(const char *in, char *out, size_t out_cap) {
    size_t o = 0;
    if (!in || !out || out_cap == 0u)
        return;
    while (*in && o + 1u < out_cap) {
        if (in[0] == '\\' && in[1]) {
            out[o++] = in[1];
            in += 2;
            continue;
        }
        out[o++] = *in++;
    }
    out[o] = '\0';
}

static int nmcli_next_field(const char **pp, char *out, size_t out_cap) {
    const char *p = *pp;
    size_t o = 0;

    if (!p || !*p) {
        if (out && out_cap > 0u)
            out[0] = '\0';
        return 0;
    }
    while (*p && o + 2u < out_cap) {
        if (p[0] == '\\' && p[1]) {
            out[o++] = p[1];
            p += 2;
            continue;
        }
        if (*p == ':') {
            out[o] = '\0';
            *pp = p + 1;
            return 1;
        }
        out[o++] = *p++;
    }
    out[o] = '\0';
    *pp = p;
    return (o > 0u);
}

static void parse_nmcli_scan_results(const char *text, uint8_t band_filter) {
    const char *p = text;

    s_wpa_scan_count = 0;
    if (!text)
        return;
    while (*p && s_wpa_scan_count < 32u) {
        char ssid[FL_WIFI_SSID_MAX];
        char bssid_txt[32];
        char signal_txt[16];
        char chan_txt[16];
        char freq_txt[32];
        char sec_txt[128];
        fl_net_wifi_scan_entry_t *e;
        int freq = 0;
        int signal = -127;

        while (*p == '\n' || *p == '\r')
            p++;
        if (!*p)
            break;
        if (!nmcli_next_field(&p, ssid, sizeof(ssid)))
            continue;
        if (!nmcli_next_field(&p, bssid_txt, sizeof(bssid_txt)))
            continue;
        if (!nmcli_next_field(&p, signal_txt, sizeof(signal_txt)))
            continue;
        if (!nmcli_next_field(&p, chan_txt, sizeof(chan_txt)))
            continue;
        if (!nmcli_next_field(&p, freq_txt, sizeof(freq_txt)))
            continue;
        if (!nmcli_next_field(&p, sec_txt, sizeof(sec_txt)))
            sec_txt[0] = '\0';
        {
            const char *eol = strchr(p, '\n');
            p = eol ? eol + 1 : p + strlen(p);
        }
        unescape_nmcli_field(ssid, ssid, sizeof(ssid));
        unescape_nmcli_field(bssid_txt, bssid_txt, sizeof(bssid_txt));
        if (!ssid[0] || !bssid_txt[0])
            continue;
        e = &s_wpa_scan[s_wpa_scan_count];
        memset(e, 0, sizeof(*e));
        (void)parse_bssid(bssid_txt, e->bssid);
        strncpy(e->ssid, ssid, sizeof(e->ssid) - 1u);
        signal = atoi(signal_txt);
        e->rssi_dbm = signal;
        e->channel = (uint8_t)atoi(chan_txt);
        if (sscanf(freq_txt, "%d", &freq) != 1)
            freq = 0;
        e->auth_mode = parse_auth_token(sec_txt);
        e->band = band_from_freq(freq);
        e->channel_width_mhz = 20;
        if (band_filter != FL_WIFI_BAND_ANY && e->band != band_filter)
            continue;
        s_wpa_scan_count++;
    }
}

static int nmcli_state_connected(void) {
    char out[512];
    char line[256];
    const char *p;

    if (!run_nmcli("-t -f DEVICE,STATE device", out, sizeof(out)))
        return 0;
    p = out;
    while (nmcli_next_field(&p, line, sizeof(line))) {
        char state[64];
        if (!nmcli_next_field(&p, state, sizeof(state)))
            break;
        if (!strcmp(line, s_wifi_iface) && !strcmp(state, "connected"))
            return 1;
    }
    return 0;
}

static int wpa_state_completed(void) {
    char out[512];
    const char *p;
    if (!run_wpa_cli("status", out, sizeof(out)))
        return 0;
    p = strstr(out, "wpa_state=");
    return p && strstr(p, "COMPLETED") != NULL;
}

static unsigned prefix_from_netmask(uint32_t mask_be) {
    uint32_t m = mask_be;
    unsigned n = 0;
    while (m & 0x80000000u) {
        n++;
        m <<= 1;
    }
    return n > 0u ? n : 24u;
}

static unsigned prefix_from_netmask6(const uint8_t mask[16]) {
    unsigned n = 0;
    unsigned i;

    for (i = 0; i < 16u; i++) {
        uint8_t b = mask[i];
        if (b == 0xffu) {
            n += 8u;
            continue;
        }
        while (b & 0x80u) {
            n++;
            b = (uint8_t)(b << 1);
        }
        break;
    }
    return n > 0u ? n : 64u;
}

static int ipv4_pick_score(uint32_t addr) {
    if (addr == 0u || fl_net_ipv4_is_apipa(addr))
        return 0;
    if (fl_net_ipv4_is_private_rfc1918(addr))
        return 2;
    return 1;
}

static fl_result_t read_iface_ipv4_route(uint32_t *addr_be_out, uint8_t *prefix_len_out,
                                       uint32_t *gw_be_out) {
    struct ifaddrs *ifa = NULL;
    struct ifaddrs *cur;
    uint32_t best_addr = 0u;
    uint32_t best_mask = 0u;
    int best_score = 0;

    if (getifaddrs(&ifa) != 0)
        return FL_RESULT_ERR;
    for (cur = ifa; cur; cur = cur->ifa_next) {
        struct sockaddr_in *sin;
        uint32_t addr;
        uint32_t mask = 0u;
        int score;

        if (!cur->ifa_addr || !cur->ifa_name)
            continue;
        if (strcmp(cur->ifa_name, s_wifi_iface) != 0)
            continue;
        if (cur->ifa_addr->sa_family != AF_INET)
            continue;
        sin = (struct sockaddr_in *)cur->ifa_addr;
        addr = sin->sin_addr.s_addr;
        score = ipv4_pick_score(addr);
        if (score == 0)
            continue;
        if (cur->ifa_netmask && cur->ifa_netmask->sa_family == AF_INET) {
            struct sockaddr_in *nm = (struct sockaddr_in *)cur->ifa_netmask;
            mask = nm->sin_addr.s_addr;
        }
        if (score > best_score || (score == best_score && best_addr == 0u)) {
            best_addr = addr;
            best_mask = mask;
            best_score = score;
        }
    }
    freeifaddrs(ifa);
    if (best_addr == 0u)
        return FL_RESULT_NOENT;
    if (addr_be_out)
        *addr_be_out = best_addr;
    if (prefix_len_out)
        *prefix_len_out = (uint8_t)prefix_from_netmask(best_mask);
    if (gw_be_out) {
        uint32_t gw = (best_addr & 0xffffff00u) | 1u;
        *gw_be_out = gw;
    }
    return FL_RESULT_OK;
}

static fl_result_t read_iface_ipv6_route(uint8_t addr6_out[16], uint8_t *prefix_len_out) {
    struct ifaddrs *ifa = NULL;
    struct ifaddrs *cur;
    uint8_t best[16];
    uint8_t best_mask[16];
    int have = 0;

    if (!addr6_out)
        return FL_RESULT_INVAL;
    if (getifaddrs(&ifa) != 0)
        return FL_RESULT_ERR;
    memset(best, 0, sizeof(best));
    memset(best_mask, 0, sizeof(best_mask));
    for (cur = ifa; cur; cur = cur->ifa_next) {
        struct sockaddr_in6 *sin6;
        uint8_t mask[16];

        if (!cur->ifa_addr || !cur->ifa_name)
            continue;
        if (strcmp(cur->ifa_name, s_wifi_iface) != 0)
            continue;
        if (cur->ifa_addr->sa_family != AF_INET6)
            continue;
        sin6 = (struct sockaddr_in6 *)cur->ifa_addr;
        if (!fl_net_ipv6_is_global_unicast(sin6->sin6_addr.s6_addr))
            continue;
        memset(mask, 0, sizeof(mask));
        if (cur->ifa_netmask && cur->ifa_netmask->sa_family == AF_INET6) {
            struct sockaddr_in6 *nm = (struct sockaddr_in6 *)cur->ifa_netmask;
            memcpy(mask, nm->sin6_addr.s6_addr, 16);
        }
        memcpy(best, sin6->sin6_addr.s6_addr, 16);
        memcpy(best_mask, mask, 16);
        have = 1;
        break;
    }
    freeifaddrs(ifa);
    if (!have)
        return FL_RESULT_NOENT;
    memcpy(addr6_out, best, 16);
    if (prefix_len_out)
        *prefix_len_out = (uint8_t)prefix_from_netmask6(best_mask);
    return FL_RESULT_OK;
}

static fl_result_t wait_for_ipv4(unsigned timeout_ms, uint32_t *addr_be_out,
                                 uint8_t *prefix_len_out, uint32_t *gw_be_out) {
    unsigned elapsed = 0u;
    const unsigned step_ms = 250u;
    fl_result_t rc;

    while (elapsed <= timeout_ms) {
        rc = read_iface_ipv4_route(addr_be_out, prefix_len_out, gw_be_out);
        if (rc == FL_RESULT_OK)
            return rc;
        usleep((useconds_t)step_ms * 1000u);
        elapsed += step_ms;
    }
    return FL_RESULT_TIMEDOUT;
}

static fl_result_t wait_for_ipv6(unsigned timeout_ms, uint8_t addr6_out[16],
                                 uint8_t *prefix_len_out) {
    unsigned elapsed = 0u;
    const unsigned step_ms = 250u;
    fl_result_t rc;

    while (elapsed <= timeout_ms) {
        rc = read_iface_ipv6_route(addr6_out, prefix_len_out);
        if (rc == FL_RESULT_OK)
            return rc;
        usleep((useconds_t)step_ms * 1000u);
        elapsed += step_ms;
    }
    return FL_RESULT_TIMEDOUT;
}

static fl_result_t wait_for_wpa_completed(unsigned timeout_ms) {
    unsigned elapsed = 0u;
    const unsigned step_ms = 250u;

    while (elapsed <= timeout_ms) {
        if (wpa_state_completed())
            return FL_RESULT_OK;
        usleep((useconds_t)step_ms * 1000u);
        elapsed += step_ms;
    }
    return FL_RESULT_TIMEDOUT;
}
#endif

static void probe_host_backend_once(void) {
    int mode;

    load_env_once();
    if (s_host_kind != FL_WIFI_HOST_NONE)
        return;
    mode = wpa_use_requested();
    if (mode == 0)
        return;
    if (mode == 1) {
        if (probe_wpa_cli())
            return;
        return;
    }
    if (probe_wpa_cli())
        return;
    (void)probe_nmcli();
}

int fl_net_wifi_host_linux_available(void) {
#if !defined(FL_NET_WIFI_HOST_LINUX)
    return 0;
#else
    probe_host_backend_once();
    return s_host_kind != FL_WIFI_HOST_NONE;
#endif
}

const char *fl_net_wifi_host_linux_backend_name(void) {
#if !defined(FL_NET_WIFI_HOST_LINUX)
    return NULL;
#else
    probe_host_backend_once();
    if (s_host_kind == FL_WIFI_HOST_WPA_CLI)
        return "wpa_cli";
    if (s_host_kind == FL_WIFI_HOST_NMCLI)
        return "nmcli";
    return NULL;
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
    char cmd[256];
    (void)timeout_ms;
    if (!fl_net_wifi_host_linux_available())
        return FL_RESULT_NOSYS;
    if (s_host_kind == FL_WIFI_HOST_NMCLI) {
        snprintf(cmd, sizeof(cmd),
                 "-t -f SSID,BSSID,SIGNAL,CHAN,FREQ,SECURITY device wifi list ifname %s",
                 s_wifi_iface);
        if (!run_nmcli(cmd, out, sizeof(out)))
            return FL_RESULT_ERR;
        parse_nmcli_scan_results(out, band);
        return FL_RESULT_OK;
    }
    if (!run_wpa_cli("scan", NULL, 0))
        return FL_RESULT_ERR;
    usleep(500000);
    if (!run_wpa_cli("scan_results", out, sizeof(out)))
        return FL_RESULT_ERR;
    parse_scan_results(out, band);
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
    char bssid_txt[32];
    fl_result_t rc;

    if (!cred || !cred->ssid[0])
        return FL_RESULT_INVAL;
    if (!fl_net_wifi_host_linux_available())
        return FL_RESULT_NOSYS;

    if (s_host_kind == FL_WIFI_HOST_NMCLI) {
        char cmd[512];
        if (cred->passphrase[0])
            snprintf(cmd, sizeof(cmd), "device wifi connect \"%s\" password \"%s\" ifname %s",
                     cred->ssid, cred->passphrase, s_wifi_iface);
        else
            snprintf(cmd, sizeof(cmd), "device wifi connect \"%s\" ifname %s", cred->ssid,
                     s_wifi_iface);
        if (!run_nmcli(cmd, NULL, 0))
            return FL_RESULT_ERR;
        {
            unsigned elapsed = 0u;
            const unsigned step_ms = 250u;
            const unsigned limit = timeout_ms > 0u ? timeout_ms : 15000u;
            rc = FL_RESULT_TIMEDOUT;
            while (elapsed <= limit) {
                if (nmcli_state_connected()) {
                    rc = FL_RESULT_OK;
                    break;
                }
                usleep((useconds_t)step_ms * 1000u);
                elapsed += step_ms;
            }
            if (rc != FL_RESULT_OK)
                return rc;
        }
        rc = wait_for_ipv4(timeout_ms > 0u ? timeout_ms : 15000u, NULL, NULL, NULL);
        if (rc != FL_RESULT_OK && rc != FL_RESULT_TIMEDOUT)
            return rc;
        (void)wait_for_ipv6(timeout_ms > 0u ? timeout_ms : 15000u, NULL, NULL);
        strncpy(s_wpa_joined_ssid, cred->ssid, sizeof(s_wpa_joined_ssid) - 1u);
        return FL_RESULT_OK;
    }

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
    if (cred_bssid_set(cred->bssid)) {
        snprintf(bssid_txt, sizeof(bssid_txt), "%02x:%02x:%02x:%02x:%02x:%02x",
                 cred->bssid[0], cred->bssid[1], cred->bssid[2], cred->bssid[3],
                 cred->bssid[4], cred->bssid[5]);
        snprintf(cmd, sizeof(cmd), "set_network %d bssid %s", net_id, bssid_txt);
        if (!run_wpa_cli(cmd, NULL, 0))
            return FL_RESULT_ERR;
    }
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

    rc = wait_for_wpa_completed(timeout_ms > 0u ? timeout_ms : 15000u);
    if (rc != FL_RESULT_OK)
        return rc;

    rc = wait_for_ipv4(timeout_ms > 0u ? timeout_ms : 15000u, NULL, NULL, NULL);
    if (rc != FL_RESULT_OK && rc != FL_RESULT_TIMEDOUT)
        return rc;

    (void)wait_for_ipv6(timeout_ms > 0u ? timeout_ms : 15000u, NULL, NULL);

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
    if (s_host_kind == FL_WIFI_HOST_NMCLI) {
        char cmd[128];
        snprintf(cmd, sizeof(cmd), "device disconnect %s", s_wifi_iface);
        if (!run_nmcli(cmd, NULL, 0))
            return FL_RESULT_ERR;
        return FL_RESULT_OK;
    }
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
    uint32_t addr = 0u;
    fl_result_t rc;

    load_env_once();
    rc = read_iface_ipv4_route(&addr, NULL, NULL);
    if (rc != FL_RESULT_OK)
        return rc;
    if (addr_be_out)
        *addr_be_out = addr;
    if (buf && buf_len > 0u)
        fl_net_ipv4_format_addr(addr, buf, buf_len);
    return FL_RESULT_OK;
#endif
}


fl_result_t fl_net_wifi_host_linux_ipv6_route(uint8_t addr6[16], uint8_t *prefix_len_out) {
#if !defined(FL_NET_WIFI_HOST_LINUX)
    (void)addr6;
    (void)prefix_len_out;
    return FL_RESULT_NOSYS;
#else
    load_env_once();
    return read_iface_ipv6_route(addr6, prefix_len_out);
#endif
}

fl_result_t fl_net_wifi_host_linux_ipv6(uint8_t addr6[16], char *buf, size_t buf_len) {
#if !defined(FL_NET_WIFI_HOST_LINUX)
    (void)addr6;
    (void)buf;
    (void)buf_len;
    return FL_RESULT_NOSYS;
#else
    uint8_t local[16];
    fl_result_t rc;

    load_env_once();
    rc = read_iface_ipv6_route(local, NULL);
    if (rc != FL_RESULT_OK)
        return rc;
    if (addr6)
        memcpy(addr6, local, 16);
    if (buf && buf_len > 0u)
        fl_net_ipv6_format_addr(local, buf, buf_len);
    return FL_RESULT_OK;
#endif
}

fl_result_t fl_net_wifi_host_linux_ipv4_route(uint32_t *addr_be_out, uint8_t *prefix_len_out,
                                              uint32_t *gw_be_out) {
#if !defined(FL_NET_WIFI_HOST_LINUX)
    (void)addr_be_out;
    (void)prefix_len_out;
    (void)gw_be_out;
    return FL_RESULT_NOSYS;
#else
    load_env_once();
    return read_iface_ipv4_route(addr_be_out, prefix_len_out, gw_be_out);
#endif
}
