#include "net_wifi_he.h"

#include <string.h>

static uint8_t he_phy_channel_width_mhz(const uint8_t phy[11]) {
    if (!phy)
        return 20u;
    if (phy[0] & 0x40u)
        return 160u;
    if (phy[0] & 0x04u)
        return 80u;
    if (phy[0] & 0x02u)
        return 40u;
    return 20u;
}

int fl_net_wifi_ie_find_extension(const uint8_t *ies, size_t ies_len, uint8_t ext_id,
                                  const uint8_t **body, size_t *body_len) {
    size_t off = 0;

    if (!ies || !body || !body_len)
        return 0;
    *body = NULL;
    *body_len = 0;

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

        if (eid != FL_WIFI_ELEM_ID_EXTENSION || data_len < 1u)
            continue;
        if (data[0] != ext_id)
            continue;
        *body = data + 1u;
        *body_len = data_len - 1u;
        return 1;
    }
    return 0;
}

int fl_net_wifi_he_parse_capabilities(const uint8_t *body, size_t body_len,
                                      fl_net_wifi_he_cap_t *out) {
    const uint8_t *mac;
    const uint8_t *phy;
    size_t mac_len = 6u;
    size_t phy_len = 11u;

    if (!out)
        return 0;
    memset(out, 0, sizeof(*out));

    if (!body || body_len < mac_len + phy_len)
        return 0;

    mac = body;
    phy = body + mac_len;

    out->supports_ofdma = (mac[1] & 0x80u) ? 1u : 0u;
    out->supports_mu_mimo = (phy[0] & 0x01u) ? 1u : 0u;
    out->supports_twt = ((mac[3] & 0x06u) != 0u) ? 1u : 0u;
    out->channel_width_mhz = he_phy_channel_width_mhz(phy);
    out->max_nss_rx = 2u;
    out->max_nss_tx = 2u;
    if (phy[0] & 0x20u)
        out->supports_6ghz = 1u;
    return 1;
}

int fl_net_wifi_he_parse_operation(const uint8_t *body, size_t body_len,
                                   uint8_t *bss_color_out, uint8_t *twt_responder_out) {
    if (!body || body_len < 4u)
        return 0;
    if (bss_color_out)
        *bss_color_out = (uint8_t)(body[3] & 0x3fu);
    if (twt_responder_out)
        *twt_responder_out = (body[0] & 0x02u) ? 1u : 0u;
    return 1;
}

int fl_net_wifi_scan_enrich_from_ies(const uint8_t *ies, size_t ies_len,
                                     fl_net_wifi_scan_entry_t *entry) {
    const uint8_t *he_cap_body;
    const uint8_t *he_op_body;
    size_t he_cap_len = 0;
    size_t he_op_len = 0;
    fl_net_wifi_he_cap_t cap;

    if (!entry)
        return 0;

    if (!fl_net_wifi_ie_find_extension(ies, ies_len, FL_WIFI_EXT_HE_CAPABILITIES,
                                       &he_cap_body, &he_cap_len)) {
        entry->he_supported = 0u;
        return 1;
    }

    entry->he_supported = 1u;
    if (!fl_net_wifi_he_parse_capabilities(he_cap_body, he_cap_len, &cap))
        return 0;

    entry->channel_width_mhz = cap.channel_width_mhz;
    entry->twt_responder = cap.supports_twt;

    if (fl_net_wifi_ie_find_extension(ies, ies_len, FL_WIFI_EXT_HE_OPERATION,
                                      &he_op_body, &he_op_len)) {
        uint8_t color = 0;
        uint8_t twt_resp = 0;
        if (fl_net_wifi_he_parse_operation(he_op_body, he_op_len, &color, &twt_resp)) {
            entry->bss_color = color;
            if (twt_resp)
                entry->twt_responder = 1u;
        }
        cap.bss_color = entry->bss_color;
    }
    return 1;
}
