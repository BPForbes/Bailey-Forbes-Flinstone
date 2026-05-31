/**
 * **P3-13 — Server file packet codec contract** (module contract, normative).
 *
 * The packet module owns byte-order conversion, u16_be byte-string framing,
 * opcode validation, payload length checks, and FILE_* payload structure. It
 * must not decide view/edit/run/overwrite/expiration/revocation policy; those
 * decisions belong to the P5 file-share policy contracts.
 */
#ifndef FL_CONTRACT_P3_FILE_PACKET_H
#define FL_CONTRACT_P3_FILE_PACKET_H

#include "contract_p3_session_wire.h"
#include "contract_p5_file_delivery.h"

#include <stdint.h>

#define FL_CONTRACT_P3_FILE_PACKET_CONTRACT_DEFINED 1

static inline int fl_file_packet_is_file_opcode(uint8_t opcode)
{
    return opcode >= FL_NET_SESSION_OP_FILE_OFFER &&
           opcode <= FL_NET_SESSION_OP_FILE_STATUS;
}

fl_result_t fl_file_packet_encode_offer(const fl_server_file_offer_t *offer,
                                        uint8_t *out,
                                        uint16_t out_cap,
                                        uint16_t *out_len);

fl_result_t fl_file_packet_decode_offer(const uint8_t *payload,
                                        uint16_t payload_len,
                                        fl_server_file_offer_t *out);

fl_result_t fl_file_packet_encode_chunk(const fl_server_file_chunk_header_t *chunk,
                                        const uint8_t *data,
                                        uint32_t data_len,
                                        uint8_t *out,
                                        uint16_t out_cap,
                                        uint16_t *out_len);

fl_result_t fl_file_packet_decode_chunk(const uint8_t *payload,
                                        uint16_t payload_len,
                                        fl_server_file_chunk_header_t *chunk,
                                        fl_bytes_view_t *data);

fl_result_t fl_file_packet_encode_done(const fl_server_file_done_t *done,
                                       uint8_t *out,
                                       uint16_t out_cap,
                                       uint16_t *out_len);

fl_result_t fl_file_packet_decode_done(const uint8_t *payload,
                                       uint16_t payload_len,
                                       fl_server_file_done_t *out);

fl_result_t fl_file_packet_encode_accept(const char *share_id,
                                         uint16_t receiver_member_id,
                                         fl_server_file_disposition_t disposition,
                                         uint8_t *out,
                                         uint16_t out_cap,
                                         uint16_t *out_len);

fl_result_t fl_file_packet_decode_accept(const uint8_t *payload,
                                         uint16_t payload_len,
                                         char *share_id,
                                         uint16_t share_id_cap,
                                         uint16_t *receiver_member_id,
                                         fl_server_file_disposition_t *disposition);

fl_result_t fl_file_packet_encode_decline(const char *share_id,
                                          uint16_t receiver_member_id,
                                          uint8_t *out,
                                          uint16_t out_cap,
                                          uint16_t *out_len);

fl_result_t fl_file_packet_decode_decline(const uint8_t *payload,
                                          uint16_t payload_len,
                                          char *share_id,
                                          uint16_t share_id_cap,
                                          uint16_t *receiver_member_id);

fl_result_t fl_file_packet_encode_revoke(const char *share_id,
                                         uint16_t owner_member_id,
                                         uint8_t *out,
                                         uint16_t out_cap,
                                         uint16_t *out_len);

fl_result_t fl_file_packet_decode_revoke(const uint8_t *payload,
                                         uint16_t payload_len,
                                         char *share_id,
                                         uint16_t share_id_cap,
                                         uint16_t *owner_member_id);

fl_result_t fl_file_packet_validate_header(uint8_t opcode,
                                           uint8_t flags,
                                           uint16_t payload_len);

#endif /* FL_CONTRACT_P3_FILE_PACKET_H */
