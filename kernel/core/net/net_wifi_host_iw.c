#include "net_wifi_host_iw.h"

#include "net_wifi_he.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int flags_has_ax_token(const char *flags)
{
    const char *p;

    if (!flags || !flags[0])
        return 0;
    if (strstr(flags, "HE") || strstr(flags, "EHT") || strstr(flags, "AX"))
        return 1;
    for (p = flags; *p; p++) {
        if (p[0] == 'h' && p[1] == 'e' && (p[2] == ' ' || p[2] == ']' || p[2] == '\0'))
            return 1;
    }
    return 0;
}

static unsigned rate_mbit_hint(const char *rate_txt)
{
    unsigned mbit = 0;

    if (!rate_txt || !rate_txt[0])
        return 0u;
    if (sscanf(rate_txt, "%u", &mbit) != 1)
        return 0u;
    return mbit;
}

void fl_net_wifi_host_he_hint(fl_net_wifi_scan_entry_t *entry, const char *flags,
                              const char *rate_mbit)
{
    unsigned rate;

    if (!entry)
        return;

    if (entry->band == FL_WIFI_BAND_6GHZ) {
        entry->he_supported = 1u;
        if (entry->channel_width_mhz < 80u)
            entry->channel_width_mhz = 80u;
    }

    if (flags_has_ax_token(flags))
        entry->he_supported = 1u;

    rate = rate_mbit_hint(rate_mbit);
    if (rate >= 400u) {
        entry->he_supported = 1u;
        if (entry->channel_width_mhz < 80u)
            entry->channel_width_mhz = 80u;
    } else if (rate >= 200u && entry->band == FL_WIFI_BAND_5GHZ) {
        entry->he_supported = 1u;
        if (entry->channel_width_mhz < 40u)
            entry->channel_width_mhz = 40u;
    }

    if (entry->auth_mode == FL_WIFI_AUTH_WPA3_SAE && entry->band == FL_WIFI_BAND_5GHZ)
        entry->he_supported = 1u;

    if (entry->band == FL_WIFI_BAND_5GHZ && entry->channel >= 36u)
        entry->he_supported = 1u;

    if (entry->he_supported && entry->channel_width_mhz == 0u)
        entry->channel_width_mhz = 20u;
}

void fl_net_wifi_host_he_cap_from_entry(const fl_net_wifi_scan_entry_t *ap,
                                        fl_net_wifi_he_cap_t *cap_out)
{
    if (!cap_out)
        return;
    memset(cap_out, 0, sizeof(*cap_out));
    if (!ap || !ap->he_supported)
        return;

    cap_out->max_nss_rx = 2u;
    cap_out->max_nss_tx = 2u;
    cap_out->supports_ofdma = 1u;
    cap_out->supports_mu_mimo = 1u;
    cap_out->supports_twt = ap->twt_responder ? 1u : 0u;
    cap_out->bss_color = ap->bss_color;
    cap_out->channel_width_mhz = ap->channel_width_mhz ? ap->channel_width_mhz : 80u;
    cap_out->supports_6ghz = (ap->band == FL_WIFI_BAND_6GHZ) ? 1u : 0u;
}

#if defined(__linux__)

static int parse_bssid_colon(const char *s, uint8_t bssid[6])
{
    unsigned o[6];

    if (!s || !bssid)
        return 0;
    if (sscanf(s, "%x:%x:%x:%x:%x:%x", &o[0], &o[1], &o[2], &o[3], &o[4], &o[5]) != 6)
        return 0;
    for (int i = 0; i < 6; i++)
        bssid[i] = (uint8_t)o[i];
    return 1;
}

static fl_net_wifi_scan_entry_t *find_entry_bssid(fl_net_wifi_scan_entry_t *entries,
                                                  size_t count, const uint8_t bssid[6])
{
    size_t i;

    for (i = 0; i < count; i++) {
        if (!memcmp(entries[i].bssid, bssid, 6))
            return &entries[i];
    }
    return NULL;
}

static void enrich_bss_block(const char *block, size_t block_len,
                             fl_net_wifi_scan_entry_t *entries, size_t count,
                             size_t *enriched_out)
{
    const char *p = block;
    const char *end = block + block_len;
    uint8_t bssid[6];
    fl_net_wifi_scan_entry_t *e;
    char hex[4096];
    size_t hex_len = 0;

    bssid[0] = bssid[1] = bssid[2] = bssid[3] = bssid[4] = bssid[5] = 0;
    if (block_len > 6u && !strncmp(block, "BSS ", 4)) {
        char bssid_txt[32];
        const char *paren = strchr(block + 4, '(');
        size_t n = paren ? (size_t)(paren - (block + 4)) : strcspn(block + 4, " \t\n");
        if (n >= sizeof(bssid_txt))
            n = sizeof(bssid_txt) - 1u;
        memcpy(bssid_txt, block + 4, n);
        bssid_txt[n] = '\0';
        (void)parse_bssid_colon(bssid_txt, bssid);
    }

    e = find_entry_bssid(entries, count, bssid);
    if (!e)
        return;

    while (p < end) {
        const char *nl = memchr(p, '\n', (size_t)(end - p));
        size_t line_len = nl ? (size_t)(nl - p) : (size_t)(end - p);
        const char *line = p;

        if (line_len > 4u && !memcmp(line, "\tIE:", 4)) {
            const char *hex_start = line + 4;
            while (hex_start < line + line_len && (*hex_start == ' ' || *hex_start == '\t'))
                hex_start++;
            if (hex_len + line_len < sizeof(hex) - 2u) {
                if (hex_len)
                    hex[hex_len++] = ' ';
                memcpy(hex + hex_len, hex_start, line_len - (size_t)(hex_start - line));
                hex_len += line_len - (size_t)(hex_start - line);
            }
        }

        if (line_len > 18u && !memcmp(line, "\tHE capabilities:", 17)) {
            e->he_supported = 1u;
            if (strstr(line, "160 MHz"))
                e->channel_width_mhz = 160u;
            else if (strstr(line, "80 MHz"))
                e->channel_width_mhz = 80u;
            else if (strstr(line, "40 MHz"))
                e->channel_width_mhz = 40u;
        }

        if (line_len > 16u && !memcmp(line, "\tHE operation:", 14)) {
            const char *color = strstr(line, "bss color");
            if (color) {
                unsigned c = 0;
                if (sscanf(color, "bss color %u", &c) == 1 && c <= 63u)
                    e->bss_color = (uint8_t)c;
            }
            if (strstr(line, "TWT responder"))
                e->twt_responder = 1u;
        }

        p = nl ? nl + 1 : end;
    }

    if (hex_len > 0u) {
        uint8_t ie_buf[512];
        size_t ie_len = 0;
        const char *hp = hex;

        while (*hp && ie_len < sizeof(ie_buf)) {
            unsigned byte = 0;
            if (sscanf(hp, "%x", &byte) != 1)
                break;
            ie_buf[ie_len++] = (uint8_t)byte;
            hp = strchr(hp, ' ');
            if (!hp)
                break;
            hp++;
        }
        if (ie_len > 0u)
            (void)fl_net_wifi_scan_enrich_from_ies(ie_buf, ie_len, e);
    }

    if (e->he_supported && enriched_out)
        (*enriched_out)++;
}

size_t fl_net_wifi_iw_enrich_scan(const char *iface, fl_net_wifi_scan_entry_t *entries,
                                  size_t count)
{
    char cmd[160];
    FILE *fp;
    char block[8192];
    char line[512];
    size_t block_len = 0;
    size_t enriched = 0;

    if (!iface || !iface[0] || !entries || count == 0u)
        return 0u;

    snprintf(cmd, sizeof(cmd), "iw dev %s scan dump 2>/dev/null", iface);
    fp = popen(cmd, "r");
    if (!fp)
        return 0u;

    while (fgets(line, sizeof(line), fp)) {
        if (!strncmp(line, "BSS ", 4)) {
            if (block_len > 0u)
                enrich_bss_block(block, block_len, entries, count, &enriched);
            block_len = 0;
        }
        {
            size_t ll = strlen(line);
            if (block_len + ll >= sizeof(block))
                continue;
            memcpy(block + block_len, line, ll + 1u);
            block_len += ll;
        }
    }
    if (block_len > 0u)
        enrich_bss_block(block, block_len, entries, count, &enriched);
    pclose(fp);
    return enriched;
}

int fl_net_wifi_iw_connected_he_cap(const char *iface,
                                    const fl_net_wifi_scan_entry_t *join_ap,
                                    fl_net_wifi_he_cap_t *cap_out)
{
    char cmd[192];
    char out[512];
    FILE *fp;

    if (!iface || !iface[0] || !cap_out)
        return -1;

    if (join_ap && join_ap->he_supported) {
        fl_net_wifi_host_he_cap_from_entry(join_ap, cap_out);
        return 0;
    }

    snprintf(cmd, sizeof(cmd), "iw dev %s link 2>/dev/null", iface);
    fp = popen(cmd, "r");
    if (!fp)
        return -1;
    out[0] = '\0';
    while (fgets(out, sizeof(out), fp)) {
        if (strstr(out, "Connected to")) {
            pclose(fp);
            memset(cap_out, 0, sizeof(*cap_out));
            cap_out->max_nss_rx = 2u;
            cap_out->max_nss_tx = 2u;
            cap_out->channel_width_mhz = 80u;
            cap_out->supports_ofdma = 1u;
            cap_out->supports_mu_mimo = 1u;
            return 0;
        }
    }
    pclose(fp);
    return -1;
}

#endif /* __linux__ */
