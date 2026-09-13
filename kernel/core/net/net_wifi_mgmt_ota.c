/*
 * Management-frame OTA orchestration (#328 Task 2).
 * Kernel orchestration: nl80211 TX/RX + net_wifi_mgmt parse/build.
 */

#include "net_wifi_mgmt_ota.h"

#include "net_wifi_fullmac.h"
#include "net_wifi_mgmt.h"
#include "wifi_nl80211.h"

#include <stdlib.h>
#include <string.h>

#define MGMT_OTA_CACHE_MAX 32u

typedef struct {
    fl_net_wifi_scan_entry_t entry;
    int valid;
} mgmt_ota_cache_slot_t;

struct fl_net_wifi_mgmt_ota {
    fl_net_wifi_nl80211_t *nl;
    uint8_t sta_mac[6];
    fl_net_wifi_he_cap_t phy_he;
    mgmt_ota_cache_slot_t cache[MGMT_OTA_CACHE_MAX];
    unsigned cache_count;
};

static fl_net_wifi_mgmt_ota_t *g_fullmac_mgmt_ota;

static mgmt_ota_cache_slot_t *mgmt_ota_find_slot(fl_net_wifi_mgmt_ota_t *ota,
                                                 const uint8_t bssid[6], int create)
{
    unsigned i;
    mgmt_ota_cache_slot_t *free_slot = NULL;

    if (!ota || !bssid)
        return NULL;
    for (i = 0; i < ota->cache_count; i++) {
        if (ota->cache[i].valid && !memcmp(ota->cache[i].entry.bssid, bssid, 6u))
            return &ota->cache[i];
    }
    if (!create)
        return NULL;
    for (i = 0; i < MGMT_OTA_CACHE_MAX; i++) {
        if (!ota->cache[i].valid) {
            free_slot = &ota->cache[i];
            break;
        }
    }
    if (!free_slot)
        return NULL;
    memset(free_slot, 0, sizeof(*free_slot));
    memcpy(free_slot->entry.bssid, bssid, 6u);
    free_slot->valid = 1;
    if (ota->cache_count < MGMT_OTA_CACHE_MAX)
        ota->cache_count++;
    return free_slot;
}

static void mgmt_ota_ingest_bss(fl_net_wifi_mgmt_ota_t *ota,
                                const fl_net_wifi_mgmt_bss_info_t *bss)
{
    fl_net_wifi_scan_entry_t tmp;
    mgmt_ota_cache_slot_t *slot;

    if (!ota || !bss)
        return;
    slot = mgmt_ota_find_slot(ota, bss->bssid, 1);
    if (!slot)
        return;
    memset(&tmp, 0, sizeof(tmp));
    fl_net_wifi_mgmt_bss_to_scan_entry(bss, &tmp);
    fl_net_wifi_scan_entry_merge(&slot->entry, &tmp);
}

static void mgmt_ota_on_probe_resp(const uint8_t *frame, size_t len, void *ctx)
{
    fl_net_wifi_mgmt_ota_t *ota = (fl_net_wifi_mgmt_ota_t *)ctx;
    fl_net_wifi_mgmt_bss_info_t bss;

    if (!ota || !frame)
        return;
    if (fl_net_wifi_mgmt_parse_probe_resp(frame, len, &bss) == FL_RESULT_OK)
        mgmt_ota_ingest_bss(ota, &bss);
}

static void mgmt_ota_on_beacon(const uint8_t *frame, size_t len, void *ctx)
{
    fl_net_wifi_mgmt_ota_t *ota = (fl_net_wifi_mgmt_ota_t *)ctx;
    fl_net_wifi_mgmt_bss_info_t bss;

    if (!ota || !frame)
        return;
    if (fl_net_wifi_mgmt_parse_beacon(frame, len, &bss) == FL_RESULT_OK)
        mgmt_ota_ingest_bss(ota, &bss);
}

static void mgmt_ota_on_auth(const uint8_t *frame, size_t len, void *ctx)
{
    fl_net_wifi_mgmt_ota_t *ota = (fl_net_wifi_mgmt_ota_t *)ctx;
    uint16_t status = 0xffffu;

    (void)ota;
    if (!frame)
        return;
    (void)fl_net_wifi_mgmt_parse_auth_resp(frame, len, NULL, NULL, &status, NULL, NULL);
}

fl_result_t fl_net_wifi_mgmt_ota_create(fl_net_wifi_mgmt_ota_t **out, fl_net_wifi_nl80211_t *nl,
                                        const uint8_t sta_mac[6],
                                        const fl_net_wifi_he_cap_t *phy_he)
{
    fl_net_wifi_mgmt_ota_t *ota;

    if (!out || !nl || !sta_mac)
        return FL_RESULT_INVAL;
    ota = (fl_net_wifi_mgmt_ota_t *)calloc(1, sizeof(*ota));
    if (!ota)
        return FL_RESULT_ERR;
    ota->nl = nl;
    memcpy(ota->sta_mac, sta_mac, 6u);
    if (phy_he)
        ota->phy_he = *phy_he;
    else
        ota->phy_he.channel_width_mhz = 80u;
    *out = ota;
    return FL_RESULT_OK;
}

void fl_net_wifi_mgmt_ota_destroy(fl_net_wifi_mgmt_ota_t *ota)
{
    if (!ota)
        return;
    if (ota->nl)
        fl_net_wifi_nl80211_unregister_mgmt_ctx(ota->nl, ota);
    if (g_fullmac_mgmt_ota == ota)
        g_fullmac_mgmt_ota = NULL;
    free(ota);
}

fl_result_t fl_net_wifi_mgmt_ota_register(fl_net_wifi_mgmt_ota_t *ota)
{
    fl_result_t rc;

    if (!ota || !ota->nl)
        return FL_RESULT_INVAL;
    rc = fl_net_wifi_nl80211_register_mgmt(ota->nl, FL_WIFI_MGMT_FC_PROBE_RESP,
                                           mgmt_ota_on_probe_resp, ota);
    if (rc != FL_RESULT_OK)
        return rc;
    rc = fl_net_wifi_nl80211_register_mgmt(ota->nl, FL_WIFI_MGMT_FC_BEACON, mgmt_ota_on_beacon,
                                           ota);
    if (rc != FL_RESULT_OK) {
        fl_net_wifi_nl80211_unregister_mgmt_ctx(ota->nl, ota);
        return rc;
    }
    rc = fl_net_wifi_nl80211_register_mgmt(ota->nl, FL_WIFI_MGMT_FC_AUTH, mgmt_ota_on_auth, ota);
    if (rc != FL_RESULT_OK)
        fl_net_wifi_nl80211_unregister_mgmt_ctx(ota->nl, ota);
    return rc;
}

fl_result_t fl_net_wifi_mgmt_ota_probe_tx(fl_net_wifi_mgmt_ota_t *ota, const char *ssid,
                                          unsigned timeout_ms)
{
    uint8_t frame[256];
    size_t len = 0;

    if (!ota || !ota->nl || !ssid)
        return FL_RESULT_INVAL;
    if (fl_net_wifi_mgmt_build_probe_req(ssid, ota->sta_mac, frame, sizeof(frame), &len) !=
        FL_RESULT_OK)
        return FL_RESULT_INVAL;
    return fl_net_wifi_nl80211_mgmt_tx(ota->nl, frame, len, timeout_ms);
}

void fl_net_wifi_mgmt_ota_merge_scan(fl_net_wifi_mgmt_ota_t *ota,
                                     fl_net_wifi_scan_entry_t *entries, size_t count)
{
    size_t i;
    unsigned j;

    if (!ota || !entries || count == 0u)
        return;
    for (i = 0; i < count; i++) {
        for (j = 0; j < MGMT_OTA_CACHE_MAX; j++) {
            if (!ota->cache[j].valid)
                continue;
            if (memcmp(entries[i].bssid, ota->cache[j].entry.bssid, 6u) != 0)
                continue;
            fl_net_wifi_scan_entry_merge(&entries[i], &ota->cache[j].entry);
        }
    }
}

fl_result_t fl_net_wifi_mgmt_ota_store_assoc_resp(fl_net_wifi_mgmt_ota_t *ota,
                                                  const uint8_t *frame, size_t len)
{
    uint16_t status = 0xffffu;
    fl_net_wifi_he_cap_t he;

    if (!ota || !frame)
        return FL_RESULT_INVAL;
    if (fl_net_wifi_mgmt_parse_assoc_resp(frame, len, &status, NULL, &he) != FL_RESULT_OK)
        return FL_RESULT_ERR;
    if (status != 0u)
        return FL_RESULT_ERR;
    if (he.channel_width_mhz == 0u)
        he.channel_width_mhz = ota->phy_he.channel_width_mhz ? ota->phy_he.channel_width_mhz : 80u;
    if (he.max_nss_rx == 0u)
        he.max_nss_rx = ota->phy_he.max_nss_rx ? ota->phy_he.max_nss_rx : 2u;
    if (he.max_nss_tx == 0u)
        he.max_nss_tx = ota->phy_he.max_nss_tx ? ota->phy_he.max_nss_tx : 2u;
    fl_net_wifi_fullmac_set_negotiated_he(&he);
    return FL_RESULT_OK;
}

fl_result_t fl_net_wifi_fullmac_mgmt_ota_attach(fl_net_wifi_nl80211_t *nl,
                                                const uint8_t sta_mac[6],
                                                const fl_net_wifi_he_cap_t *phy_he)
{
    fl_net_wifi_mgmt_ota_t *ota = NULL;
    fl_result_t rc;

    if (g_fullmac_mgmt_ota)
        return FL_RESULT_OK;
    rc = fl_net_wifi_mgmt_ota_create(&ota, nl, sta_mac, phy_he);
    if (rc != FL_RESULT_OK)
        return rc;
    rc = fl_net_wifi_mgmt_ota_register(ota);
    if (rc != FL_RESULT_OK) {
        fl_net_wifi_mgmt_ota_destroy(ota);
        return rc;
    }
    g_fullmac_mgmt_ota = ota;
    return FL_RESULT_OK;
}

void fl_net_wifi_fullmac_mgmt_ota_detach(void)
{
    if (!g_fullmac_mgmt_ota)
        return;
    fl_net_wifi_mgmt_ota_destroy(g_fullmac_mgmt_ota);
    g_fullmac_mgmt_ota = NULL;
}

fl_net_wifi_mgmt_ota_t *fl_net_wifi_fullmac_mgmt_ota(void)
{
    return g_fullmac_mgmt_ota;
}
