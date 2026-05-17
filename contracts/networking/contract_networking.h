/*
 * P3 networking contract bundle (contracts/networking).
 *
 * Include this header (or individual contract_p3_*.h after contract_p3_wire.h) when
 * implementing or reviewing P3-1 through P3-12 work.
 * Build with -Icontracts/networking alongside -Icontracts/identity, -Icontracts/runtime,
 * and -Icontracts/foundations.
 *
 * **Not a clone of P2:** this bundle builds on **contract_extend.h** (P0–P1 vocabulary)
 * plus **contract_p3_wire.h** (octet-path types). **contract_p3_trust.h** composes the
 * **narrow P2-3 authz op slice** only. For principals, credentials, or elevation, include
 * **contract_identity.h** separately in the same translation unit—do not fold the full
 * identity bundle into every P3 shard.
 *
 * Bump FL_CONTRACT_P3_NETWORKING_REV when shard set or mandatory P3 vocabulary changes.
 */
#ifndef FL_CONTRACT_NETWORKING_H
#define FL_CONTRACT_NETWORKING_H

#include "contract_extend.h"
#include "contract_p3_wire.h"
#include "contract_p3_trust.h"

#define FL_CONTRACT_P3_NETWORKING_REV 2

#include "contract_p3_netdev.h"
#include "contract_p3_loopback.h"
#include "contract_p3_tap.h"
#include "contract_p3_arp.h"
#include "contract_p3_ipv4.h"
#include "contract_p3_udp.h"
#include "contract_p3_dhcp.h"
#include "contract_p3_tcp.h"
#include "contract_p3_dns.h"
#include "contract_p3_tls_hosted.h"
#include "contract_p3_wifi_deferred.h"
#include "contract_p3_ipv6_deferred.h"

#define FL_CONTRACT_P3_VOCABULARY_LOCK 1

#endif /* FL_CONTRACT_NETWORKING_H */
