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
    if (!frame || !ies_out || !ies_len)
        return FL_RESULT_INVAL;
    if (!fl_net_wifi_mgmt_hdr_valid(frame, len))
        return FL_RESULT_INVAL;
    if (len < FL_WIFI_MGMT_HDR_LEN + 12u)
        return FL_RESULT_INVAL;
    *ies_out = frame + FL_WIFI_MGMT_HDR_LEN + 12u;
    *ies_len = len - (FL_WIFI_MGMT_HDR_LEN + 12u);
    return FL_RESULT_OK;
}

fl_result_t fl_net_wifi_mgmt_build_assoc_req(const char *ssid, const uint8_t bssid[6],
                                             const uint8_t sta_mac[6], uint8_t *out,
                                             size_t out_cap, size_t *out_len) {
    size_t ssid_len;
    size_t pos;
    static const uint8_t he_cap_stub[] = {
        FL_WIFI_ELEM_ID_EXTENSION, 4u, FL_WIFI_EXT_HE_CAPABILITIES, 0x00, 0x80, 0x00
    };

    if (!ssid || !bssid || !sta_mac || !out || !out_len)
        return FL_RESULT_INVAL;
    ssid_len = strlen(ssid);
    if (ssid_len == 0u || ssid_len > 32u)
        return FL_RESULT_INVAL;
    pos = FL_WIFI_MGMT_HDR_LEN;
    if (out_cap < pos + 2u + ssid_len + sizeof(he_cap_stub))
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
    memcpy(out + pos, he_cap_stub, sizeof(he_cap_stub));
    pos += sizeof(he_cap_stub);
    *out_len = pos;
    return FL_RESULT_OK;
}
