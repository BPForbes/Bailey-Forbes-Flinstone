#include "net_pkt_channel_meta.h"

#include "contract_p5_file_perms.h"

#include <string.h>

void fl_pkt_meta_file_bytes_from_perms(fl_file_perms_t perms,
                                       uint8_t *out_perms_byte,
                                       uint8_t *out_state_byte)
{
    uint8_t b1 = 0u;
    uint8_t b2 = 0u;

    if (!out_perms_byte || !out_state_byte)
        return;

    if (perms & FL_FILE_FLAG_EXPIRES)
        b1 |= FL_PKT_FILE_EXPIRATION;
    if (perms & FL_FILE_PERM_VIEW)
        b1 |= FL_PKT_FILE_VIEW;
    if (perms & FL_FILE_PERM_EDIT)
        b1 |= FL_PKT_FILE_EDIT;
    if (perms & FL_FILE_PERM_OVERWRITE)
        b1 |= FL_PKT_FILE_OVERWRITE;
    if (perms & FL_FILE_PERM_WRITE)
        b1 |= FL_PKT_FILE_WRITE;
    if (perms & FL_FILE_PERM_RUN)
        b1 |= FL_PKT_FILE_RUN;
    if (perms & FL_FILE_PERM_CREATE)
        b1 |= FL_PKT_FILE_CREATE;
    if (perms & FL_FILE_FLAG_PUBLIC)
        b1 |= FL_PKT_FILE_PUBLIC;

    if (perms & FL_FILE_FLAG_LOCKED)
        b2 |= FL_PKT_FILE_LOCKED;
    if (perms & FL_FILE_PERM_SERVER_SHARE)
        b2 |= FL_PKT_FILE_SVR_SHARE;
    if (perms & FL_FILE_FLAG_SCAN_REQUIRED)
        b2 |= FL_PKT_FILE_SCAN_REQ;
    if (perms & FL_FILE_PERM_DOWNLOAD)
        b2 |= FL_PKT_FILE_DL_ALLOW;
    if (perms & FL_FILE_PERM_ACCEPT)
        b2 |= FL_PKT_FILE_ACCEPT;

    *out_perms_byte = b1;
    *out_state_byte = b2;
}

fl_file_perms_t fl_pkt_meta_file_perms_merge(fl_file_perms_t base,
                                             uint8_t perms_byte,
                                             uint8_t state_byte,
                                             int valid_bit)
{
    fl_file_perms_t perms = base;

    perms &= (fl_file_perms_t) ~(FL_FILE_FLAG_EXPIRES | FL_FILE_PERM_VIEW |
                                 FL_FILE_PERM_EDIT | FL_FILE_PERM_OVERWRITE |
                                 FL_FILE_PERM_WRITE | FL_FILE_PERM_RUN |
                                 FL_FILE_PERM_CREATE | FL_FILE_FLAG_PUBLIC |
                                 FL_FILE_FLAG_LOCKED | FL_FILE_PERM_SERVER_SHARE |
                                 FL_FILE_FLAG_SCAN_REQUIRED | FL_FILE_PERM_DOWNLOAD |
                                 FL_FILE_PERM_ACCEPT | FL_FILE_FLAG_REVOKED);

    if (perms_byte & FL_PKT_FILE_EXPIRATION)
        perms |= FL_FILE_FLAG_EXPIRES;
    if (perms_byte & FL_PKT_FILE_VIEW)
        perms |= FL_FILE_PERM_VIEW;
    if (perms_byte & FL_PKT_FILE_EDIT)
        perms |= FL_FILE_PERM_EDIT;
    if (perms_byte & FL_PKT_FILE_OVERWRITE)
        perms |= FL_FILE_PERM_OVERWRITE;
    if (perms_byte & FL_PKT_FILE_WRITE)
        perms |= FL_FILE_PERM_WRITE;
    if (perms_byte & FL_PKT_FILE_RUN)
        perms |= FL_FILE_PERM_RUN;
    if (perms_byte & FL_PKT_FILE_CREATE)
        perms |= FL_FILE_PERM_CREATE;
    if (perms_byte & FL_PKT_FILE_PUBLIC)
        perms |= FL_FILE_FLAG_PUBLIC;

    if (state_byte & FL_PKT_FILE_LOCKED)
        perms |= FL_FILE_FLAG_LOCKED;
    if (state_byte & FL_PKT_FILE_SVR_SHARE)
        perms |= FL_FILE_PERM_SERVER_SHARE;
    if (state_byte & FL_PKT_FILE_SCAN_REQ)
        perms |= FL_FILE_FLAG_SCAN_REQUIRED;
    if (state_byte & FL_PKT_FILE_DL_ALLOW)
        perms |= FL_FILE_PERM_DOWNLOAD;
    if (state_byte & FL_PKT_FILE_ACCEPT)
        perms |= FL_FILE_PERM_ACCEPT;

    if (!valid_bit)
        perms |= FL_FILE_FLAG_REVOKED;
    else
        perms &= (fl_file_perms_t) ~FL_FILE_FLAG_REVOKED;

    return fl_file_perms_normalize(perms);
}

fl_result_t fl_pkt_meta_encode_file_offer(const fl_server_file_offer_t *offer,
                                          const fl_pkt_meta_route_ctx_t *route,
                                          uint8_t meta[FL_PKT_CHANNEL_META_LEN])
{
    uint8_t b0;
    uint8_t b1;
    uint8_t b2;

    if (!offer || !meta)
        return FL_RESULT_INVAL;

    memset(meta, 0, FL_PKT_CHANNEL_META_LEN);

    b0 = FL_PKT_TYPE;
    if (!fl_file_perms_is_revoked(offer->file_perms))
        b0 |= FL_PKT_VALID;

    if (offer->receiver_member_id != 0u) {
        b0 |= FL_PKT_PRIVATE;
        b0 |= FL_PKT_ID_BASED;
    }

    if (route) {
        if (route->is_host)
            b0 |= FL_PKT_IS_HOST;
        if (route->is_sender_echo)
            b0 |= FL_PKT_IS_SENDER;
        if (route->destination)
            b0 |= FL_PKT_DESTINATION;
    }

    fl_pkt_meta_file_bytes_from_perms(offer->file_perms, &b1, &b2);

    meta[0] = b0;
    meta[1] = b1;
    meta[2] = b2;
  /* Bytes 3–4: message domain — zero for FILE_OFFER */
    return FL_RESULT_OK;
}

fl_result_t fl_pkt_meta_decode_file_offer(const uint8_t meta[FL_PKT_CHANNEL_META_LEN],
                                          fl_server_file_offer_t *out)
{
    int valid;

    if (!meta || !out)
        return FL_RESULT_INVAL;

    valid = (meta[0] & FL_PKT_VALID) != 0;
    out->file_perms = fl_pkt_meta_file_perms_merge(out->file_perms, meta[1], meta[2], valid);
    return FL_RESULT_OK;
}
