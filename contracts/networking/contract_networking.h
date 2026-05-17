/*
 * P3 networking contract bundle (contracts/networking).
 *
 * Include this header (or individual contract_p3_*.h after contract_identity.h)
 * when implementing or reviewing P3-1 through P3-12 work.
 * Build with -Icontracts/networking alongside -Icontracts/identity, -Icontracts/runtime,
 * and -Icontracts/foundations.
 *
 * Inheritance: contract_identity.h brings P2 on top of P1 and P0. This bundle
 * adds P3 only.
 *
 * Bump FL_CONTRACT_P3_NETWORKING_REV when shard set or mandatory P3 vocabulary changes.
 */
#ifndef FL_CONTRACT_NETWORKING_H
#define FL_CONTRACT_NETWORKING_H

#include "contract_identity.h"

#define FL_CONTRACT_P3_NETWORKING_REV 1

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
