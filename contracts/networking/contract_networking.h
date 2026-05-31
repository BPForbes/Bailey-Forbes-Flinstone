/*
 * P3 networking contract bundle (contracts/networking).
 *
 * Include this header (or individual contract_p3_*.h after contract_p3_wire.h) when
 * implementing or reviewing P3-1 through P3-14 work.
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
#include "contract_p3_packet.h"
#include "contract_p3_socket.h"
#include "contract_p3_trust.h"

#define FL_CONTRACT_P3_NETWORKING_REV 14

#ifndef FL_CONTRACT_P3_WIRE_REV
#error "FL_CONTRACT_P3_WIRE_REV must be defined by contract_p3_wire.h"
#endif
_Static_assert(FL_CONTRACT_P3_WIRE_REV >= 1, "Unexpected P3 wire revision");

/** Keep in lockstep with **FL_CONTRACT_P3_WIRE_REV** in **contract_p3_wire.h**. */
#define FL_CONTRACT_P3_NETWORKING_EXPECT_WIRE_REV 3
_Static_assert(FL_CONTRACT_P3_WIRE_REV == FL_CONTRACT_P3_NETWORKING_EXPECT_WIRE_REV,
               "Bump FL_CONTRACT_P3_NETWORKING_REV when contract_p3_wire.h FL_CONTRACT_P3_WIRE_REV changes");

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
#include "contract_p3_background.h"
#include "contract_p3_sockets.h"
#include "contract_p3_session_wire.h"
#include "contract_p3_sftp_adapter.h"
#include "contract_p3_server.h"

#define FL_CONTRACT_P3_VOCABULARY_LOCK 1

_Static_assert(FL_CONTRACT_P3_VOCABULARY_LOCK == 1,
               "P3 umbrella must include the full shard set when vocabulary lock is on");

#endif /* FL_CONTRACT_NETWORKING_H */
