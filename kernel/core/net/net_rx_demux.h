#ifndef NET_RX_DEMUX_H
#define NET_RX_DEMUX_H

#include "contract_result.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Non-blocking poll of every registered netdev RX queue, L2/L3/L4 demux into
 * the in-tree TCP FSM (and ICMP/UDP handlers where applicable).
 * Returns the number of frames consumed (0 when idle).
 */
unsigned fl_net_rx_demux_poll(unsigned max_frames);

#ifdef __cplusplus
}
#endif

#endif /* NET_RX_DEMUX_H */
