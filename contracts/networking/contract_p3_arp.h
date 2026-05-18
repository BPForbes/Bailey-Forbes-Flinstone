/**
 * **P3-4 — ARP** (module contract, normative).
 *
 * **Distribution:** **RFC 826** messages are carried in **IEEE 802.3** frames with
 * **FL_ETHERTYPE_ARP**. **Hardware type** **1** = Ethernet; **protocol type** **0x0800** =
 * IPv4 on the wire (see **RFC 894** expectations).
 *
 * **Cache:** table entries are **bounded**; eviction and stale-entry policy must be
 * documented beside implementation. Flood / rate behaviour returns **fl_result_t** or
 * drops per documented policy.
 */
#ifndef FL_CONTRACT_P3_ARP_H
#define FL_CONTRACT_P3_ARP_H

#include "contract_p3_wire.h"

#define FL_CONTRACT_P3_4_ARP_CONTRACT_DEFINED 1

#define FL_NET_ARP_HWTYPE_ETHERNET 1u
#define FL_NET_ARP_PROTOTYPE_IPV4 FL_ETHERTYPE_IPV4

#endif /* FL_CONTRACT_P3_ARP_H */
