/**
 * **P3-6 — UDP** (module contract, normative).
 *
 * **Distribution:** datagrams are **fl_net_frame_view_t** over UDP header+payload; ports
 * use **fl_port_be16_t**. Demux tables are **bounded**; under pressure the **drop vs error**
 * outcome is part of this contract and must be logged per **P6** when sinks are active.
 *
 * **Caps:** single-datagram size is bounded by **FL_NET_CONTRACT_MAX_UDP_DATAGRAM** unless
 * a build flag documents a different lab ceiling.
 *
 * **Time:** **P1-7** backs timeout and rate-limit logic for sockets and echo servers.
 */
#ifndef FL_CONTRACT_P3_UDP_H
#define FL_CONTRACT_P3_UDP_H

#include "contract_p3_wire.h"

#define FL_CONTRACT_P3_6_UDP_CONTRACT_DEFINED 1

#endif /* FL_CONTRACT_P3_UDP_H */
