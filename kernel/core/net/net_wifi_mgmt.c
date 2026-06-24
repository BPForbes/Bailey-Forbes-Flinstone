#include "net_wifi_mgmt.h"

#include "net_wifi_he.h"
#include "contract_result.h"

#include <string.h>

static const uint8_t k_wifi_bcast[6] = {0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu};

static uint16_t mgmt_fc(const uint8_t *frame) {
    return ((uint16_t)frame[0] | ((uint16_t)frame[1] << 8)) & 0x00fcu;
}

static void mgmt_write_hdr(uint8_t *out, uint16_t fc, const uint8_t addr1[6],
                           const uint8_t addr2[6], const uint8_t addr3[6]) {
    out[0] = (uint8_t)(fc & 0xffu);
    out[1] = (uint8_t)((fc >> 8) & 0xffu);
    out[2] = 0u;
    out[3] = 0u;
    memcpy(out + 4, addr1, 6u);
    memcpy(out + 10, addr2, 6u);
    memcpy(out + 16, addr3, 6u);
    out[22] = 0u;
    out[23] = 0u;
}

int fl_net_wifi_mgmt_hdr_valid(const uint8_t *frame, size_t len) {
    if (!frame || len < FL_WIFI_MGMT_HDR_LEN)
        return 0;
    if ((frame[0] & 0x0cu) != 0u)
        return 0;
    return 1;
}

fl_result_t fl_net_wifi_scan_ssid_validate(const char *ssid, size_t *len_out) {
    size_t slen;

    if (!ssid)
        return FL_RESULT_INVAL;
    slen = strlen(ssid);
    if (slen == 0u || slen > FL_WIFI_SCAN_SSID_MAX_LEN)
        return FL_RESULT_INVAL;
    if (len_out)
        *len_out = slen;
    return FL_RESULT_OK;
}

fl_result_t fl_net_wifi_mgmt_build_probe_req(const char *ssid, const uint8_t sta_mac[6],
                                             uint8_t *out, size_t out_cap, size_t *out_len) {
    size_t ssid_len;
    size_t need;

    if (!ssid || !sta_mac || !out || !out_len)
        return FL_RESULT_INVAL;
    if (fl_net_wifi_scan_ssid_validate(ssid, &ssid_len) != FL_RESULT_OK)
        return FL_RESULT_INVAL;
    need = FL_WIFI_MGMT_HDR_LEN + 2u + ssid_len;
    if (out_cap < need)
        return FL_RESULT_INVAL;

    mgmt_write_hdr(out, FL_WIFI_MGMT_FC_PROBE_REQ, k_wifi_bcast, sta_mac, k_wifi_bcast);
    out[24] = FL_WIFI_ELEM_SSID;
    out[25] = (uint8_t)ssid_len;
    memcpy(out + 26, ssid, ssid_len);
    *out_len = need;
    return FL_RESULT_OK;
}

fl_result_t fl_net_wifi_mgmt_parse_mgmt_ies(const uint8_t *frame, size_t len,
                                            const uint8_t **ies_out, size_t *ies_len) {
    size_t fixed = 12u;
    uint16_t fc;

    if (!frame || !ies_out || !ies_len)
        return FL_RESULT_INVAL;
    if (!fl_net_wifi_mgmt_hdr_valid(frame, len))
        return FL_RESULT_INVAL;

    fc = mgmt_fc(frame);
    switch (fc) {
    case FL_WIFI_MGMT_FC_ASSOC_REQ:
        fixed = 4u;
        break;
    case FL_WIFI_MGMT_FC_PROBE_REQ:
        fixed = 0u;
        break;
    case FL_WIFI_MGMT_FC_ASSOC_RESP:
    case FL_WIFI_MGMT_FC_AUTH:
        fixed = 6u;
        break;
    case FL_WIFI_MGMT_FC_PROBE_RESP:
    case FL_WIFI_MGMT_FC_BEACON:
    default:
        fixed = 12u;
        break;
    }

    if (len < FL_WIFI_MGMT_HDR_LEN + fixed)
        return FL_RESULT_INVAL;
    *ies_out = frame + FL_WIFI_MGMT_HDR_LEN + fixed;
    *ies_len = len - (FL_WIFI_MGMT_HDR_LEN + fixed);
    return FL_RESULT_OK;
}

static uint8_t mgmt_parse_rsn_auth_mode(const uint8_t *data, size_t data_len) {
    size_t pos;
    uint16_t pairwise_count;
    uint16_t akm_count;
    size_t i;

    if (!data || data_len < 8u)
        return FL_WIFI_AUTH_OPEN;
    pos = 2u + 4u;
    if (pos + 2u > data_len)
        return FL_WIFI_AUTH_OPEN;
    pairwise_count = (uint16_t)data[pos] | ((uint16_t)data[pos + 1u] << 8);
    pos += 2u + (size_t)pairwise_count * 4u;
    if (pos + 2u > data_len)
        return FL_WIFI_AUTH_OPEN;
    akm_count = (uint16_t)data[pos] | ((uint16_t)data[pos + 1u] << 8);
    pos += 2u;
    for (i = 0; i < (size_t)akm_count && pos + 4u <= data_len; i++, pos += 4u) {
        if (data[pos] == 0x00u && data[pos + 1u] == 0x0fu && data[pos + 2u] == 0xacu) {
            if (data[pos + 3u] == 0x08u)
                return FL_WIFI_AUTH_WPA3_SAE;
            if (data[pos + 3u] == 0x02u)
                return FL_WIFI_AUTH_WPA2_PSK;
        }
    }
    return FL_WIFI_AUTH_WPA2_PSK;
}

static void mgmt_parse_common_ies(const uint8_t *ies, size_t ies_len,
                                  fl_net_wifi_mgmt_bss_info_t *out) {
    size_t off = 0;
    fl_net_wifi_scan_entry_t scan_tmp;

    if (!out || !ies)
        return;

    memset(&scan_tmp, 0, sizeof(scan_tmp));
    while (off + 2u <= ies_len) {
        uint8_t eid = ies[off];
        uint8_t elen = ies[off + 1u];
        const uint8_t *data;
        size_t data_len;

        off += 2u;
        if (off + (size_t)elen > ies_len)
            break;
        data = ies + off;
        data_len = (size_t)elen;
        off += data_len;

        if (eid == FL_WIFI_ELEM_SSID && data_len > 0u && data_len < sizeof(out->ssid)) {
            memcpy(out->ssid, data, data_len);
            out->ssid[data_len] = '\0';
        } else if (eid == FL_WIFI_ELEM_SUPPORTED_RATES && data_len > 0u) {
            size_t copy = data_len;

            if (copy > sizeof(out->supported_rates))
                copy = sizeof(out->supported_rates);
            memcpy(out->supported_rates, data, copy);
            out->supported_rates_len = (uint8_t)copy;
        } else if (eid == FL_WIFI_ELEM_DS_PARAM && data_len >= 1u)
            out->channel = data[0];
        else if (eid == FL_WIFI_ELEM_RSN && out->auth_mode == FL_WIFI_AUTH_OPEN)
            out->auth_mode = mgmt_parse_rsn_auth_mode(data, data_len);
    }

    (void)fl_net_wifi_scan_enrich_from_ies(ies, ies_len, &scan_tmp);
    out->he_supported = scan_tmp.he_supported;
    out->bss_color = scan_tmp.bss_color;
    out->channel_width_mhz = scan_tmp.channel_width_mhz;
    out->twt_responder = scan_tmp.twt_responder;
}

static fl_result_t mgmt_parse_probe_beacon(const uint8_t *frame, size_t len, int is_beacon,
                                           fl_net_wifi_mgmt_bss_info_t *out) {
    const uint8_t *ies = NULL;
    size_t ies_len = 0;
    uint16_t fc;

    if (!frame || !out)
        return FL_RESULT_INVAL;
    memset(out, 0, sizeof(*out));
    if (!fl_net_wifi_mgmt_hdr_valid(frame, len))
        return FL_RESULT_INVAL;

    fc = mgmt_fc(frame);
    if (is_beacon) {
        if (fc != FL_WIFI_MGMT_FC_BEACON)
            return FL_RESULT_INVAL;
    } else if (fc != FL_WIFI_MGMT_FC_PROBE_RESP) {
        return FL_RESULT_INVAL;
    }

    if (len < FL_WIFI_MGMT_HDR_LEN + 12u)
        return FL_RESULT_INVAL;

    memcpy(out->bssid, frame + 16, 6u);
    out->beacon_timestamp =
        ((uint64_t)frame[24] << 0) | ((uint64_t)frame[25] << 8) |
        ((uint64_t)frame[26] << 16) | ((uint64_t)frame[27] << 24) |
        ((uint64_t)frame[28] << 32) | ((uint64_t)frame[29] << 40) |
        ((uint64_t)frame[30] << 48) | ((uint64_t)frame[31] << 56);
    out->beacon_interval = (uint16_t)frame[32] | ((uint16_t)frame[33] << 8);
    out->capability = (uint16_t)frame[34] | ((uint16_t)frame[35] << 8);

    if (fl_net_wifi_mgmt_parse_mgmt_ies(frame, len, &ies, &ies_len) != FL_RESULT_OK)
        return FL_RESULT_INVAL;
    mgmt_parse_common_ies(ies, ies_len, out);
    return FL_RESULT_OK;
}

fl_result_t fl_net_wifi_mgmt_parse_probe_resp(const uint8_t *frame, size_t len,
                                              fl_net_wifi_mgmt_bss_info_t *out) {
    return mgmt_parse_probe_beacon(frame, len, 0, out);
}

fl_result_t fl_net_wifi_mgmt_parse_beacon(const uint8_t *frame, size_t len,
                                          fl_net_wifi_mgmt_bss_info_t *out) {
    return mgmt_parse_probe_beacon(frame, len, 1, out);
}

fl_result_t fl_net_wifi_mgmt_parse_auth_resp(const uint8_t *frame, size_t len,
                                             uint16_t *auth_alg_out, uint16_t *auth_seq_out,
                                             uint16_t *status_out, const uint8_t **body_out,
                                             size_t *body_len_out) {
    uint16_t fc;

    if (!frame)
        return FL_RESULT_INVAL;
    if (!fl_net_wifi_mgmt_hdr_valid(frame, len))
        return FL_RESULT_INVAL;
    fc = mgmt_fc(frame);
    if (fc != FL_WIFI_MGMT_FC_AUTH)
        return FL_RESULT_INVAL;
    if (len < FL_WIFI_MGMT_HDR_LEN + 6u)
        return FL_RESULT_INVAL;

    if (auth_alg_out)
        *auth_alg_out = (uint16_t)frame[24] | ((uint16_t)frame[25] << 8);
    if (auth_seq_out)
        *auth_seq_out = (uint16_t)frame[26] | ((uint16_t)frame[27] << 8);
    if (status_out)
        *status_out = (uint16_t)frame[28] | ((uint16_t)frame[29] << 8);
    if (body_out)
        *body_out = frame + FL_WIFI_MGMT_HDR_LEN + 6u;
    if (body_len_out)
        *body_len_out = len - (FL_WIFI_MGMT_HDR_LEN + 6u);
    return FL_RESULT_OK;
}

fl_result_t fl_net_wifi_mgmt_parse_assoc_resp(const uint8_t *frame, size_t len,
                                              uint16_t *status_out, uint16_t *aid_out,
                                              fl_net_wifi_he_cap_t *he_out) {
    const uint8_t *ies = NULL;
    size_t ies_len = 0;
    const uint8_t *he_cap_body = NULL;
    const uint8_t *he_op_body = NULL;
    size_t he_cap_len = 0;
    size_t he_op_len = 0;
    uint16_t fc;
    uint16_t status = 0xffffu;
    uint16_t aid = 0u;

    if (!frame)
        return FL_RESULT_INVAL;
    if (!fl_net_wifi_mgmt_hdr_valid(frame, len))
        return FL_RESULT_INVAL;
    fc = mgmt_fc(frame);
    if (fc != FL_WIFI_MGMT_FC_ASSOC_RESP)
        return FL_RESULT_INVAL;
    if (len < FL_WIFI_MGMT_HDR_LEN + 6u)
        return FL_RESULT_INVAL;

    status = (uint16_t)frame[26] | ((uint16_t)frame[27] << 8);
    aid = (uint16_t)frame[28] | ((uint16_t)frame[29] << 8);
    aid &= 0x3fffu;

    if (status_out)
        *status_out = status;
    if (aid_out)
        *aid_out = aid;

    if (!he_out)
        return FL_RESULT_OK;
    memset(he_out, 0, sizeof(*he_out));

    if (fl_net_wifi_mgmt_parse_mgmt_ies(frame, len, &ies, &ies_len) != FL_RESULT_OK)
        return FL_RESULT_OK;

    if (fl_net_wifi_ie_find_extension(ies, ies_len, FL_WIFI_EXT_HE_CAPABILITIES, &he_cap_body,
                                      &he_cap_len))
        (void)fl_net_wifi_he_parse_capabilities(he_cap_body, he_cap_len, he_out);
    if (fl_net_wifi_ie_find_extension(ies, ies_len, FL_WIFI_EXT_HE_OPERATION, &he_op_body,
                                      &he_op_len)) {
        uint8_t twt_resp = 0;
        (void)fl_net_wifi_he_parse_operation(he_op_body, he_op_len, &he_out->bss_color,
                                             &twt_resp);
        if (twt_resp)
            he_out->supports_twt = 1u;
    }
    return FL_RESULT_OK;
}

void fl_net_wifi_mgmt_bss_to_scan_entry(const fl_net_wifi_mgmt_bss_info_t *bss,
                                        fl_net_wifi_scan_entry_t *entry) {
    if (!bss || !entry)
        return;
    if (bss->ssid[0])
        strncpy(entry->ssid, bss->ssid, sizeof(entry->ssid) - 1u);
    entry->ssid[sizeof(entry->ssid) - 1u] = '\0';
    if (bss->bssid[0] || bss->bssid[1] || bss->bssid[2] || bss->bssid[3] || bss->bssid[4] ||
        bss->bssid[5])
        memcpy(entry->bssid, bss->bssid, 6u);
    if (bss->channel)
        entry->channel = bss->channel;
    if (bss->auth_mode != FL_WIFI_AUTH_OPEN)
        entry->auth_mode = bss->auth_mode;
    entry->he_supported = bss->he_supported;
    entry->bss_color = bss->bss_color;
    if (bss->channel_width_mhz)
        entry->channel_width_mhz = bss->channel_width_mhz;
    entry->twt_responder = bss->twt_responder;
}

void fl_net_wifi_scan_entry_merge(fl_net_wifi_scan_entry_t *dst,
                                  const fl_net_wifi_scan_entry_t *src) {
    if (!dst || !src)
        return;
    if (src->ssid[0] && !dst->ssid[0])
        strncpy(dst->ssid, src->ssid, sizeof(dst->ssid) - 1u);
    dst->ssid[sizeof(dst->ssid) - 1u] = '\0';
    if ((src->bssid[0] | src->bssid[1] | src->bssid[2] | src->bssid[3] | src->bssid[4] |
         src->bssid[5]) &&
        !(dst->bssid[0] | dst->bssid[1] | dst->bssid[2] | dst->bssid[3] | dst->bssid[4] |
          dst->bssid[5]))
        memcpy(dst->bssid, src->bssid, 6u);
    if (src->channel && !dst->channel)
        dst->channel = src->channel;
    if (src->auth_mode != FL_WIFI_AUTH_OPEN && dst->auth_mode == FL_WIFI_AUTH_OPEN)
        dst->auth_mode = src->auth_mode;
    if (src->rssi_dbm > dst->rssi_dbm)
        dst->rssi_dbm = src->rssi_dbm;
    if (src->band && !dst->band)
        dst->band = src->band;
    if (src->he_supported)
        dst->he_supported = 1u;
    if (src->bss_color)
        dst->bss_color = src->bss_color;
    if (src->channel_width_mhz > dst->channel_width_mhz)
        dst->channel_width_mhz = src->channel_width_mhz;
    if (src->twt_responder)
        dst->twt_responder = 1u;
}

fl_result_t fl_net_wifi_mgmt_enrich_scan_from_ies(const uint8_t *ies, size_t ies_len,
                                                  fl_net_wifi_scan_entry_t *entry) {
    fl_net_wifi_mgmt_bss_info_t bss;
    fl_net_wifi_scan_entry_t tmp;

    if (!ies || !entry)
        return FL_RESULT_INVAL;
    memset(&bss, 0, sizeof(bss));
    mgmt_parse_common_ies(ies, ies_len, &bss);
    memset(&tmp, 0, sizeof(tmp));
    fl_net_wifi_mgmt_bss_to_scan_entry(&bss, &tmp);
    fl_net_wifi_scan_entry_merge(entry, &tmp);
    return FL_RESULT_OK;
}

fl_result_t fl_net_wifi_mgmt_build_he_cap_ie(const fl_net_wifi_he_cap_t *sta_he, uint8_t *out,
                                             size_t out_cap, size_t *out_len) {
    uint8_t mac[6];
    uint8_t phy[11];
    size_t body_len = 17u;
    size_t need;

    if (!out || !out_len)
        return FL_RESULT_INVAL;
    if (!sta_he) {
        *out_len = 0u;
        return FL_RESULT_OK;
    }
    memset(mac, 0, sizeof(mac));
    memset(phy, 0, sizeof(phy));
    if (sta_he->supports_ofdma)
        mac[1] |= 0x80u;
    if (sta_he->supports_twt)
        mac[3] |= 0x06u;
    switch (sta_he->channel_width_mhz) {
    case 160u:
        phy[0] |= 0x40u;
        /* fall through */
    case 80u:
        phy[0] |= 0x04u;
        break;
    case 40u:
        phy[0] |= 0x02u;
        break;
    default:
        break;
    }
    if (sta_he->supports_6ghz)
        phy[0] |= 0x20u;
    if (sta_he->supports_mu_mimo)
        phy[0] |= 0x01u;

    need = 3u + body_len;
    if (out_cap < need)
        return FL_RESULT_INVAL;
    out[0] = FL_WIFI_ELEM_ID_EXTENSION;
    out[1] = (uint8_t)(1u + body_len);
    out[2] = FL_WIFI_EXT_HE_CAPABILITIES;
    memcpy(out + 3, mac, sizeof(mac));
    memcpy(out + 9, phy, sizeof(phy));
    *out_len = need;
    return FL_RESULT_OK;
}

fl_result_t fl_net_wifi_mgmt_build_rsne_ie(uint8_t auth_mode, uint8_t *out, size_t out_cap,
                                           size_t *out_len) {
    static const uint8_t rsne_wpa2[] = {
        0x30u, 18u, 0x01, 0x00u, 0x00, 0x0f, 0xac, 0x04u, 0x01, 0x00u,
        0x00, 0x0f, 0xac, 0x04u, 0x01, 0x00u, 0x00, 0x0f, 0xac, 0x02u
    };
    static const uint8_t rsne_wpa3[] = {
        0x30u, 18u, 0x01, 0x00u, 0x00, 0x0f, 0xac, 0x04u, 0x01, 0x00u,
        0x00, 0x0f, 0xac, 0x04u, 0x01, 0x00u, 0x00, 0x0f, 0xac, 0x08u
    };
    const uint8_t *src;
    size_t src_len;

    if (!out || !out_len)
        return FL_RESULT_INVAL;
    if (auth_mode == FL_WIFI_AUTH_WPA3_SAE) {
        src = rsne_wpa3;
        src_len = sizeof(rsne_wpa3);
    } else if (auth_mode == FL_WIFI_AUTH_WPA2_PSK) {
        src = rsne_wpa2;
        src_len = sizeof(rsne_wpa2);
    } else {
        *out_len = 0u;
        return FL_RESULT_OK;
    }
    if (out_cap < src_len)
        return FL_RESULT_INVAL;
    memcpy(out, src, src_len);
    *out_len = src_len;
    return FL_RESULT_OK;
}

fl_result_t fl_net_wifi_mgmt_build_auth_req(const uint8_t sta_mac[6], const uint8_t bssid[6],
                                            uint8_t *out, size_t out_cap, size_t *out_len) {
    if (!sta_mac || !bssid || !out || !out_len)
        return FL_RESULT_INVAL;
    if (out_cap < FL_WIFI_MGMT_HDR_LEN + 6u)
        return FL_RESULT_INVAL;

    mgmt_write_hdr(out, FL_WIFI_MGMT_FC_AUTH, bssid, sta_mac, bssid);
    out[24] = 0x00u;
    out[25] = 0x00u;
    out[26] = 0x01u;
    out[27] = 0x00u;
    out[28] = 0x00u;
    out[29] = 0x00u;
    *out_len = FL_WIFI_MGMT_HDR_LEN + 6u;
    return FL_RESULT_OK;
}

fl_result_t fl_net_wifi_mgmt_build_auth_resp(const uint8_t bssid[6], const uint8_t sta_mac[6],
                                             uint16_t auth_seq, uint8_t *out, size_t out_cap,
                                             size_t *out_len) {
    if (!bssid || !sta_mac || !out || !out_len)
        return FL_RESULT_INVAL;
    if (out_cap < FL_WIFI_MGMT_HDR_LEN + 6u)
        return FL_RESULT_INVAL;

    mgmt_write_hdr(out, FL_WIFI_MGMT_FC_AUTH, sta_mac, bssid, bssid);
    out[24] = 0x00u;
    out[25] = 0x00u;
    out[26] = (uint8_t)(auth_seq & 0xffu);
    out[27] = (uint8_t)((auth_seq >> 8) & 0xffu);
    out[28] = 0x00u;
    out[29] = 0x00u;
    *out_len = FL_WIFI_MGMT_HDR_LEN + 6u;
    return FL_RESULT_OK;
}

fl_result_t fl_net_wifi_mgmt_build_sae_auth(const uint8_t sta_mac[6], const uint8_t bssid[6],
                                            uint16_t auth_seq, const uint8_t *body, size_t body_len,
                                            uint8_t *out, size_t out_cap, size_t *out_len) {
    size_t need;

    if (!sta_mac || !bssid || !out || !out_len)
        return FL_RESULT_INVAL;
    if (body_len > 0u && !body)
        return FL_RESULT_INVAL;
    if (body_len > (size_t)-1 - (FL_WIFI_MGMT_HDR_LEN + 6u))
        return FL_RESULT_INVAL;
    need = FL_WIFI_MGMT_HDR_LEN + 6u + body_len;
    if (out_cap < need)
        return FL_RESULT_INVAL;

    mgmt_write_hdr(out, FL_WIFI_MGMT_FC_AUTH, bssid, sta_mac, bssid);
    out[24] = 0x03u;
    out[25] = 0x00u;
    out[26] = (uint8_t)(auth_seq & 0xffu);
    out[27] = (uint8_t)((auth_seq >> 8) & 0xffu);
    out[28] = 0x00u;
    out[29] = 0x00u;
    if (body_len > 0u && body)
        memcpy(out + FL_WIFI_MGMT_HDR_LEN + 6u, body, body_len);
    *out_len = need;
    return FL_RESULT_OK;
}

fl_result_t fl_net_wifi_mgmt_build_auth_sae_commit(const uint8_t sta_mac[6],
                                                   const uint8_t bssid[6], const uint8_t *commit,
                                                   size_t commit_len, uint8_t *out, size_t out_cap,
                                                   size_t *out_len) {
    return fl_net_wifi_mgmt_build_sae_auth(sta_mac, bssid, 1u, commit, commit_len, out, out_cap,
                                           out_len);
}

fl_result_t fl_net_wifi_mgmt_build_auth_sae_confirm(const uint8_t sta_mac[6],
                                                    const uint8_t bssid[6],
                                                    const uint8_t *confirm, size_t confirm_len,
                                                    uint8_t *out, size_t out_cap, size_t *out_len) {
    return fl_net_wifi_mgmt_build_sae_auth(sta_mac, bssid, 2u, confirm, confirm_len, out, out_cap,
                                           out_len);
}

fl_result_t fl_net_wifi_mgmt_build_assoc_req(const char *ssid, const uint8_t bssid[6],
                                             const uint8_t sta_mac[6], uint8_t auth_mode,
                                             const fl_net_wifi_he_cap_t *sta_he, uint8_t *out,
                                             size_t out_cap, size_t *out_len) {
    size_t ssid_len;
    size_t pos;
    uint8_t rsne[32];
    uint8_t he_cap[32];
    size_t rsne_len = 0;
    size_t he_cap_len = 0;

    if (!ssid || !bssid || !sta_mac || !out || !out_len)
        return FL_RESULT_INVAL;
    if (fl_net_wifi_scan_ssid_validate(ssid, &ssid_len) != FL_RESULT_OK)
        return FL_RESULT_INVAL;
    if (fl_net_wifi_mgmt_build_rsne_ie(auth_mode, rsne, sizeof(rsne), &rsne_len) != FL_RESULT_OK)
        return FL_RESULT_INVAL;
    if (fl_net_wifi_mgmt_build_he_cap_ie(sta_he, he_cap, sizeof(he_cap), &he_cap_len) !=
        FL_RESULT_OK)
        return FL_RESULT_INVAL;
    pos = FL_WIFI_MGMT_HDR_LEN;
    if (out_cap < pos + 4u + 2u + ssid_len + rsne_len + he_cap_len)
        return FL_RESULT_INVAL;

    mgmt_write_hdr(out, FL_WIFI_MGMT_FC_ASSOC_REQ, bssid, sta_mac, bssid);

    out[pos++] = 0x00u;
    out[pos++] = 0x00u;
    out[pos++] = 0x0au;
    out[pos++] = 0x00u;
    out[pos++] = FL_WIFI_ELEM_SSID;
    out[pos++] = (uint8_t)ssid_len;
    memcpy(out + pos, ssid, ssid_len);
    pos += ssid_len;
    if (rsne_len > 0u) {
        memcpy(out + pos, rsne, rsne_len);
        pos += rsne_len;
    }
    if (he_cap_len > 0u) {
        memcpy(out + pos, he_cap, he_cap_len);
        pos += he_cap_len;
    }
    *out_len = pos;
    return FL_RESULT_OK;
}

fl_result_t fl_net_wifi_mgmt_build_assoc_resp(const uint8_t bssid[6], const uint8_t sta_mac[6],
                                              uint8_t *out, size_t out_cap, size_t *out_len) {
    static const uint8_t he_cap_ie[] = {
        FL_WIFI_ELEM_ID_EXTENSION, 18u, FL_WIFI_EXT_HE_CAPABILITIES,
        0x00, 0x80, 0x00, 0x06, 0x00, 0x00,
        0x44, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };
    static const uint8_t he_op_ie[] = {
        FL_WIFI_ELEM_ID_EXTENSION, 5u, FL_WIFI_EXT_HE_OPERATION, 0x02, 0x00, 0x00, 0x05
    };
    size_t pos;

    if (!bssid || !sta_mac || !out || !out_len)
        return FL_RESULT_INVAL;
    pos = FL_WIFI_MGMT_HDR_LEN + 6u;
    if (out_cap < pos + sizeof(he_cap_ie) + sizeof(he_op_ie))
        return FL_RESULT_INVAL;

    mgmt_write_hdr(out, FL_WIFI_MGMT_FC_ASSOC_RESP, sta_mac, bssid, bssid);
    out[FL_WIFI_MGMT_HDR_LEN] = 0x00u;
    out[FL_WIFI_MGMT_HDR_LEN + 1u] = 0x00u;
    out[FL_WIFI_MGMT_HDR_LEN + 2u] = 0x00u;
    out[FL_WIFI_MGMT_HDR_LEN + 3u] = 0x00u;
    out[FL_WIFI_MGMT_HDR_LEN + 4u] = 0x01u;
    out[FL_WIFI_MGMT_HDR_LEN + 5u] = 0x00u;
    pos = FL_WIFI_MGMT_HDR_LEN + 6u;
    memcpy(out + pos, he_cap_ie, sizeof(he_cap_ie));
    pos += sizeof(he_cap_ie);
    memcpy(out + pos, he_op_ie, sizeof(he_op_ie));
    pos += sizeof(he_op_ie);
    *out_len = pos;
    return FL_RESULT_OK;
}
