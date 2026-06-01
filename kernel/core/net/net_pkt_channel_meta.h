#ifndef NET_PKT_CHANNEL_META_H
#define NET_PKT_CHANNEL_META_H

#include "contract_pkt_channel_meta.h"
#include "contract_p5_file_delivery.h"
#include "contract_result.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Optional routing bits for Byte 0 not implied by **offer** alone. */
typedef struct fl_pkt_meta_route_ctx {
    uint8_t is_host;
    uint8_t is_sender_echo;
    uint8_t destination;
} fl_pkt_meta_route_ctx_t;

/** Map **fl_file_perms_t** into metadata bytes 1–2 (excludes revoked — use Byte 0). */
void fl_pkt_meta_file_bytes_from_perms(fl_file_perms_t perms,
                                       uint8_t *out_perms_byte,
                                       uint8_t *out_state_byte);

/** Merge metadata bytes 1–2 into an existing **file_perms** word. */
fl_file_perms_t fl_pkt_meta_file_perms_merge(fl_file_perms_t base,
                                             uint8_t perms_byte,
                                             uint8_t state_byte,
                                             int valid_bit);

/**
 * Build the 5-byte metadata prefix for a FILE_OFFER.
 * **route** may be NULL (private/id_based derived from **receiver_member_id**).
 */
fl_result_t fl_pkt_meta_encode_file_offer(const fl_server_file_offer_t *offer,
                                          const fl_pkt_meta_route_ctx_t *route,
                                          uint8_t meta[FL_PKT_CHANNEL_META_LEN]);

/** Apply metadata bytes to **out** (valid/revoke + permission mapping). */
fl_result_t fl_pkt_meta_decode_file_offer(const uint8_t meta[FL_PKT_CHANNEL_META_LEN],
                                          fl_server_file_offer_t *out);

#ifdef __cplusplus
}
#endif

#endif /* NET_PKT_CHANNEL_META_H */
