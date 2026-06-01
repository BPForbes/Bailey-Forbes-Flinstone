#ifndef NET_CHANNEL_SIDECAR_H
#define NET_CHANNEL_SIDECAR_H

#include "contract_p3_channel_sidecar.h"

fl_result_t fl_channel_sidecar_encode(const fl_channel_sidecar_t *sidecar,
                                      uint8_t *out,
                                      uint16_t out_cap,
                                      uint16_t *out_len);

fl_result_t fl_channel_sidecar_decode(const uint8_t *payload,
                                      uint16_t payload_len,
                                      fl_channel_sidecar_t *out);

/** True when relay may forward sidecar to this member (never on final hop). */
int fl_channel_sidecar_may_forward_to(uint16_t receiver_member_id,
                                      uint16_t target_member_id);

#endif /* NET_CHANNEL_SIDECAR_H */
