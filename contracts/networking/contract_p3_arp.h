/**
 * **P3-4 — ARP** (module contract, normative).
 *
 * Obligations:
 *   - IPv4 neighbour resolution over **IEEE 802.3 Ethernet** using **RFC 826** and
 *     **RFC 894** ethertype expectations.
 *   - **Cache** with **bounded** size and eviction policy documented; **flood** / rate
 *     limits may start as stubs but the contract surface must reserve the outcome channel
 *     (drop vs error) before wide deployment.
 *
 * See **docs/ROADMAP.md** Phase 3.
 */
#ifndef FL_CONTRACT_P3_ARP_H
#define FL_CONTRACT_P3_ARP_H

#include "contract_identity.h"

#define FL_CONTRACT_P3_4_ARP_CONTRACT_DEFINED 1

#endif /* FL_CONTRACT_P3_ARP_H */
