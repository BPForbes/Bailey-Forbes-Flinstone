/**
 * **P3-7 — TCP (large)** (module contract, normative).
 *
 * **Distribution:** byte streams move in order over **IPv4** (**P3-5**); segment headers
 * add at least **FL_NET_TCP_HDR_LEN_MIN** octets. **MSS** defaults to
 * **FL_NET_TCP_MSS_IPV4_ETH_DEFAULT** until PMTU discovery updates it.
 *
 * **Time:** **P1-7** is the reference for RTO/backoff; document clock choice per track.
 *
 * **Interop:** acceptance includes host tool peers (**nc**, **socat**) as stated in the roadmap.
 */
#ifndef FL_CONTRACT_P3_TCP_H
#define FL_CONTRACT_P3_TCP_H

#include "contract_p3_wire.h"

#define FL_CONTRACT_P3_7_TCP_CONTRACT_DEFINED 1

#ifndef FL_NET_TCP_FLAG_SYN
#define FL_NET_TCP_FLAG_FIN 0x01u
#define FL_NET_TCP_FLAG_SYN 0x02u
#define FL_NET_TCP_FLAG_RST 0x04u
#define FL_NET_TCP_FLAG_PSH 0x08u
#define FL_NET_TCP_FLAG_ACK 0x10u
#endif

typedef enum {
    FL_NET_TCP_STATE_CLOSED = 0,
    FL_NET_TCP_STATE_LISTEN,
    FL_NET_TCP_STATE_SYN_SENT,
    FL_NET_TCP_STATE_SYN_RECV,
    FL_NET_TCP_STATE_ESTABLISHED,
    FL_NET_TCP_STATE_FIN_WAIT_1,
    FL_NET_TCP_STATE_FIN_WAIT_2,
    FL_NET_TCP_STATE_CLOSE_WAIT,
    FL_NET_TCP_STATE_CLOSING,
    FL_NET_TCP_STATE_LAST_ACK,
    FL_NET_TCP_STATE_TIME_WAIT
} fl_net_tcp_state_t;

_Static_assert(FL_NET_TCP_HDR_LEN_MIN >= 20u, "TCP header min matches wire vocabulary");

#endif /* FL_CONTRACT_P3_TCP_H */
