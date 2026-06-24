#include "net_wifi_mgmt.h"

#include "contract_result.h"

#include <string.h>

int fl_net_wifi_mgmt_hdr_valid(const uint8_t *frame, size_t len) {
    if (!frame || len < FL_WIFI_MGMT_HDR_LEN)
        return 0;
    if ((frame[0] & 0x0cu) != 0u)
        return 0;
    return 1;
}

fl_result_t fl_net_wifi_mgmt_build_probe_req(const char *ssid, uint8_t *out, size_t out_cap,
                                             size_t *out_len) {
    size_t ssid_len;
    size_t need;

    if (!ssid || !out || !out_len)
        return FL_RESULT_INVAL;
    ssid_len = strlen(ssid);
    if (ssid_len == 0u || ssid_len > 32u)
        return FL_RESULT_INVAL;
    need = FL_WIFI_MGMT_HDR_LEN + 2u + ssid_len;
    if (out_cap < need)
        return FL_RESULT_INVAL;

    memset(out, 0, FL_WIFI_MGMT_HDR_LEN);
    out[0] = 0x40u; /* Probe Request */
    out[24] = FL_WIFI_ELEM_SSID;
    out[25] = (uint8_t)ssid_len;
    memcpy(out + 26, ssid, ssid_len);
    *out_len = need;
    return FL_RESULT_OK;
}

fl_result_t fl_net_wifi_mgmt_parse_mgmt_ies(const uint8_t *frame, size_t len,
                                            const uint8_t **ies_out, size_t *ies_len) {
    size_t fixed = 12u;
    uint8_t subtype;

    if (!frame || !ies_out || !ies_len)
        return FL_RESULT_INVAL;
    if (!fl_net_wifi_mgmt_hdr_valid(frame, len))
        return FL_RESULT_INVAL;

    subtype = (uint8_t)(frame[0] & 0xfcu);
    switch (subtype) {
    case 0x00u: /* Association Request */
    case 0x40u: /* Probe Request */
        fixed = 0u;
        break;
    case 0x10u: /* Association Response */
    case 0xb0u: /* Authentication */
        fixed = 6u;
        break;
    case 0x50u: /* Probe Response */
    case 0x80u: /* Beacon */
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

static void fl_net_wifi_mgmt_fill_addrs(uint8_t *out, const uint8_t bssid[6],
                                      const uint8_t sta_mac[6]) {
    memcpy(out + 4, bssid, 6u);
    memcpy(out + 10, sta_mac, 6u);
    memcpy(out + 16, bssid, 6u);
}

fl_result_t fl_net_wifi_mgmt_build_auth_req(const uint8_t sta_mac[6], const uint8_t bssid[6],
                                            uint8_t *out, size_t out_cap, size_t *out_len) {
    if (!sta_mac || !bssid || !out || !out_len)
        return FL_RESULT_INVAL;
    if (out_cap < FL_WIFI_MGMT_HDR_LEN + 6u)
        return FL_RESULT_INVAL;

    memset(out, 0, FL_WIFI_MGMT_HDR_LEN);
    out[0] = 0xb0u; /* Authentication */
    fl_net_wifi_mgmt_fill_addrs(out, bssid, sta_mac);
    out[24] = 0x00u;
    out[25] = 0x00u; /* Open System */
    out[26] = 0x00u;
    out[27] = 0x01u; /* Sequence 1 */
    out[28] = 0x00u;
    out[29] = 0x00u; /* Status 0 */
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

    memset(out, 0, FL_WIFI_MGMT_HDR_LEN);
    out[0] = 0xb0u;
    fl_net_wifi_mgmt_fill_addrs(out, bssid, sta_mac);
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
    need = FL_WIFI_MGMT_HDR_LEN + 6u + body_len;
    if (out_cap < need)
        return FL_RESULT_INVAL;

    memset(out, 0, FL_WIFI_MGMT_HDR_LEN);
    out[0] = 0xb0u;
    fl_net_wifi_mgmt_fill_addrs(out, bssid, sta_mac);
    out[24] = 0x03u;
    out[25] = 0x00u; /* SAE */
    out[26] = (uint8_t)(auth_seq & 0xffu);
    out[27] = (uint8_t)((auth_seq >> 8) & 0xffu);
    out[28] = 0x00u;
    out[29] = 0x00u;
    if (body_len > 0u && body)
        memcpy(out + FL_WIFI_MGMT_HDR_LEN + 6u, body, body_len);
    *out_len = need;
    return FL_RESULT_OK;
}

fl_result_t fl_net_wifi_mgmt_build_assoc_req(const char *ssid, const uint8_t bssid[6],
                                             const uint8_t sta_mac[6], uint8_t auth_mode,
                                             uint8_t *out, size_t out_cap, size_t *out_len) {
    size_t ssid_len;
    size_t pos;
    uint8_t rsne[32];
    size_t rsne_len = 0;
    static const uint8_t he_cap_stub[] = {
        FL_WIFI_ELEM_ID_EXTENSION, 4u, FL_WIFI_EXT_HE_CAPABILITIES, 0x00, 0x80, 0x00
    };

    if (!ssid || !bssid || !sta_mac || !out || !out_len)
        return FL_RESULT_INVAL;
    ssid_len = strlen(ssid);
    if (ssid_len == 0u || ssid_len > 32u)
        return FL_RESULT_INVAL;
    if (fl_net_wifi_mgmt_build_rsne_ie(auth_mode, rsne, sizeof(rsne), &rsne_len) != FL_RESULT_OK)
        return FL_RESULT_INVAL;
    pos = FL_WIFI_MGMT_HDR_LEN;
    if (out_cap < pos + 2u + ssid_len + rsne_len + sizeof(he_cap_stub))
        return FL_RESULT_INVAL;

    memset(out, 0, FL_WIFI_MGMT_HDR_LEN);
    out[0] = 0x00u; /* Assoc Request */
    memcpy(out + 4, bssid, 6u);
    memcpy(out + 10, sta_mac, 6u);
    memcpy(out + 16, bssid, 6u);

    out[pos++] = FL_WIFI_ELEM_SSID;
    out[pos++] = (uint8_t)ssid_len;
    memcpy(out + pos, ssid, ssid_len);
    pos += ssid_len;
    if (rsne_len > 0u) {
        memcpy(out + pos, rsne, rsne_len);
        pos += rsne_len;
    }
    memcpy(out + pos, he_cap_stub, sizeof(he_cap_stub));
    pos += sizeof(he_cap_stub);
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

    memset(out, 0, FL_WIFI_MGMT_HDR_LEN);
    out[0] = 0x10u; /* Association Response */
    memcpy(out + 4, bssid, 6u);
    memcpy(out + 10, sta_mac, 6u);
    memcpy(out + 16, bssid, 6u);
    out[FL_WIFI_MGMT_HDR_LEN] = 0x00u;
    out[FL_WIFI_MGMT_HDR_LEN + 1u] = 0x00u;
    out[FL_WIFI_MGMT_HDR_LEN + 2u] = 0x00u;
    out[FL_WIFI_MGMT_HDR_LEN + 3u] = 0x00u; /* Status 0 */
    out[FL_WIFI_MGMT_HDR_LEN + 4u] = 0x00u;
    out[FL_WIFI_MGMT_HDR_LEN + 5u] = 0x01u; /* AID */
    pos = FL_WIFI_MGMT_HDR_LEN + 6u;
    memcpy(out + pos, he_cap_ie, sizeof(he_cap_ie));
    pos += sizeof(he_cap_ie);
    memcpy(out + pos, he_op_ie, sizeof(he_op_ie));
    pos += sizeof(he_op_ie);
    *out_len = pos;
    return FL_RESULT_OK;
}
