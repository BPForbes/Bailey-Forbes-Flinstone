/**
 * **P3-6 — UDP** (module contract, normative).
 *
 * **Distribution:** datagrams are **fl_net_frame_view_t** over UDP header+payload; ports
 * use **fl_port_be16_t**. Demux tables are **bounded**; under pressure the **drop vs error**
 * outcome is part of this contract and must be logged per **P6** when sinks are active.
 * Socket and queue pressure return **`fl_result_t`** in **FL_RESULT_MIN** … **FL_RESULT_MAX**
 * (**P0-2**). **CI:** UDP stability interop follows **P0-3** network-interop surface
 * (**contract_p0_ci.h**).
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

/**
 * Default per-socket RX queue depth for bounded **P3-6** implementations (datagram count,
 * not bytes). Override in translation units only with a **.ver** note.
 */
#ifndef FL_NET_UDP_DEFAULT_RX_QUEUE_DATAGRAMS
#define FL_NET_UDP_DEFAULT_RX_QUEUE_DATAGRAMS 64u
#endif

/** Lab implementation caps per-datagram stored payload (MTU-shaped; not 64 KiB). */
#ifndef FL_NET_UDP_LAB_RX_PAYLOAD_MAX
#define FL_NET_UDP_LAB_RX_PAYLOAD_MAX 1472u
#endif

/** Max distinct destination ports with an active RX queue in lab builds. */
#ifndef FL_NET_UDP_BIND_SLOTS_MAX
#define FL_NET_UDP_BIND_SLOTS_MAX 16u
#endif

/** **IANA** ephemeral **UDP** port range (**RFC 6335** dynamic range lower bound). */
#define FL_NET_UDP_EPHEMERAL_PORT_MIN 49152u
#define FL_NET_UDP_EPHEMERAL_PORT_MAX 65535u

_Static_assert(FL_NET_UDP_DEFAULT_RX_QUEUE_DATAGRAMS >= 1u,
               "UDP RX queue contract must allow at least one datagram");

#endif /* FL_CONTRACT_P3_UDP_H */
