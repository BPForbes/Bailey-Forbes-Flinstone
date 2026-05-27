#ifndef NET_LOOPBACK_H
#define NET_LOOPBACK_H

#include "contract_result.h"
#include "fl/driver/net.h"

#include <stddef.h>
#include <stdint.h>

int fl_net_loopback_owns(uint32_t dst_be);

fl_result_t fl_net_loopback_icmp_echo(const uint8_t *icmp_req, size_t icmp_len,
                                      uint8_t *icmp_reply, size_t reply_cap,
                                      size_t *reply_len);

fl_result_t fl_net_loopback_tcp_syn(const uint8_t *tcp_syn, size_t tcp_len, uint16_t sport,
                                    uint16_t dport, uint8_t *tcp_reply, size_t reply_cap,
                                    size_t *reply_len);

/** **P3-2** netdev **send** / **recv** handlers (see **fl_net_driver_t** in **P3-1**). */
fl_result_t fl_net_loopback_driver_send(fl_net_driver_t *drv, const fl_net_frame_view_t *frame);
fl_result_t fl_net_loopback_driver_recv(fl_net_driver_t *drv, fl_net_frame_mut_t *out);

void fl_net_loopback_reset(void);
uint64_t fl_net_loopback_stat_tx(void);
uint64_t fl_net_loopback_stat_rx(void);

#endif /* NET_LOOPBACK_H */
