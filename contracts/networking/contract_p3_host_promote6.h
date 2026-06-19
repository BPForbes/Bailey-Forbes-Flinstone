/**
 * **P3-13 / P3-11 — Host-transfer wire payloads** (#283).
 *
 * **OP_CTRL_HOST_PROMOTE** (`0x22`, 8 bytes): `[u16_be id][u32_be ipv4_nbo][u16_be port]`
 * **OP_CTRL_HOST_PROMOTE6** (`0x24`, 20 bytes): `[u16_be id][16 ipv6_nbo][u16_be port]`
 *
 * Port fields are **host byte order** on the wire per **contract_p3_server.h**.
 * IPv4/IPv6 address octets are **network byte order**.
 */
#ifndef FL_CONTRACT_P3_HOST_PROMOTE6_H
#define FL_CONTRACT_P3_HOST_PROMOTE6_H

#include "contract_p3_session_wire.h"

#define FL_CONTRACT_P3_HOST_PROMOTE6_DEFINED 1

#endif /* FL_CONTRACT_P3_HOST_PROMOTE6_H */
