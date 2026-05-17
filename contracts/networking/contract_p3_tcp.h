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

#endif /* FL_CONTRACT_P3_TCP_H */
